/* Vita — process management.
 * v0 model: one user task runs at a time. spawn() suspends the parent,
 * runs the child to completion, and exit() resumes the parent. The
 * "scheduler" is a stack of task frames — cooperative, deterministic. */
#include <kernel.h>
#include <elf.h>
#include <syscalls.h>

#define MAX_FDS 16
#define USER_STACK_PAGES 8
#define USER_STACK_TOP   0x80000000ULL      /* 2 GiB — inside both user maps */
#define USER_MMAP_BASE   0x40000000ULL

struct task_fd {
    int vfs_fd;          /* -1 = free; -2 = console out; -3 = console in */
    u64 off;
};

struct task {
    char name[64];
    u64 root;                       /* user page-table root (phys) */
    u8  ctx[512];                   /* arch register frame (uctx) */
    u64 brk, mmap_next;
    struct task_fd fds[MAX_FDS];
    struct task *parent;
    bool dead;
};

static struct task *current;

struct task *task_current(void) { return current; }
void task_set_current(struct task *t) { current = t; }
void *task_ctx(struct task *t) { return t->ctx; }
u64   task_root(struct task *t) { return t->root; }
u64 *task_brk_ptr(struct task *t) { return &t->brk; }
u64 *task_mmap_ptr(struct task *t) { return &t->mmap_next; }

/* Map a fresh physical page at va inside t's user space. Returns HHDM ptr. */
void *task_map_page(struct task *t, u64 va, u64 flags) {
    u64 pa = pmm_alloc_page();
    if (!pa) return NULL;
    vmm_map(t->root, va, pa, flags | VMM_USER | VMM_WRITE);
    return phys_to_virt(pa);
}

static void map_range(struct task *t, u64 va, u64 pages, u64 flags) {
    for (u64 i = 0; i < pages; i++)
        task_map_page(t, va + i * PAGE_SIZE, flags);
}

int task_fd_alloc(struct task *t, int vfs_fd) {
    for (int i = 3; i < MAX_FDS; i++) {
        if (t->fds[i].vfs_fd == -1) {
            t->fds[i].vfs_fd = vfs_fd;
            t->fds[i].off = 0;
            return i;
        }
    }
    return -1;
}

int task_fd_get(struct task *t, int fd) {
    if (fd < 0 || fd >= MAX_FDS) return -1;
    return t->fds[fd].vfs_fd;
}

u64 task_fd_off(struct task *t, int fd) { return t->fds[fd].off; }
void task_fd_seek(struct task *t, int fd, u64 off) { t->fds[fd].off = off; }
void task_fd_close(struct task *t, int fd) {
    if (fd > 2 && fd < MAX_FDS) t->fds[fd].vfs_fd = -1;
}

void task_grow_brk(struct task *t, u64 new_brk) {
    u64 old_top = ALIGN_UP(t->brk, PAGE_SIZE);
    u64 new_top = ALIGN_UP(new_brk, PAGE_SIZE);
    if (new_top > old_top) map_range(t, old_top, (new_top - old_top) / PAGE_SIZE, 0);
    t->brk = new_brk;
}

/* load ELF image at `data` into a task and set its initial context */
static bool task_load(struct task *t, const void *data, size_t size) {
    u64 brk = 0;
    u64 entry = elf_load(t, data, size, &brk);
    if (!entry) return false;
    t->brk = brk;

    /* user stack */
    map_range(t, USER_STACK_TOP - USER_STACK_PAGES * PAGE_SIZE,
              USER_STACK_PAGES, 0);
    arch_make_user_ctx(t->ctx, entry, USER_STACK_TOP);
    return true;
}

struct task *task_spawn(const char *path) {
    int fd = vfs_open(path);
    if (fd < 0) { early_printk("spawn: %s not found\n", path); return NULL; }
    long sz = vfs_size(fd);
    void *img = kmalloc(sz);
    if (!img || vfs_read(fd, img, sz) != sz) return NULL;
    vfs_close(fd);

    struct task *t = kzalloc(sizeof(*t));
    strncpy(t->name, path, sizeof(t->name) - 1);
    t->root = vmm_new_user_root();
    t->mmap_next = USER_MMAP_BASE;
    for (int i = 0; i < MAX_FDS; i++) t->fds[i].vfs_fd = -1;
    t->fds[0].vfs_fd = -3;              /* stdin  = serial */
    t->fds[1].vfs_fd = -2;              /* stdout = console */
    t->fds[2].vfs_fd = -2;              /* stderr = console */
    t->parent = current;

    if (!task_load(t, img, sz)) {
        early_printk("spawn: %s not a valid ELF\n", path);
        kfree(img);
        return NULL;
    }
    kfree(img);
    return t;
}

/* run `t` (no parent — used once, for init) */
void task_run(struct task *t) {
    current = t;
    vmm_activate(t->root);
    arch_enter_user(t);                 /* never returns */
}

/* resume current task's parent; returns the parent or NULL */
struct task *task_exit_and_resume(int code) {
    struct task *dead = current;
    struct task *p = dead->parent;
    (void)code;
    if (!p) return NULL;
    current = p;
    vmm_activate(p->root);
    dead->dead = true;
    /* v0: leak the child (root + pages). Freed when we add reaping. */
    return p;
}
