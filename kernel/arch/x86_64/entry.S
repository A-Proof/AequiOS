/* x86_64 — kernel entry, syscall entry, user-return stub, exception stubs. */
.text

/* ---- Limine entry point ---- */
.globl _start
_start:
    cli
    movabsq $kernel_stack_top, %rax
    movq (%rax), %rsp
    xorq %rbp, %rbp
    call kmain
    hlt

/* ---- syscall entry ----
 * On entry: rcx = user rip, r11 = user rflags, rsp = user rsp.
 * We switch to the kernel stack, push a full uctx frame, call the
 * dispatcher, then restore whatever frame is on top (it may have been
 * swapped to another task). */
.globl aequi_syscall_entry
aequi_syscall_entry:
    movq %rax, saved_user_rax(%rip)   /* syscall nr / future ret slot */
    movq %rsp, saved_user_rsp(%rip)
    movabsq $kernel_stack_top, %rax
    movq (%rax), %rsp
    /* build uctx: highest field pushed first */
    pushq saved_user_rsp(%rip)   /* +0x78 rsp */
    pushq %rcx                   /* +0x70 rip */
    pushq %r11                   /* +0x68 rflags */
    pushq saved_user_rax(%rip)   /* +0x60 rax */
    pushq %rdi                   /* +0x58 */
    pushq %rsi                   /* +0x50 */
    pushq %rdx                   /* +0x48 */
    pushq %r10                   /* +0x40 */
    pushq %r8                    /* +0x38 */
    pushq %r9                    /* +0x30 */
    pushq %rbx                   /* +0x28 */
    pushq %rbp                   /* +0x20 */
    pushq %r12                   /* +0x18 */
    pushq %r13                   /* +0x10 */
    pushq %r14                   /* +0x08 */
    pushq %r15                   /* +0x00 */
    movq %rsp, %rdi
    call sys_handle
    movq %rsp, %rdi
    /* fall through to user-return */

/* return_to_user(frame): pops a uctx frame and sysrets.
 * Also the path for entering a task the first time (kmain builds the
 * frame in task->ctx and calls us with it). */
.globl return_to_user
return_to_user:
    movq %rdi, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    popq %r9
    popq %r8
    popq %r10
    popq %rdx
    popq %rsi
    popq %rdi
    popq %rax
    popq %r11                    /* rflags */
    popq %rcx                    /* user rip */
    popq %rsp                    /* user rsp */
    sysretq

/* ---- exception stubs ----
 * Common stub: vector number pushed by per-vector macro, then jump to
 * the dumper which prints registers and halts. */
.macro EXC n
    .globl exc_\n
    exc_\n:
    pushq $\n
    jmp exc_common
.endm

EXC 0
EXC 1
EXC 2
EXC 3
EXC 4
EXC 5
EXC 6
EXC 7
/* 8 pushes its own error code — handled separately below */
EXC 9
EXC 10
EXC 11
EXC 12
EXC 13
/* 14 handled separately */
EXC 15
EXC 16
EXC 17
EXC 18
EXC 19
EXC 20
EXC 21
EXC 22
EXC 23
EXC 24
EXC 25
EXC 26
EXC 27
EXC 28
EXC 29
EXC 30
EXC 31

/* page fault: CPU pushed an error code; grab cr2 into rsi slot */
.globl exc_14
exc_14:
    movq %cr2, %rax
    pushq %rax            /* pseudo-vector arg: cr2 */
    pushq $14
    jmp exc_common

/* double fault */
.globl exc_8
exc_8:
    pushq $0              /* fake err field */
    pushq $8
    jmp exc_common

exc_common:
    /* stack: [vector][arg/err][rip][cs][rflags]([old rsp][old ss]) */
    movq %rsp, %rdi
    call exc_dump
    hlt

.data
saved_user_rsp:
    .quad 0
saved_user_rax:
    .quad 0

.section .note.GNU-stack,"",@progbits
