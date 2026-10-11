# Linux ABI compatibility layer (v0)

`kernel/sys/linux.c` implements the Linux syscall ABI on top of Ordo
task services. Dispatch rule: any syscall number `< 0xAE00` is treated
as a Linux call; `≥ 0xAE00` is a native AequiOS call (`syscalls.h`).

## What runs today

`userspace/linuxhello` — a static freestanding binary that issues raw
Linux syscall numbers — works on **both** x86_64 and aarch64:

`uname`, `openat(AT_FDCWD)` + `read` + `close` on `/etc/os-release`,
`brk`, `mmap(NULL, …, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON)`,
`writev`, `write`, `exit_group`.

## Implemented calls

| x86_64 nr | aarch64 nr | Linux call       | Implementation                    |
|-----------|------------|------------------|-----------------------------------|
| 0         | 63         | read             | task fd → vfs / serial            |
| 1         | 64         | write            | fd 1/2 → console, else vfs-ro     |
| 2         | —          | open             | path → vfs → task fd              |
| 3         | 57         | close            |                                   |
| 9         | 222        | mmap             | anon only; honors `MAP_FIXED`     |
| 12        | 214        | brk              | grow-only                         |
| 20        | 66         | writev           |                                   |
| 63        | 160        | uname            | "AequiOS", machine = arch         |
| 158       | —          | arch_prctl       | ARCH_SET_FS/GS → pretend-ok (0)   |
| 218       | 96         | set_tid_address  | → 1                               |
| 231       | 94         | exit_group       | → `sys_do_exit`                   |
| 257       | 56         | openat           | AT_FDCWD paths → open             |
| —         | 29         | ioctl            | → -ENOTTY                         |

Everything else → `-ENOSYS` (38). Musl/dietlibc-style startup stubs need
the calls above plus `arch_prctl`/`set_tid_address` to reach `main`.

## Conventions

* **User pointers** are dereferenced in-place: the kernel touches the
  user VA directly while the task's root is active (user pages are
  readable/writable from EL1/ring 0 under our AP/user-bit setup).
  No `copy_from_user` validation yet — a bad pointer kills the task via
  the fault path, which is acceptable for v0 but must be fixed before
  untrusted binaries.
* **`brk(0)`** returns the current break; growing maps zeroed pages.
* **`mmap`** is anonymous-only and bumps `task->mmap_next` from
  `0x40000000`; `MAP_FIXED` maps exactly where asked.
* **`open`/`openat`** are read-only against the initramfs. `O_WRONLY`,
  `O_CREAT`, `O_TRUNC` etc. are ignored — flagged in ROADMAP.

## Gap analysis → real Linux binaries

To run a **real** dynamically-linked Linux binary (e.g. BusyBox) the
layer still needs, roughly in order:

1. `read` on stdin fd → serial (fd wiring exists; buffers exist)
2. `fstat`/`statx`/`newfstatat`, `lseek`, `getdents64`, `access`,
   `readlink` — filesystem introspection
3. `fcntl`, `dup`/`dup2`, `pipe`, `poll`/`ppoll`/`select`
4. `mmap` file-backed + `munmap`/`mprotect` — needed by `ld-linux` and
   any real allocator (musl `malloc` mostly uses `brk`, glibc needs mmap)
5. `clone`/`fork`/`execve`/`wait4` — real process model (today spawn is
   blocking exec+wait)
6. signals: `rt_sigaction`/`rt_sigprocmask`/`kill`/`sigreturn`
7. `getpid`/`gettid`/`getuid`, `nanosleep`/`clock_gettime`,
   `prlimit64`, `getrandom`, `rseq`, `set_robust_list`
8. sockets + ioctls → the network stack milestone

Busiest first: a statically-linked **musl** `busybox` needs roughly the
calls in groups 1–5 + the trivial stubs in 7. That is the milestone
"BusyBox `sh`/`ls`/`cat` on AequiOS" in ROADMAP.md.
