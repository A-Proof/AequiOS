/* x86_64 — COM1 (0x3F8) polled UART. */
#include <kernel.h>

#define COM1 0x3F8

static inline void outb(u16 port, u8 v) {
    __asm__ volatile ("outb %0, %1" :: "a"(v), "Nd"(port));
}
static inline u8 inb(u16 port) {
    u8 v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

void serial_init(void) {
    outb(COM1 + 1, 0x00);   /* no IRQs */
    outb(COM1 + 3, 0x80);   /* DLAB */
    outb(COM1 + 0, 0x03);   /* 38400 baud */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);   /* 8N1 */
    outb(COM1 + 2, 0xC7);   /* FIFO */
    outb(COM1 + 4, 0x0B);   /* RTS+DTR */
}

void serial_putc(char c) {
    if (c == '\n') serial_putc('\r');
    while (!(inb(COM1 + 5) & 0x20)) {}
    outb(COM1, (u8)c);
}

int serial_getc_poll(void) {
    if (!(inb(COM1 + 5) & 0x01)) return -1;
    return inb(COM1);
}
