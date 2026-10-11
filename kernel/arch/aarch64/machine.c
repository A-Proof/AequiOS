/* aarch64 — machine glue: vectors, EL0 ctx helpers, MMIO graft, PSCI. */
#include <kernel.h>

extern char aequi_vectors[];
extern void aequi_set_vbar(u64 base);
extern void return_to_user(void *frame);
extern u64 vmm_kernel_root(void);
void serial_bind_mmio(u64 va);

#define UART_PA 0x09000000ULL

void arch_early_init(void) {
    aequi_set_vbar((u64)aequi_vectors);
    early_printk("vectors installed\n");
}

void arch_late_init(void) {
    /* graft the PL011 page into the kernel map (device memory is outside
     * Limine's HHDM coverage on virt) */
    u64 va = UART_PA + hhdm_offset;
    vmm_map(vmm_kernel_root(), va, UART_PA, VMM_DEVICE);
    serial_bind_mmio(va);
    early_printk("uart mapped at %p\n", (void *)va);
}

void arch_halt(void) {
    for (;;) __asm__ volatile ("wfi");
}

void arch_reboot(void) {
    /* PSCI SYSTEM_RESET via hvc (qemu virt exposes PSCI at EL2) */
    register u64 x0 __asm__("x0") = 0x84000009ULL;
    __asm__ volatile ("hvc #0" :: "r"(x0) : "memory");
    arch_halt();
}

void kernel_exc_dump(u64 esr, u64 far, u64 elr) {
    early_printk("\n[EXC] esr=%p far=%p elr=%p\n",
                 (void *)esr, (void *)far, (void *)elr);
    panic("kernel exception");
}

/* EL0 fault path (non-SVC sync exception): report and kill the task. */
void el0_exception_dump(void *frame) {
    u64 esr, far, elr;
    __asm__ volatile ("mrs %0, esr_el1" : "=r"(esr));
    __asm__ volatile ("mrs %0, far_el1" : "=r"(far));
    __asm__ volatile ("mrs %0, elr_el1" : "=r"(elr));
    early_printk("\n[task] fault: esr=%p far=%p elr=%p — killing task\n",
                 (void *)esr, (void *)far, (void *)elr);
    sys_do_exit(frame, 139);
}

/* ---- uctx layout (see entry.S): x0..x30, sp_el0, elr, spsr ---- */
struct uctx {
    u64 x[31];
    u64 sp_el0;
    u64 elr;
    u64 spsr;
};

u64 uctx_syscall_nr(void *ctx) { return ((struct uctx *)ctx)->x[8]; }
u64 uctx_arg(void *ctx, int i) {
    return i >= 0 && i <= 5 ? ((struct uctx *)ctx)->x[i] : 0;
}
void uctx_set_ret(void *ctx, u64 v) { ((struct uctx *)ctx)->x[0] = v; }
void uctx_set_ctx_frame(void *frame, const void *ctx) {
    memcpy(frame, ctx, sizeof(struct uctx));
}
void uctx_copy(void *dst, const void *src) {
    memcpy(dst, src, sizeof(struct uctx));
}
void arch_make_user_ctx(void *ctx, u64 entry, u64 ustack) {
    struct uctx *u = ctx;
    memset(u, 0, sizeof(*u));
    u->elr = entry;
    u->sp_el0 = ustack;
    u->spsr = 0;                /* EL0t, all exceptions unmasked */
}
void arch_enter_user(struct task *t) {
    return_to_user(task_ctx(t));
    __builtin_unreachable();
}
