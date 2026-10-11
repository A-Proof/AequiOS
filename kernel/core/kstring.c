#include <kernel.h>

size_t strlen(const char *s) {
    const char *p = s;
    while (*p) p++;
    return p - s;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i] || !a[i]) return (unsigned char)a[i] - (unsigned char)b[i];
    }
    return 0;
}

char *strcpy(char *d, const char *s) {
    char *r = d;
    while ((*d++ = *s++));
    return r;
}

char *strncpy(char *d, const char *s, size_t n) {
    size_t i = 0;
    for (; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}

void *memset(void *d, int c, size_t n) {
    u8 *p = d;
    for (size_t i = 0; i < n; i++) p[i] = (u8)c;
    return d;
}

void *memcpy(void *d, const void *s, size_t n) {
    u8 *dp = d; const u8 *sp = s;
    for (size_t i = 0; i < n; i++) dp[i] = sp[i];
    return d;
}

void *memmove(void *d, const void *s, size_t n) {
    u8 *dp = d; const u8 *sp = s;
    if (dp < sp)
        for (size_t i = 0; i < n; i++) dp[i] = sp[i];
    else
        for (size_t i = n; i > 0; i--) dp[i - 1] = sp[i - 1];
    return d;
}

int memcmp(const void *a, const void *b, size_t n) {
    const u8 *x = a, *y = b;
    for (size_t i = 0; i < n; i++) {
        if (x[i] != y[i]) return x[i] - y[i];
    }
    return 0;
}

char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (*s == (char)c) ? (char *)s : NULL;
}

static void sputc(char **buf, size_t *n, char c) {
    if (*n > 1) { **buf = c; (*buf)++; (*n)--; }
}

static void su64(char **buf, size_t *n, u64 v, int base, bool sgn) {
    char tmp[32]; int i = 0;
    if (sgn && (s64)v < 0) { sputc(buf, n, '-'); v = (u64)(-(s64)v); }
    do { int d = v % base; tmp[i++] = d < 10 ? '0' + d : 'a' + d - 10; v /= base; } while (v);
    while (i) sputc(buf, n, tmp[--i]);
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    char *b = buf; size_t n = size;
    for (; *fmt; fmt++) {
        if (*fmt != '%') { sputc(&b, &n, *fmt); continue; }
        fmt++;
        switch (*fmt) {
        case 's': { const char *s = va_arg(ap, const char *);
                    if (!s) s = "(null)";
                    while (*s) sputc(&b, &n, *s++); break; }
        case 'c': sputc(&b, &n, (char)va_arg(ap, int)); break;
        case 'd': su64(&b, &n, (s64)va_arg(ap, int), 10, true); break;
        case 'u': su64(&b, &n, (u64)va_arg(ap, u32), 10, false); break;
        case 'x': su64(&b, &n, (u64)va_arg(ap, u32), 16, false); break;
        case '%': sputc(&b, &n, '%'); break;
        default: sputc(&b, &n, *fmt); break;
        }
    }
    if (size) *b = 0;
    va_end(ap);
    return (int)(b - buf);
}

static char *strtok_save;
char *strtok(char *str, const char *delim) {
    if (!str) str = strtok_save;
    if (!str) return NULL;
    /* skip leading delims */
    while (*str && strchr(delim, *str)) str++;
    if (!*str) { strtok_save = NULL; return NULL; }
    char *start = str;
    while (*str && !strchr(delim, *str)) str++;
    if (*str) { *str = 0; strtok_save = str + 1; } else strtok_save = NULL;
    return start;
}
