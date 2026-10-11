/* x86_64 userspace — _start and the native syscall instruction path. */
.text
.globl _start
_start:
    xor %rbp, %rbp
    call main
    mov %rax, %rdi
    mov $0xAE05, %rax        /* AEQ_SYS_exit */
    syscall
    hlt

/* C signature: aequi_callN(nr, a0, a1, a2) → rax=nr, rdi=a0, rsi=a1, rdx=a2 */
.globl aequi_call0
aequi_call0:
    mov %rdi, %rax
    syscall
    ret

.globl aequi_call1
aequi_call1:
    mov %rdi, %rax
    mov %rsi, %rdi
    syscall
    ret

.globl aequi_call2
aequi_call2:
    mov %rdi, %rax
    mov %rsi, %rdi
    mov %rdx, %rsi
    syscall
    ret

.globl aequi_call3
aequi_call3:
    mov %rdi, %rax
    mov %rsi, %rdi
    mov %rdx, %rsi
    mov %rcx, %rdx
    syscall
    ret

.section .note.GNU-stack,"",@progbits
