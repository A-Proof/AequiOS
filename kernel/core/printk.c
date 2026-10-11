#include <kernel.h>

static bool fb_console_ready;

void printk_console_ready(void) { fb_console_ready = true; }

static void emit_char(char c, bool to_fb) {
    serial_putc(c);
    if (to_fb && fb_console_ready) fb_putc(c);
}

static void emit_str(const char *s, bool to_fb) {
    while (*s) emit_char(*s++, to_fb);
}

static void emit_u64(u64 v, int base, bool sgn, bool to_fb) {
    char buf[32];
    int i = 0;
    if (sgn && (s64)v < 0) { emit_char('-', to_fb); v = (u64)(-(s64)v); }
    do {
        int d = v % base;
        buf[i++] = d < 10 ? '0' + d : 'a' + d - 10;
        v /= base;
    } while (v);
    while (i) emit_char(buf[--i], to_fb);
}

static void vprintk_to(const char *fmt, va_list ap, bool to_fb) {
    for (; *fmt; fmt++) {
        if (*fmt != '%') { emit_char(*fmt, to_fb); continue; }
        fmt++;
        char spec = *fmt;
        if (spec == 'l' && fmt[1] == 'l') { fmt++; spec = 'L'; fmt++; }
        else if (spec == 'l') { fmt++; spec = 'L'; }
        switch (spec == 'L' ? *fmt : spec) {
        case 's': { const char *s = va_arg(ap, const char *); emit_str(s ? s : "(null)", to_fb); break; }
        case 'c': emit_char((char)va_arg(ap, int), to_fb); break;
        case 'd': case 'i': {
            s64 v = (spec == 'L') ? va_arg(ap, s64) : va_arg(ap, s32);
            emit_u64((u64)v, 10, true, to_fb); break; }
        case 'u': {
            u64 v = (spec == 'L') ? va_arg(ap, u64) : va_arg(ap, u32);
            emit_u64(v, 10, false, to_fb); break; }
        case 'x': case 'p': {
            u64 v = (spec == 'L') ? va_arg(ap, u64) : va_arg(ap, u32);
            if (spec == 'p') emit_str("0x", to_fb);
            emit_u64(v, 16, false, to_fb); break; }
        case '%': emit_char('%', to_fb); break;
        default: emit_char('%', to_fb); emit_char(spec, to_fb); break;
        }
    }
}

void printk(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vprintk_to(fmt, ap, true);
    va_end(ap);
}

void early_printk(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vprintk_to(fmt, ap, false);
    va_end(ap);
}

_Noreturn void panic(const char *fmt, ...) {
    early_printk("\n[ORDO PANIC] ");
    va_list ap; va_start(ap, fmt);
    vprintk_to(fmt, ap, false);
    va_end(ap);
    early_printk("\n");
    arch_halt();
    for (;;) {}
}
