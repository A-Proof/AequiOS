/* AequiOS kernel — common types and helpers. */
#ifndef AEQ_KERNEL_H
#define AEQ_KERNEL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <limine.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;
typedef uintptr_t uptr;
typedef intptr_t  sptr;

#define KB (1024ULL)
#define MB (1024ULL * 1024)
#define GB (1024ULL * 1024 * 1024)

#define ALIGN_UP(x, a)   (((u64)(x) + (a) - 1) & ~((u64)(a) - 1))
#define ALIGN_DOWN(x, a) ((u64)(x) & ~((u64)(a) - 1))
#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

#define PAGE_SIZE 4096

extern u64 hhdm_offset;

/* Translate a physical address to a kernel-accessible virtual address
 * through the Limine-provided higher-half direct map. */
static inline void *phys_to_virt(u64 pa) { return (void *)(pa + hhdm_offset); }
static inline u64   virt_to_phys(const void *va) { return (u64)va - hhdm_offset; }

/* printk.c */
void printk(const char *fmt, ...);
void early_printk(const char *fmt, ...);   /* serial only, safe before console init */
void printk_console_ready(void);
_Noreturn void panic(const char *fmt, ...);

/* kstring.c */
size_t strlen(const char *s);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);
char  *strcpy(char *d, const char *s);
char  *strncpy(char *d, const char *s, size_t n);
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
int    memcmp(const void *a, const void *b, size_t n);
char  *strchr(const char *s, int c);
char  *strtok(char *str, const char *delim);
int    snprintf(char *buf, size_t size, const char *fmt, ...);

/* arch hooks — implemented per architecture */
void arch_early_init(void);          /* serial, exceptions, syscall entry */
void arch_late_init(void);           /* after pmm/vmm ready (e.g. MMIO maps) */
void arch_halt(void);
void arch_reboot(void);
void serial_init(void);
void serial_putc(char c);
int  serial_getc_poll(void);         /* -1 if no data */
u64  arch_get_page_root(void);       /* phys addr of active kernel root table */
void arch_set_page_root(u64 root);

/* syscall interface glue (per arch): */
void arch_init_syscalls(void);
struct task;
void arch_enter_user(struct task *t); /* never returns */
u64  uctx_syscall_nr(void *ctx);
u64  uctx_arg(void *ctx, int i);
void uctx_set_ret(void *ctx, u64 v);
void uctx_set_ctx_frame(void *frame, const void *ctx);
void uctx_copy(void *dst, const void *src);
void arch_make_user_ctx(void *ctx, u64 entry, u64 ustack);

/* memory */
void pmm_init(struct limine_memmap_response *mm);
u64  pmm_alloc_page(void);
u64  pmm_alloc_contig(u64 pages);
void pmm_free_page(u64 pa);
u64  pmm_total_pages(void);
u64  pmm_free_pages(void);
u64  pmm_total_bytes(void);
u64  pmm_free_bytes(void);

void kmalloc_init(void);
void *kmalloc(size_t n);
void *kzalloc(size_t n);
void  kfree(void *p);

/* vmm: map pa at va in the given page-table root. flags is arch-abstract. */
#define VMM_WRITE   (1 << 0)
#define VMM_USER    (1 << 1)
#define VMM_DEVICE  (1 << 2)
#define VMM_EXEC    (1 << 3)
void vmm_init(void);
void vmm_map(u64 root, u64 va, u64 pa, u64 flags);
u64  vmm_translate(u64 root, u64 va); /* va → pa, 0 if unmapped */
u64  vmm_new_user_root(void);       /* root with kernel half shared, user half empty */
void vmm_activate(u64 root);
void vmm_destroy_user_root(u64 root);

/* console/framebuffer */
void fb_init(void);
void fb_putc(char c);
void fb_splash(void);
bool fb_ready(void);
u64  fb_width(void);
u64  fb_height(void);
void px(int x, int y, u32 c);
u32  fb_get(int x, int y);
void fb_fill_rect(int x, int y, int w, int h, u32 c);
void fb_text(int x, int y, const char *s, u32 color, int scale);
void fb_set_console_rect(int x, int y, int w, int h);
void fb_repaint(void);
void fb_dst_set(u32 *pix, u64 pitch, u64 w, u64 h);   /* NULL → real fb */
u32 *fb_console_surface(int *pitch, int *w, int *h);
bool fb_console_dirty(void);
void fb_console_clean(void);

/* input + desktop (Aura v1) */
void input_init(void);
int  input_getc_poll(void);            /* -1 if no key */
bool input_poll_mouse(s32 *dx, s32 *dy, u8 *btn);
int  input_wait_char(void);            /* blocks: kbd/serial + paints mouse */
void desktop_init(void);
void desktop_poll_mouse(void);
bool desktop_running(void);

/* vfs/initramfs */
int  vfs_init(void);
int  vfs_open(const char *path);
int  vfs_read(int fd, void *buf, size_t n);
int  vfs_pread(int fd, void *buf, size_t n, u64 off);
long vfs_size(int fd);
int  vfs_close(int fd);
void vfs_list(void (*cb)(const char *name, size_t size));

/* tasks */
struct task;
struct task *task_spawn(const char *path);     /* NULL on failure */
void   task_run(struct task *t);               /* enter user mode, never returns */
struct task *task_exit_and_resume(int code);
struct task *task_current(void);
void   task_set_current(struct task *t);
u64   *task_brk_ptr(struct task *t);
void   task_grow_brk(struct task *t, u64 new_brk);
u64   *task_mmap_ptr(struct task *t);
void  *task_map_page(struct task *t, u64 va, u64 flags);
int    task_fd_alloc(struct task *t, int vfs_fd);
int    task_fd_get(struct task *t, int fd);
u64    task_fd_off(struct task *t, int fd);
void   task_fd_seek(struct task *t, int fd, u64 off);
void   task_fd_close(struct task *t, int fd);
void  *task_ctx(struct task *t);
u64    task_root(struct task *t);

/* syscall dispatch (sys/syscall.c) */
void sys_handle(void *uctx);
void sys_do_exit(void *frame, int code);
bool user_mem_ok(struct task *t, u64 ptr, u64 n);
bool user_str_ok(struct task *t, const char *s);

/* linux compat (sys/linux.c) — returns true if nr handled */
bool linux_handle(void *uctx, u64 nr, struct task *t);

#endif /* AEQ_KERNEL_H */
