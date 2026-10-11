/* Linux compat — translates a subset of the Linux syscall ABI onto
 * AequiOS services. This is what lets unmodified statically-linked
 * Linux ELFs run on Ordo. Covered calls are listed in docs/LINUX-ABI.md.
 *
 * Syscall numbers are per-architecture; the same table shape works on
 * both because argument order and return conventions are identical.
 */
#include <kernel.h>

#ifdef __x86_64__
#define L_read        0
#define L_write       1
#define L_open        2
#define L_close       3
#define L_mmap        9
#define L_brk         12
#define L_writev      20
#define L_uname       63
#define L_arch_prctl  158
#define L_set_tid_address 218
#define L_exit_group  231
#define L_openat      257
#else /* aarch64 */
#define L_ioctl       29
#define L_openat      56
#define L_close       57
#define L_read        63
#define L_write       64
#define L_writev      66
#define L_exit_group  94
#define L_set_tid_address 96
#define L_uname       160
#define L_brk         214
#define L_mmap        222
#endif

struct linux_utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

static void l_ret(void *f, s64 v) { uctx_set_ret(f, (u64)v); }

static s64 l_write(struct task *t, int fd, const void *buf, size_t n) {
    int v = task_fd_get(t, fd);
    if (fd >= 1 && fd <= 2) v = -2;
    if (v != -2) return -9;
    const char *s = buf;
    for (size_t i = 0; i < n; i++) printk("%c", s[i]);
    return (s64)n;
}

static s64 l_read(struct task *t, int fd, void *buf, size_t n) {
    int v = task_fd_get(t, fd);
    if (fd == 0 || v == -3) {
        char *s = buf;
        size_t i = 0;
        while (i < n) {
            int c = serial_getc_poll();
            if (c < 0) continue;
            s[i++] = (char)c;
            if (c == '\n') break;
        }
        return (s64)i;
    }
    if (v < 0) return -9;
    s64 r = vfs_pread(v, buf, n, task_fd_off(t, fd));
    if (r > 0) task_fd_seek(t, fd, task_fd_off(t, fd) + r);
    return r;
}

static s64 l_open(struct task *t, const char *path) {
    int v = vfs_open(path);
    if (v < 0) return -2 /*ENOENT*/;
    int fd = task_fd_alloc(t, v);
    return fd < 0 ? -24 /*EMFILE*/ : fd;
}

static s64 l_uname(u64 ubuf) {
    struct linux_utsname *u = (struct linux_utsname *)ubuf;
    memset(u, 0, sizeof(*u));
    strcpy(u->sysname, "AequiOS");
    strcpy(u->nodename, "aequi");
#ifdef __x86_64__
    strcpy(u->machine, "x86_64");
#else
    strcpy(u->machine, "aarch64");
#endif
    strcpy(u->release, "0.1.0-aequi");
    strcpy(u->version, "Ordo (AequiOS native kernel)");
    return 0;
}

static s64 l_brk(struct task *t, u64 addr) {
    u64 *b = task_brk_ptr(t);
    if (addr == 0) return (s64)*b;
    if (addr < *b) return (s64)*b;   /* shrinking not supported */
    task_grow_brk(t, addr);
    return (s64)*b;
}

static s64 l_mmap(struct task *t, u64 addr, u64 len, int prot, int flags) {
    (void)prot;
    u64 *next = task_mmap_ptr(t);
    u64 va = (flags & 0x10 /*MAP_FIXED*/) ? addr : *next;
    u64 pages = ALIGN_UP(len, PAGE_SIZE) / PAGE_SIZE;
    for (u64 i = 0; i < pages; i++) task_map_page(t, va + i * PAGE_SIZE, 0);
    if (!(flags & 0x10)) *next = va + pages * PAGE_SIZE;
    return (s64)va;
}

static s64 l_writev(struct task *t, int fd, const void *iov, int iovcnt) {
    const struct { void *base; size_t len; } *v = iov;
    s64 total = 0;
    for (int i = 0; i < iovcnt; i++) {
        s64 r = l_write(t, fd, v[i].base, v[i].len);
        if (r < 0) return r;
        total += r;
    }
    return total;
}

bool linux_handle(void *frame, u64 nr, struct task *t) {
    u64 a0 = uctx_arg(frame, 0), a1 = uctx_arg(frame, 1);
    u64 a2 = uctx_arg(frame, 2), a3 = uctx_arg(frame, 3);

    switch (nr) {
    case L_write:
        l_ret(frame, l_write(t, (int)a0, (const void *)a1, a2));
        return true;
    case L_read:
        l_ret(frame, l_read(t, (int)a0, (void *)a1, a2));
        return true;
    case L_writev:
        l_ret(frame, l_writev(t, (int)a0, (const void *)a1, (int)a2));
        return true;
#ifdef __x86_64__
    case L_open:
#endif
    case L_openat:
        /* openat(dirfd, path, flags, mode) — dirfd ignored (flat initramfs) */
#ifdef __x86_64__
        l_ret(frame, l_open(t, (const char *)(nr == L_open ? a0 : a1)));
#else
        l_ret(frame, l_open(t, (const char *)a1));
#endif
        return true;
    case L_close:
        task_fd_close(t, (int)a0);
        l_ret(frame, 0);
        return true;
    case L_uname:
        l_ret(frame, l_uname(a0));
        return true;
    case L_brk:
        l_ret(frame, l_brk(t, a0));
        return true;
    case L_mmap:
        l_ret(frame, l_mmap(t, a0, a1, (int)a2, (int)a3));
        return true;
    case L_exit_group:
        sys_do_exit(frame, (int)a0);
        return true;
#ifdef __x86_64__
    case L_arch_prctl:            /* ignore ARCH_SET_FS etc. for v0 */
        l_ret(frame, 0);
        return true;
#endif
    case L_set_tid_address:
        l_ret(frame, 1);
        return true;
#ifdef L_ioctl
    case L_ioctl:                 /* serial console is not a tty — honest ENOTTY */
        l_ret(frame, -25);
        return true;
#endif
    default:
        return false;
    }
}
