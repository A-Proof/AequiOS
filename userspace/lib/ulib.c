/* mini libc for AequiOS userspace */
#include <aequios.h>
#include <stdarg.h>

int putc(char c) {
    sys_write(1, &c, 1);
    return c;
}

int puts(const char *s) {
    while (*s) putc(*s++);
    putc('\n');
    return 0;
}

u64 ustrlen(const char *s) {
    const char *p = s;
    while (*p) p++;
    return p - s;
}

int ustrcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int ustrncmp(const char *a, const char *b, u64 n) {
    for (u64 i = 0; i < n; i++)
        if (a[i] != b[i] || !a[i]) return (unsigned char)a[i] - (unsigned char)b[i];
    return 0;
}

void ustrcpy(char *d, const char *s) {
    while ((*d++ = *s++));
}

char *ustrchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (*s == (char)c) ? (char *)s : (void *)0;
}

static void u_emit_u64(u64 v, int base, int sgn) {
    char buf[32];
    int i = 0;
    if (sgn && (s64)v < 0) { putc('-'); v = (u64)(-(s64)v); }
    do {
        int d = v % base;
        buf[i++] = d < 10 ? '0' + d : 'a' + d - 10;
        v /= base;
    } while (v);
    while (i) putc(buf[--i]);
}

void uprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    for (; *fmt; fmt++) {
        if (*fmt != '%') { putc(*fmt); continue; }
        fmt++;
        int lng = 0;
        if (*fmt == 'l') { lng = 1; fmt++; if (*fmt == 'l') fmt++; }
        switch (*fmt) {
        case 's': { const char *s = va_arg(ap, const char *);
                    while (s && *s) putc(*s++); break; }
        case 'c': putc((char)va_arg(ap, int)); break;
        case 'd': u_emit_u64(lng ? va_arg(ap, s64) : va_arg(ap, int), 10, 1); break;
        case 'u': u_emit_u64(lng ? va_arg(ap, u64) : va_arg(ap, u32), 10, 0); break;
        case 'x': u_emit_u64(lng ? va_arg(ap, u64) : va_arg(ap, u32), 16, 0); break;
        case 'p': puts("0x"); u_emit_u64(va_arg(ap, u64), 16, 0); break;
        case '%': putc('%'); break;
        default: putc('%'); putc(*fmt); break;
        }
    }
    va_end(ap);
}
