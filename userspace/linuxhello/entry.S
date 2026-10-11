/* _start for the Linux-ABI demo binary (raw entry, no libc). */
.text
.globl _start
_start:
#ifdef __x86_64__
    xor %rbp, %rbp
    call main
    hlt
#elif defined(__aarch64__)
    mov x29, xzr
    bl  main
    b   .
#endif
.section .note.GNU-stack,"",@progbits
