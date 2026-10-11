/* AequiOS native syscall numbers — based at 0xAE00 so they never collide
 * with the Linux ABI range (the compat layer claims everything below). */
#ifndef AEQ_SYSCALLS_H
#define AEQ_SYSCALLS_H

#define AEQ_SYS_BASE     0xAE00

#define AEQ_SYS_write    (AEQ_SYS_BASE + 0)
#define AEQ_SYS_read     (AEQ_SYS_BASE + 1)
#define AEQ_SYS_open     (AEQ_SYS_BASE + 2)
#define AEQ_SYS_close    (AEQ_SYS_BASE + 3)
#define AEQ_SYS_spawn    (AEQ_SYS_BASE + 4)
#define AEQ_SYS_exit     (AEQ_SYS_BASE + 5)
#define AEQ_SYS_meminfo  (AEQ_SYS_BASE + 6)
#define AEQ_SYS_brk      (AEQ_SYS_BASE + 7)
#define AEQ_SYS_mmap     (AEQ_SYS_BASE + 8)
#define AEQ_SYS_ls       (AEQ_SYS_BASE + 9)
#define AEQ_SYS_reboot   (AEQ_SYS_BASE + 10)
#define AEQ_SYS_uname    (AEQ_SYS_BASE + 11)

#endif
