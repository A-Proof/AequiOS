/* aarch64 — kernel entry, exception vectors, EL0 return stub. */
.text

.globl _start
_start:
    msr spsel, #1                 /* kernel runs on SP_EL1 */
    isb
    ldr x9, =kernel_stack_top
    ldr x9, [x9]
    mov sp, x9
    mov x29, xzr
    bl  kmain
    b   .

/* install the vector table */
.globl aequi_set_vbar
aequi_set_vbar:
    msr vbar_el1, x0
    isb
    ret

/* ---- exception vector table (must be 0x800 aligned) ---- */
.balign 0x800
.globl aequi_vectors
aequi_vectors:
    /* current EL, SP_EL0 — unused */
    .space 0x200
    /* current EL, SP_EL1 — kernel faults */
    b   kernel_sync               /* 0x200 sync */
    .space 0x7c
    b   kernel_sync               /* 0x280 irq  */
    .space 0x7c
    b   kernel_sync               /* 0x300 fiq  */
    .space 0x7c
    b   kernel_sync               /* 0x380 serr */
    .space 0x7c
    /* lower EL, AArch64 */
    b   el0_sync                  /* 0x400 sync — SVC */
    .space 0x7c
    b   el0_lower                 /* 0x480 irq  */
    .space 0x7c
    b   el0_lower                 /* 0x500 fiq  */
    .space 0x7c
    b   el0_lower                 /* 0x580 serr */
    .space 0x7c
    /* lower EL, AArch32 — unused */
    .space 0x200

kernel_sync:
    mrs x0, esr_el1
    mrs x1, far_el1
    mrs x2, elr_el1
    bl  kernel_exc_dump
    b   .

el0_lower:
    mrs x0, esr_el1
    mrs x1, far_el1
    mrs x2, elr_el1
    bl  kernel_exc_dump
    b   .

/* SVC / synchronous abort from EL0 — build the uctx frame on SP_EL1 */
el0_sync:
    sub sp, sp, #272
    stp x0,  x1,  [sp, #0]
    stp x2,  x3,  [sp, #16]
    stp x4,  x5,  [sp, #32]
    stp x6,  x7,  [sp, #48]
    stp x8,  x9,  [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30,      [sp, #240]
    mrs x9,  sp_el0
    str x9,  [sp, #248]
    mrs x10, elr_el1
    str x10, [sp, #256]
    mrs x11, spsr_el1
    str x11, [sp, #264]

    mrs x12, esr_el1
    ubfx x13, x12, #26, #6        /* exception class */
    cmp x13, #0x15                /* SVC from AArch64 */
    b.ne el0_abort

    mov x0, sp
    bl  sys_handle
    /* fall through — restore whatever frame is current */

.globl el0_ret
el0_ret:
    ldr x9,  [sp, #248]
    msr sp_el0, x9
    ldr x10, [sp, #256]
    msr elr_el1, x10
    ldr x11, [sp, #264]
    msr spsr_el1, x11
    ldp x0,  x1,  [sp, #0]
    ldp x2,  x3,  [sp, #16]
    ldp x4,  x5,  [sp, #32]
    ldp x6,  x7,  [sp, #48]
    ldp x8,  x9,  [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    ldp x18, x19, [sp, #144]
    ldp x20, x21, [sp, #160]
    ldp x22, x23, [sp, #176]
    ldp x24, x25, [sp, #192]
    ldp x26, x27, [sp, #208]
    ldp x28, x29, [sp, #224]
    ldr x30,      [sp, #240]
    add sp, sp, #272
    eret

/* non-SVC exception from EL0 (e.g. user page fault): dump and kill task */
el0_abort:
    mov x0, sp
    bl  el0_exception_dump        /* never returns (task killed or panic) */
    b   .

/* return_to_user(frame): load a saved ctx and eret — also first entry.
 * The ctx lives inside a heap task struct, so it cannot double as the
 * kernel stack: copy it onto the real kernel stack first, otherwise the
 * next exception's C call chain would grow into the heap and corrupt it. */
.globl return_to_user
return_to_user:
    ldr x9, =kernel_stack_top
    ldr x9, [x9]
    sub x9, x9, #272
    mov sp, x9                    /* frame base on kernel stack */
    mov x10, #17                  /* 272 bytes = 17 stp pairs */
1:  ldp x11, x12, [x0], #16
    stp x11, x12, [x9], #16
    subs x10, x10, #1
    b.ne 1b
    b   el0_ret

.section .note.GNU-stack,"",@progbits
