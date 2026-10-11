/* AequiOS syscall dispatcher — two personalities behind one trap:
 *   nr < 0xAE00   → Linux compat layer (sys/linux.c)
 *   nr ≥ 0xAE00   → native AequiOS calls (below)
 *
 * Task-switch model: the frame on the kernel stack is what the exit
 * stub restores to userspace. To run another task we simply overwrite
 * that frame with the target task's saved context (see task.c).
 */
#include <kernel.h>
#include <syscalls.h>

static void ret(void *f, s64 v) { uctx_set_ret(f, (u64)v); }

/* every page of [ptr,ptr+n) must be present in the task's map */
bool user_mem_ok(struct task *t, u64 p, u64 n) {
    if (!t) return false;
    if (n == 0) return true;
    u64 root = task_root(t);
    for (u64 a = p & ~(PAGE_SIZE - 1); a < p + n; a += PAGE_SIZE)
        if (!vmm_translate(root, a)) return false;
    return true;
}

/* NUL-terminated string whose pages must all be present */
bool user_str_ok(struct task *t, const char *s) {
    u64 root = task_root(t);
    u64 p = (u64)s, checked = ~(u64)0;
    for (int i = 0; i < 8192; i++, p++) {
        u64 pg = p & ~(PAGE_SIZE - 1);
        if (pg != checked) {
            if (!vmm_translate(root, pg)) return false;
            checked = pg;
        }
        if (!*(const char *)p) return true;
    }
    return false;
}

static s64 sys_write(struct task *t, int fd, const void *buf, size_t n) {
    int v = task_fd_get(t, fd);
    if (v != -2 && fd > 2) return -9 /*EBADF*/;
    if (v >= 0) return -9;
    if (!user_mem_ok(t, (u64)buf, n)) return -14 /*EFAULT*/;
    const char *s = buf;
    for (size_t i = 0; i < n; i++) printk("%c", s[i]);
    return (s64)n;
}

static s64 sys_read(struct task *t, int fd, void *buf, size_t n) {
    int v = task_fd_get(t, fd);
    if (v == -3 || fd == 0) {
        char *s = buf;
        size_t i = 0;
        while (i < n) {
            int c = input_wait_char();   /* kbd + serial + paints mouse */
            s[i++] = (char)c;
            if (c == '\n') break;
        }
        return (s64)i;
    }
    if (v < 0) return -9;
    if (!user_mem_ok(t, (u64)buf, n)) return -14 /*EFAULT*/;
    s64 r = vfs_pread(v, buf, n, task_fd_off(t, fd));
    if (r > 0) task_fd_seek(t, fd, task_fd_off(t, fd) + r);
    return r;
}

static void list_cb(const char *name, size_t size) {
    printk("  %-28s %llu bytes\n", name, (unsigned long long)size);
}

/* Exit the current task and resume its parent with the exit code.
 * Shared with the Linux compat layer (exit_group). */
void sys_do_exit(void *frame, int code) {
    struct task *p = task_exit_and_resume(code);
    if (!p) panic("init exited — no parent to resume");
    uctx_set_ctx_frame(frame, task_ctx(p)); /* restore parent's saved ctx */
    uctx_set_ret(frame, (u64)code);         /* parent's SYS_spawn returns code */
}

static void do_spawn(void *frame, struct task *t, const char *path) {
    struct task *child = task_spawn(path);
    if (!child) { ret(frame, -1); return; }
    /* task_spawn already set child->parent = current */
    uctx_copy(task_ctx(t), frame);       /* park parent's ctx */
    uctx_set_ctx_frame(frame, task_ctx(child)); /* frame now runs the child */
    vmm_activate(task_root(child));
    task_set_current(child);
}

void sys_handle(void *frame) {
    struct task *t = task_current();
    u64 nr = uctx_syscall_nr(frame);
    u64 a0 = uctx_arg(frame, 0), a1 = uctx_arg(frame, 1);
    u64 a2 = uctx_arg(frame, 2);

    if (nr < AEQ_SYS_BASE) {
        if (!linux_handle(frame, nr, t)) ret(frame, -38 /*ENOSYS*/);
        return;
    }

    switch (nr) {
    case AEQ_SYS_write:
        ret(frame, sys_write(t, (int)a0, (const void *)a1, a2));
        break;
    case AEQ_SYS_read:
        ret(frame, sys_read(t, (int)a0, (void *)a1, a2));
        break;
    case AEQ_SYS_open: {
        if (!user_str_ok(t, (const char *)a0)) { ret(frame, -14); break; }
        int v = vfs_open((const char *)a0);
        ret(frame, v < 0 ? -2 : task_fd_alloc(t, v));
        break; }
    case AEQ_SYS_close:
        task_fd_close(t, (int)a0);
        ret(frame, 0);
        break;
    case AEQ_SYS_spawn:
        if (!user_str_ok(t, (const char *)a0)) { ret(frame, -14); break; }
        do_spawn(frame, t, (const char *)a0);
        break;
    case AEQ_SYS_exit:
        sys_do_exit(frame, (int)a0);
        break;
    case AEQ_SYS_meminfo:
        ret(frame, (s64)(pmm_free_bytes() / KB));
        break;
    case AEQ_SYS_brk: {
        u64 *b = task_brk_ptr(t);
        if (a0 == 0) ret(frame, (s64)*b);
        else { task_grow_brk(t, a0); ret(frame, (s64)*b); }
        break; }
    case AEQ_SYS_mmap: {
        u64 *next = task_mmap_ptr(t);
        u64 va = *next;
        u64 pages = ALIGN_UP(a1, PAGE_SIZE) / PAGE_SIZE;
        for (u64 i = 0; i < pages; i++) task_map_page(t, va + i * PAGE_SIZE, 0);
        *next = va + pages * PAGE_SIZE;
        ret(frame, (s64)va);
        break; }
    case AEQ_SYS_ls:
        vfs_list(list_cb);
        ret(frame, 0);
        break;
    case AEQ_SYS_uname: {
        const char *s = "AequiOS/Ordo 0.1 (scratch kernel)";
        if (!user_mem_ok(t, a0, strlen(s) + 1)) { ret(frame, -14); break; }
        char *d = (char *)a0;
        for (size_t i = 0; s[i]; i++) d[i] = s[i];
        d[strlen(s)] = 0;
        ret(frame, 0);
        break; }
    case AEQ_SYS_reboot:
        arch_reboot();
        break;
    default:
        ret(frame, -38);
        break;
    }
}
