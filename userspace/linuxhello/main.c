/* linux-hello - an unmodified Linux-ABI binary: raw Linux syscall
 * numbers, no AequiOS headers. Running correctly under Ordo proves the
 * compat layer is translating the ABI, not just printing text. */
typedef unsigned long u64;
typedef long s64;
typedef unsigned long size_t;

#if defined(__x86_64__)
#define LSYS_read        0
#define LSYS_write       1
#define LSYS_open        2
#define LSYS_close       3
#define LSYS_mmap        9
#define LSYS_brk         12
#define LSYS_writev      20
#define LSYS_uname       63
#define LSYS_exit_group  231
#define LSYS_openat      257

static inline s64 lsys(s64 nr, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5) {
    s64 ret;
    register u64 r10 __asm__("r10") = a3;
    register u64 r8  __asm__("r8")  = a4;
    register u64 r9  __asm__("r9")  = a5;
    __asm__ volatile ("syscall"
        : "=a"(ret)
        : "a"(nr), "D"(a0), "S"(a1), "d"(a2), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory");
    return ret;
}
#define LSYSC_OPEN(path) lsys(LSYS_openat, -100 /*AT_FDCWD*/, (u64)(path), 0, 0, 0, 0)

#elif defined(__aarch64__)
#define LSYS_close       57
#define LSYS_read        63
#define LSYS_write       64
#define LSYS_writev      66
#define LSYS_exit_group  94
#define LSYS_uname       160
#define LSYS_brk         214
#define LSYS_mmap        222
#define LSYS_openat      56

static inline s64 lsys(s64 nr, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5) {
    register s64 x0 __asm__("x0") = a0;
    register u64 x1 __asm__("x1") = a1;
    register u64 x2 __asm__("x2") = a2;
    register u64 x3 __asm__("x3") = a3;
    register u64 x4 __asm__("x4") = a4;
    register u64 x5 __asm__("x5") = a5;
    register s64 x8 __asm__("x8") = nr;
    __asm__ volatile ("svc #0"
        : "+r"(x0)
        : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x8)
        : "memory");
    return x0;
}
#define LSYSC_OPEN(path) lsys(LSYS_openat, -100 /*AT_FDCWD*/, (u64)(path), 0, 0, 0, 0)
#else
#error "unsupported arch"
#endif

static u64 slen(const char *s) { const char *p = s; while (*p) p++; return p - s; }
static void lputs(const char *s) { lsys(LSYS_write, 1, (u64)s, slen(s), 0, 0, 0); }
static void lwrite(const void *b, u64 n) { lsys(LSYS_write, 1, (u64)b, n, 0, 0, 0); }

struct iovec { void *base; u64 len; };

struct utsname {
    char sysname[65], nodename[65], release[65],
         version[65], machine[65], domainname[65];
};

int main(void) {
    lputs("\n=== linux-hello: Linux ABI on the AequiOS kernel ===\n");

    /* uname(2) */
    struct utsname u;
    if (lsys(LSYS_uname, (u64)&u, 0, 0, 0, 0, 0) == 0) {
        lputs("uname(2): ");
        lputs(u.sysname); lputs(" ");
        lputs(u.release); lputs(" ");
        lputs(u.machine); lputs("\n");
    }

    /* openat(2)/open(2) + read(2) on /etc/os-release from the initramfs */
    s64 fd = LSYSC_OPEN("/etc/os-release");
    if (fd >= 0) {
        char buf[512];
        s64 n = lsys(LSYS_read, (u64)fd, (u64)buf, sizeof(buf), 0, 0, 0);
        lputs("os-release via Linux openat/read:\n");
        if (n > 0) lwrite(buf, (u64)n);
        lsys(LSYS_close, (u64)fd, 0, 0, 0, 0, 0);
    } else {
        lputs("openat failed\n");
    }

    /* brk(2): grow the heap, write through it, read back */
    s64 base = lsys(LSYS_brk, 0, 0, 0, 0, 0, 0);
    s64 top  = lsys(LSYS_brk, (u64)base + 8192, 0, 0, 0, 0, 0);
    if (top >= base + 8192) {
        char *heap = (char *)base;
        const char *msg = "brk() memory works on AequiOS";
        for (u64 i = 0; msg[i]; i++) heap[i] = msg[i];
        heap[slen(msg)] = 0;
        lputs(heap); lputs("\n");
    } else {
        lputs("brk failed\n");
    }

    /* mmap(2) anonymous - write a counter pattern, verify a few bytes */
    u64 va = (u64)lsys(LSYS_mmap, 0, 4096, 3 /*PROT_RW*/, 0x22 /*ANON|PRIV*/, -1, 0);
    if ((s64)va > 0) {
        unsigned char *p = (unsigned char *)va;
        for (int i = 0; i < 256; i++) p[i] = (unsigned char)i;
        lputs("mmap page check: ");
        lputs((p[200] == 200 && p[5] == 5) ? "PASS" : "FAIL");
        lputs("\n");
    }

    /* writev(2) */
    const char *a = "writev(2) joins ";
    const char *b = "two buffers\n";
    struct iovec iov[2] = { {(void *)a, slen(a)}, {(void *)b, slen(b)} };
    lsys(LSYS_writev, 1, (u64)iov, 2, 0, 0, 0);

    lputs("=== linux-hello done - exit_group(42) ===\n\n");
    lsys(LSYS_exit_group, 42, 0, 0, 0, 0, 0);
    return 0;
}
