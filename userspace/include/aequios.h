/* AequiOS userspace mini-lib — native syscalls live at base 0xAE00 so
 * they never collide with the Linux ABI personality. */
#ifndef AEQ_USERSPACE_H
#define AEQ_USERSPACE_H

typedef unsigned long u64;
typedef long s64;
typedef unsigned int u32;
typedef unsigned long size_t;

#define AEQ_SYS_BASE    0xAE00
#define AEQ_SYS_write   (AEQ_SYS_BASE + 0)
#define AEQ_SYS_read    (AEQ_SYS_BASE + 1)
#define AEQ_SYS_open    (AEQ_SYS_BASE + 2)
#define AEQ_SYS_close   (AEQ_SYS_BASE + 3)
#define AEQ_SYS_spawn   (AEQ_SYS_BASE + 4)
#define AEQ_SYS_exit    (AEQ_SYS_BASE + 5)
#define AEQ_SYS_meminfo (AEQ_SYS_BASE + 6)
#define AEQ_SYS_brk     (AEQ_SYS_BASE + 7)
#define AEQ_SYS_mmap    (AEQ_SYS_BASE + 8)
#define AEQ_SYS_ls      (AEQ_SYS_BASE + 9)
#define AEQ_SYS_reboot  (AEQ_SYS_BASE + 10)
#define AEQ_SYS_uname   (AEQ_SYS_BASE + 11)

s64 aequi_call0(s64 nr);
s64 aequi_call1(s64 nr, u64 a0);
s64 aequi_call2(s64 nr, u64 a0, u64 a1);
s64 aequi_call3(s64 nr, u64 a0, u64 a1, u64 a2);

/* mini libc */
int   puts(const char *s);
int   putc(char c);
void  uprintf(const char *fmt, ...);
int   ustrcmp(const char *a, const char *b);
u64   ustrlen(const char *s);
void  ustrcpy(char *d, const char *s);
int   ustrncmp(const char *a, const char *b, u64 n);
char *ustrchr(const char *s, int c);

/* libc-ish convenience wrappers */
static inline s64 sys_write(int fd, const void *b, u64 n) {
    return aequi_call3(AEQ_SYS_write, (u64)fd, (u64)b, n);
}
static inline s64 sys_read(int fd, void *b, u64 n) {
    return aequi_call3(AEQ_SYS_read, (u64)fd, (u64)b, n);
}
static inline s64 sys_open(const char *p) {
    return aequi_call1(AEQ_SYS_open, (u64)p);
}
static inline s64 sys_close(int fd) {
    return aequi_call1(AEQ_SYS_close, (u64)fd);
}
static inline s64 sys_spawn(const char *p) {
    return aequi_call1(AEQ_SYS_spawn, (u64)p);
}
static inline void sys_exit(int code) {
    aequi_call1(AEQ_SYS_exit, (u64)code);
    for (;;) {}
}
static inline s64 sys_meminfo(void) { return aequi_call0(AEQ_SYS_meminfo); }
static inline s64 sys_ls(void)      { return aequi_call0(AEQ_SYS_ls); }
static inline s64 sys_uname(char *b) { return aequi_call1(AEQ_SYS_uname, (u64)b); }
static inline void sys_reboot(void) { aequi_call0(AEQ_SYS_reboot); for (;;) {} }

#endif
