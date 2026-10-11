/* aarch64 — PL011 UART @ 0x09000000 (qemu -M virt).
 * The MMIO page is grafted into the kernel page table in arch_late_init —
 * device memory is not covered by Limine's HHDM on this platform.
 * Output before the bind is buffered and flushed once the UART is live. */
#include <kernel.h>

static volatile u32 *uart;

#define DR   0x00
#define FR   0x18
#define FR_RXFE (1 << 4)
#define FR_TXFF (1 << 5)

static char early_buf[8192];
static size_t early_len;

void serial_init(void) {
    uart = NULL;
    early_len = 0;
}

/* called by arch_late_init once the MMIO mapping exists */
void serial_bind_mmio(u64 va) {
    uart = (volatile u32 *)va;
    for (size_t i = 0; i < early_len; i++) {
        while (uart[FR / 4] & FR_TXFF) {}
        uart[DR / 4] = (u32)(u8)early_buf[i];
    }
    early_len = 0;
}

static bool ready(void) { return uart != NULL; }

void serial_putc(char c) {
    if (!ready()) {
        if (early_len < sizeof(early_buf) - 1) {
            if (c == '\n' && early_len < sizeof(early_buf) - 2)
                early_buf[early_len++] = '\r';
            early_buf[early_len++] = c;
        }
        return;
    }
    if (c == '\n') serial_putc('\r');
    while (uart[FR / 4] & FR_TXFF) {}
    uart[DR / 4] = (u32)c;
}

int serial_getc_poll(void) {
    if (!ready()) return -1;
    if (uart[FR / 4] & FR_RXFE) return -1;
    return (int)(uart[DR / 4] & 0xFF);
}
