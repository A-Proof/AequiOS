/* x86_64 — IDT (exceptions only; the machine is polled, no IRQs) +
 * syscall MSR setup + user-context helpers. */
#include <kernel.h>

struct idt_entry {
    u16 off_low;
    u16 sel;
    u8  ist;
    u8  flags;
    u16 off_mid;
    u32 off_high;
    u32 zero;
} __attribute__((packed));

static struct idt_entry idt[256];

struct idtr { u16 limit; u64 base; } __attribute__((packed));

extern void exc_0(void); extern void exc_1(void); extern void exc_2(void);
extern void exc_3(void); extern void exc_4(void); extern void exc_5(void);
extern void exc_6(void); extern void exc_7(void); extern void exc_8(void);
extern void exc_9(void); extern void exc_10(void); extern void exc_11(void);
extern void exc_12(void); extern void exc_13(void); extern void exc_14(void);
extern void exc_15(void); extern void exc_16(void); extern void exc_17(void);
extern void exc_18(void); extern void exc_19(void); extern void exc_20(void);
extern void exc_21(void); extern void exc_22(void); extern void exc_23(void);
extern void exc_24(void); extern void exc_25(void); extern void exc_26(void);
extern void exc_27(void); extern void exc_28(void); extern void exc_29(void);
extern void exc_30(void); extern void exc_31(void);

static void (*const exc_table[32])(void) = {
    exc_0, exc_1, exc_2, exc_3, exc_4, exc_5, exc_6, exc_7,
    exc_8, exc_9, exc_10, exc_11, exc_12, exc_13, exc_14, exc_15,
    exc_16, exc_17, exc_18, exc_19, exc_20, exc_21, exc_22, exc_23,
    exc_24, exc_25, exc_26, exc_27, exc_28, exc_29, exc_30, exc_31,
};

static void set_idt(int i, void *h) {
    u64 a = (u64)h;
    idt[i].off_low = a & 0xFFFF;
    idt[i].sel = 0x08;
    idt[i].ist = 0;
    idt[i].flags = 0x8E;               /* present, ring0, interrupt gate */
    idt[i].off_mid = (a >> 16) & 0xFFFF;
    idt[i].off_high = a >> 32;
    idt[i].zero = 0;
}

static const char *exc_names[32] = {
    "divide error", "debug", "nmi", "breakpoint", "overflow", "bound",
    "invalid opcode", "no fpu", "double fault", "coprocessor", "invalid tss",
    "segment missing", "stack fault", "general protection", "page fault",
    "reserved", "fpu error", "alignment", "machine check", "simd error",
    "virtualization", "control protection", "22", "23", "24", "25", "26",
    "27", "hypervisor", "vmm comm", "security", "31"
};

void exc_dump(u64 *frame) {
    u64 vec = frame[0];
    u64 aux = frame[1];
    u64 rip = frame[2], cs = frame[3], rflags = frame[4];
    early_printk("\n[EXC %llu %s] rip=%p cs=%#llx rflags=%#llx aux=%p\n",
                 (unsigned long long)vec,
                 vec < 32 ? exc_names[vec] : "?",
                 (void *)rip, (unsigned long long)cs,
                 (unsigned long long)rflags, (void *)aux);
    if (vec == 14)
        early_printk("       err=%#llx (P=%llx W=%llx U=%llx) cr2=%p\n",
                     (unsigned long long)aux,
                     (unsigned long long)(aux & 1),
                     (unsigned long long)((aux >> 1) & 1),
                     (unsigned long long)((aux >> 2) & 1),
                     (void *)aux);
    /* a fault in a ring-3 task kills the task, never the kernel —
     * same policy as aarch64's el0_exception_dump */
    if (cs == 0x2B) {
        struct task *p = task_exit_and_resume(139);
        if (p) {
            extern void return_to_user(void *);
            early_printk("[task] fault — killed, resuming parent\n");
            return_to_user(task_ctx(p));   /* never returns */
        }
    }
    panic("unhandled exception");
}

static void idt_init(void) {
    for (int i = 0; i < 32; i++) set_idt(i, exc_table[i]);
    struct idtr r = { sizeof(idt) - 1, (u64)&idt };
    __asm__ volatile ("lidt %0" :: "m"(r));
}

void gdt_init(void);
extern void aequi_syscall_entry(void);
extern char kernel_stack_top;

#define MSR_EFER   0xC0000080
#define MSR_STAR   0xC0000081
#define MSR_LSTAR  0xC0000082
#define MSR_SFMASK 0xC0000084

static u64 rdmsr(u32 m) {
    u32 lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(m));
    return ((u64)hi << 32) | lo;
}
static void wrmsr(u32 m, u64 v) {
    __asm__ volatile ("wrmsr" :: "a"((u32)v), "d"((u32)(v >> 32)), "c"(m));
}

void arch_init_syscalls(void) {
    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | 1);          /* SCE */
    wrmsr(MSR_STAR, ((u64)0x18 << 48) | ((u64)0x08 << 32));
    wrmsr(MSR_LSTAR, (u64)aequi_syscall_entry);
    wrmsr(MSR_SFMASK, 0x700);                    /* mask DF|TF|IF */
}

void arch_early_init(void) {
    gdt_init();
    idt_init();
    arch_init_syscalls();
    early_printk("gdt/idt/syscall installed\n");
}

void arch_late_init(void) { /* COM1 needs no MMIO mapping */ }

void arch_halt(void) {
    for (;;) __asm__ volatile ("cli; hlt");
}

void arch_reboot(void) {
    /* pulse the 8042 reset line */
    u8 v;
    do { __asm__ volatile ("inb $0x64, %0" : "=a"(v)); } while (v & 2);
    __asm__ volatile ("outb %0, $0x64" :: "a"((u8)0xFE));
    arch_halt();
}

/* ---- uctx (see entry.S for layout) ---- */
struct uctx {
    u64 r15, r14, r13, r12, rbp, rbx;
    u64 r9, r8, r10, rdx, rsi, rdi;
    u64 rax, rflags, rip, rsp;
};

u64 uctx_syscall_nr(void *ctx) { return ((struct uctx *)ctx)->rax; }
u64 uctx_arg(void *ctx, int i) {
    struct uctx *u = ctx;
    switch (i) {
    case 0: return u->rdi;
    case 1: return u->rsi;
    case 2: return u->rdx;
    case 3: return u->r10;
    case 4: return u->r8;
    default: return u->r9;
    }
}
void uctx_set_ret(void *ctx, u64 v) { ((struct uctx *)ctx)->rax = v; }
void uctx_set_ctx_frame(void *frame, const void *ctx) {
    memcpy(frame, ctx, sizeof(struct uctx));
}
void uctx_copy(void *dst, const void *src) {
    memcpy(dst, src, sizeof(struct uctx));
}
void arch_make_user_ctx(void *ctx, u64 entry, u64 ustack) {
    struct uctx *u = ctx;
    memset(u, 0, sizeof(*u));
    u->rip = entry;
    u->rsp = ustack;
    u->rflags = 0x202;
}
void arch_enter_user(struct task *t) {
    extern void return_to_user(void *);
    return_to_user(task_ctx(t));
    __builtin_unreachable();
}
