/* input — polled PS/2 (i8042) keyboard + mouse.
 * x86_64: real i8042 ports. aarch64: stubs (no PS/2 on QEMU virt —
 * virtio-input is a roadmap item; serial remains the input path there).
 *
 * No interrupts yet: the keyboard byte stream and mouse packets are
 * drained whenever userspace blocks in input_wait_char(), so typing
 * works while the shell waits for a line, and mouse motion repaints
 * the cursor the same way. */
#include <kernel.h>

#if defined(__x86_64__)

#define PORT_DATA   0x60
#define PORT_STATUS 0x64
#define PORT_CMD    0x64

#define ST_OUT_FULL 0x01
#define ST_AUX      0x20

static inline u8 inb(u16 p) {
    u8 v; __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(p)); return v;
}
static inline void outb(u16 p, u8 v) {
    __asm__ volatile ("outb %0, %1" :: "a"(v), "Nd"(p));
}

/* ---- keyboard: scancode set 1 (US layout) ---- */

static char kbd_buf[128];
static int kbd_head, kbd_tail;
static bool shift_l, shift_r;

static void kbd_put(char c) {
    int next = (kbd_tail + 1) % (int)sizeof(kbd_buf);
    if (next != kbd_head) { kbd_buf[kbd_tail] = c; kbd_tail = next; }
}

static const char sc_map[0x60] = {
    [0x02]='1',[0x03]='2',[0x04]='3',[0x05]='4',[0x06]='5',[0x07]='6',
    [0x08]='7',[0x09]='8',[0x0A]='9',[0x0B]='0',[0x0C]='-',[0x0D]='=',
    [0x0E]='\b',[0x0F]='\t',
    [0x10]='q',[0x11]='w',[0x12]='e',[0x13]='r',[0x14]='t',[0x15]='y',
    [0x16]='u',[0x17]='i',[0x18]='o',[0x19]='p',[0x1A]='[',[0x1B]=']',
    [0x1C]='\n',[0x1E]='a',[0x1F]='s',[0x20]='d',[0x21]='f',[0x22]='g',
    [0x23]='h',[0x24]='j',[0x25]='k',[0x26]='l',[0x27]=';',[0x28]='\'',
    [0x29]='`',[0x2B]='\\',
    [0x2C]='z',[0x2D]='x',[0x2E]='c',[0x2F]='v',[0x30]='b',[0x31]='n',
    [0x32]='m',[0x33]=',',[0x34]='.',[0x35]='/',[0x39]=' ',
};

static const char sc_shift[0x60] = {
    [0x02]='!',[0x03]='@',[0x04]='#',[0x05]='$',[0x06]='%',[0x07]='^',
    [0x08]='&',[0x09]='*',[0x0A]='(',[0x0B]=')',[0x0C]='_',[0x0D]='+',
    [0x0E]='\b',[0x0F]='\t',
    [0x10]='Q',[0x11]='W',[0x12]='E',[0x13]='R',[0x14]='T',[0x15]='Y',
    [0x16]='U',[0x17]='I',[0x18]='O',[0x19]='P',[0x1A]='{',[0x1B]='}',
    [0x1C]='\n',[0x1E]='A',[0x1F]='S',[0x20]='D',[0x21]='F',[0x22]='G',
    [0x23]='H',[0x24]='J',[0x25]='K',[0x26]='L',[0x27]=':',[0x28]='"',
    [0x29]='~',[0x2B]='|',
    [0x2C]='Z',[0x2D]='X',[0x2E]='C',[0x2F]='V',[0x30]='B',[0x31]='N',
    [0x32]='M',[0x33]='<',[0x34]='>',[0x35]='?',[0x39]=' ',
};

static void kbd_byte(u8 sc) {
    bool release = sc & 0x80;
    u8 code = sc & 0x7F;
    if (code == 0x2A) { shift_l = !release; return; }
    if (code == 0x36) { shift_r = !release; return; }
    if (release) return;
    if (code == 0x1D || code == 0x38) return;   /* ctrl / alt, unused */
    char c = (shift_l || shift_r) ? sc_shift[code] : sc_map[code];
    if (c) kbd_put(c);
}

