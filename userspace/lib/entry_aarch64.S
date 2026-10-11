/* aarch64 userspace — _start and the native SVC path. */
.text
.globl _start
_start:
    mov x29, xzr
    bl  main
    mov x8, #0xAE00 + 5       /* AEQ_SYS_exit */
    svc #0
    b   .

.globl aequi_call0
aequi_call0:
    mov x8, x0
    svc #0
    ret

.globl aequi_call1
aequi_call1:
    mov x8, x0
    mov x0, x1
    svc #0
    ret

.globl aequi_call2
aequi_call2:
    mov x8, x0
    mov x0, x1
    mov x1, x2
    svc #0
    ret

.globl aequi_call3
aequi_call3:
    mov x8, x0
    mov x0, x1
    mov x1, x2
    mov x2, x3
    svc #0
    ret

.section .note.GNU-stack,"",@progbits
