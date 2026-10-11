# AequiOS Architecture

> Even in Architecture, Design, and Spirit — how the system fits together.

AequiOS is a from-scratch, hybrid kernel: our own Ordo kernel owns
memory, tasks, files and devices, and a **Linux personality** translates
native binaries so statically-linked Linux programs run on it unchanged.

```
┌─────────────────────────────────────────────────────────┐
│ userspace                                               │
│   ratio (Serenus shell)   linux-hello   your apps       │
│   native AEQ syscalls     |   Linux syscalls            │
├───────────────────────────┴─────────────────────────────┤
│ Ordo kernel                                             │
│   dispatch: nr < 0xAE00 → linux.c   nr ≥ 0xAE00 → native│
│   tasks · ELF loader · vfs (LumenFS tar) · Aura fb      │
├─────────────────────────────────────────────────────────┤
│ arch layer:  x86_64 (GDT/IDT/syscall)   aarch64 (SVC)   │
├─────────────────────────────────────────────────────────┤
│ Limine bootloader (Initium)  →  PMM/VMM/heap            │
└─────────────────────────────────────────────────────────┘
```

## Boot flow (Initium → Ordo)

1. **Limine** (v12, base revision 6) loads `aequios.elf` at
   `0xFFFFFFFF80000000` plus `initramfs.tar` as a module, and hands us
   the HHDM offset, memory map and a linear framebuffer.
2. `_start` switches to the kernel stack, `kmain` runs:
   serial → hhdm → `vmm_init` → `pmm_init` → `kmalloc_init` →
   `arch_early_init` → `arch_late_init` → `fb_init`/`fb_splash` →
   `vfs_init` → `task_spawn("/bin/ratio")` → `task_run`.
3. Nothing returns from `task_run`: the first `eret`/`sysret` drops to
   ring 3 / EL0.

## Memory management

* **PMM** (`kernel/mm/pmm.c`): bitmap allocator over Limine's usable
  memmap; contiguous-run allocation for big blocks.
* **VMM** (`kernel/arch/*/vmm.c`): 4-level page tables per address space.
  * x86_64: user root PML4 = private table with the kernel half
    (`PML4[256..511]`) copied, so syscalls never switch CR3.
  * aarch64: kernel on TTBR1 (Limine's tables), each task gets a fresh
    TTBR0. MAIR indices are discovered dynamically (device attr vs
    normal). The PL011 UART page is grafted into the kernel map at
    `arch_late_init` (it is *not* covered by the HHDM on `virt`).
* **kmalloc** (`kernel/mm/kmalloc.c`): slab classes 16–2048 B in 4 KiB
  pages (`slabhdr`), larger requests get `pages+1` contiguous pages with
  a `bigalloc` header. `kfree` discriminates by page alignment + magic.

## Tasks (Vita)

`struct task` = address-space root + saved register context (`ctx`) +
brk/mmap state + 16 fd slots + parent link.

There is **no scheduler yet** — a cooperative context-stack model:

* `task_spawn` loads an ELF from the initramfs, builds a user stack and
  an initial `uctx` (`arch_make_user_ctx`).
* `sys_spawn` parks the parent's live register frame into `parent->ctx`,
  copies the child's `ctx` over the CPU frame, activates the child
  address space — and the syscall return *is* the child's first entry.
* `exit`/`exit_group` restores the parked parent frame (with the exit
  code as its `spawn` return value) and reactivates the parent's root.
* A non-SVC fault in a task (page fault, bad instruction) kills the task
  the same way — the parent survives.

`exit` currently leaks the child task struct and its pages (v0).

## Syscall interface — two personalities

Native AequiOS calls live at `AEQ_SYS_BASE = 0xAE00`:

| nr         | name     | notes                                   |
|------------|----------|-----------------------------------------|
| 0xAE00     | write    | fd 1 = console (fb + serial mirror)     |
| 0xAE01     | read     | fd 0 = serial input                     |
| 0xAE02     | open     | path in initramfs → fd                  |
| 0xAE03     | close    |                                         |
| 0xAE05     | spawn    | exec+wait in one call → exit code       |
| 0xAE06     | exit     |                                         |
| 0xAE07     | meminfo  | free KiB                                |
| 0xAE08     | brk      | grow program break                      |
| 0xAE09     | mmap     | anonymous pages at/above 0x40000000     |
| 0xAE0A     | ls       | print initramfs listing                 |
| 0xAE0B     | reboot   |                                         |
| 0xAE0C     | uname    | "AequiOS/Ordo 0.1"                      |

Any number below `0xAE00` is dispatched to the **Linux personality**
(`kernel/sys/linux.c`), which uses a per-architecture table (x86_64 and
aarch64 have different numbering — e.g. `write` is 1 vs 64). See
`docs/LINUX-ABI.md` for the implemented set and the translation rules
(user-pointer handling, `MAP_FIXED`, `AT_FDCWD`, etc.).

## Userspace

* `userspace/lib` — freestanding mini-libc: `entry_*.S` `_start` stubs,
  `ulib.c` (`uprintf`, strings), `aequi_call0..3` syscall stubs.
* `userspace/ratio` — **Ratio**, the Serenus shell. REPL over serial:
  `help`, `uname`, `meminfo`, `ls`, `cat`, `run`, `demo`, `reboot`.
  Boots into a boxed banner and auto-runs the Linux demo once (used by
  `tools/vmtest.py` for CI-style checks).
* `userspace/linuxhello` — demo that calls **Linux** syscall numbers
  directly (`svc`/`syscall` on the raw numbers): `uname`, `openat` /
  `read` of `/etc/os-release`, `brk`, `mmap` (verifies the mapping),
  `writev`, `exit_group(42)`. Proves the ABI both arches.

## File system (LumenFS v0)

`kernel/fs/vfs.c` parses the Limine module as a **ustar** archive
(read-only, built deterministically by `tools/mkinitramfs.py`). File
descriptors are per-task indexes into the VFS table; console I/O is fd
sentinels (-2 = out, -3 = serial in).

## Display (Aura v0)

`drivers/framebuffer.c` draws an 8×16 console font (converted from
`Lat15-Terminus16.psf` by `tools/mkfont.py`) onto the Limine
framebuffer, on both arches (QEMU `ramfb` on ARM). Boot splash:
centered wordmark + tagline + accent rule, then a scrolling console
region underneath.

## Testing

`make test` → `tools/vmtest.py <arch>` boots QEMU headless, captures
serial output, feeds shell commands, and asserts on 14 markers —
including `exited with code 42` from the Linux binary. Both arches pass.

## Layout

```
boot/limine.conf          kernel entry + initramfs module for Limine
kernel/
  core/    _start glue, Limine requests, printk, kstring, stacks
  mm/      pmm (bitmap), kmalloc (slab+contig)
  proc/    task, ELF loader
  sys/     syscall dispatch, linux.c compat layer
  arch/x86_64/   entry.S, gdt, idt+syscall MSRs, serial, vmm
  arch/aarch64/  entry.S+vectors, machine.c, serial(PL011), vmm
  include/ kernel.h, syscalls.h, elf.h, limine.h (vendored)
drivers/framebuffer.c     Aura console + splash
userspace/  include/ lib/ ratio/ linuxhello/
tools/      mkinitramfs, mkfont, vmtest, fetch_limine
docs/       this file, ROADMAP.md, LINUX-ABI.md, HANDOFF.md
```