/* ---- mouse: 3-byte PS/2 packets ---- */

static u8 mpkt[3];
static int mpkt_len;
static s32 mouse_dx, mouse_dy;
static u8 mouse_btn;
static bool mouse_new;

static void mouse_byte(u8 b) {
    if (mpkt_len == 0 && !(b & 0x08)) return;   /* resync on bit3 */
    mpkt[mpkt_len++] = b;
    if (mpkt_len < 3) return;
    mpkt_len = 0;
    s32 dx = mpkt[1], dy = mpkt[2];
    if (mpkt[0] & 0x10) dx -= 256;              /* x sign */
    if (mpkt[0] & 0x20) dy -= 256;              /* y sign */
    mouse_dx = dx; mouse_dy = -dy;              /* PS/2 y grows up */
    mouse_btn = mpkt[0] & 0x07;
    mouse_new = true;
}

static void i8042_drain(void) {
    for (int i = 0; i < 64; i++) {
        u8 st = inb(PORT_STATUS);
        if (!(st & ST_OUT_FULL)) break;
        u8 b = inb(PORT_DATA);
        if (st & ST_AUX) mouse_byte(b);
        else kbd_byte(b);
    }
}

static bool i8042_wait_out(void) {
    for (int i = 0; i < 100000; i++)
        if (inb(PORT_STATUS) & ST_OUT_FULL) return true;
    return false;
}

static bool i8042_wait_in(void) {
    for (int i = 0; i < 100000; i++)
        if (!(inb(PORT_STATUS) & 0x02)) return true;
    return false;
}

void input_init(void) {
    /* flush stale bytes */
    while (inb(PORT_STATUS) & ST_OUT_FULL) (void)inb(PORT_DATA);

    /* the emulated kbd speaks scancode set 2; enable the i8042's
     * set-2→set-1 translation bit so the OS sees set-1 codes */
    if (i8042_wait_in()) {
        outb(PORT_CMD, 0x20);                 /* read command byte */
        if (i8042_wait_out()) {
            u8 cb = inb(PORT_DATA) | 0x40;    /* translation enable */
            if (i8042_wait_in()) {
                outb(PORT_CMD, 0x60);         /* write command byte */
                if (i8042_wait_in()) outb(PORT_DATA, cb);
            }
        }
    }

    /* enable the aux (mouse) port, then enable data reporting */
    if (i8042_wait_in()) outb(PORT_CMD, 0xA8);
    if (i8042_wait_in()) {
        outb(PORT_CMD, 0xD4);
        if (i8042_wait_in()) {
            outb(PORT_DATA, 0xF4);              /* enable reporting */
            if (i8042_wait_out()) (void)inb(PORT_DATA); /* ACK */
        }
    }
    early_printk("input: i8042 polled keyboard+mouse\n");
}

int input_getc_poll(void) {
    i8042_drain();
    if (kbd_head == kbd_tail) return -1;
    char c = kbd_buf[kbd_head];
    kbd_head = (kbd_head + 1) % (int)sizeof(kbd_buf);
    return (unsigned char)c;
}

bool input_poll_mouse(s32 *dx, s32 *dy, u8 *btn) {
    i8042_drain();
    if (!mouse_new) return false;
    *dx = mouse_dx; *dy = mouse_dy; *btn = mouse_btn;
    mouse_new = false;
    return true;
}

#else  /* !__x86_64__ — no i8042; stubs keep the desktop drawing */

void input_init(void) { early_printk("input: none (serial only)\n"); }
int  input_getc_poll(void) { return -1; }
bool input_poll_mouse(s32 *dx, s32 *dy, u8 *btn) {
    (void)dx; (void)dy; (void)btn; return false;
}

#endif

/* shared wait loop: keyboard + serial both feed the shell (the serial
 * path doubles as the headless "server" interface), mouse motion is
 * painted while we wait. */
int input_wait_char(void) {
    for (;;) {
        int c = input_getc_poll();
        if (c >= 0) return c;
        c = serial_getc_poll();
        if (c >= 0) return c;
        desktop_poll_mouse();
    }
}
