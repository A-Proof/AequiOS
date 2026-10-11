# AequiOS Handoff

**State (2026-10-11): v0.1 — kernel boots x86_64 + aarch64 in QEMU,
runs a ring-3 shell, and runs a Linux-ABI demo binary on both.**

## Build & run

```bash
cd ~/repos/AequiOS
make image ARCH=x86_64          # needs: clang lld xorriso mtools
make run   ARCH=x86_64          # qemu-system-x86_64, serial console
make image ARCH=aarch64         # raw EFI/ESP image
make run   ARCH=aarch64         # qemu-system-aarch64 -bios QEMU_EFI.fd
make test  ARCH=x86_64          # tools/vmtest.py — 14 markers, PASS
make test  ARCH=aarch64
./build.sh x86_64 test          # wrapper
```

Deps: `clang lld qemu-system-x86 qemu-system-arm ovmf qemu-efi-aarch64
xorriso mtools nasm python3` + `console-setup` (PSF font). Limine v12.9.3
is built from `tools/fetch_limine.sh` into `third_party/limine` (or copy
binaries from an existing build — only 6 files are used:
`limine`, `limine-bios.sys`, `limine-bios-cd.bin`, `limine-uefi-cd.bin`,
`BOOTX64.EFI`, `BOOTAA64.EFI`).

## What works

* Both arches: Limine → kernel → PMM/VMM/heap → Aura splash →
  initramfs VFS → ring-3 **Ratio** shell.
* Ratio REPL: `help uname meminfo ls cat run demo reboot`.
* Linux ABI: `/bin/linux-hello` (raw Linux syscall numbers) passes —
  `uname`, `openat/read /etc/os-release`, `brk`, `mmap` verify,
  `writev`, `exit_group(42)` → shell reports exit code.
* `tools/vmtest.py` asserts 14 serial markers per arch — currently PASS.

## Known limitations (v0)

* No scheduler — tasks are cooperative spawn→wait (parent parks its
  register frame while child runs). No `fork`, no `exec` without wait.
* `exit` leaks the child task + its pages.
* VFS is read-only ustar. `open` ignores flags beyond read.
* User pointers are dereferenced unchecked — a bad pointer kills the
  task (fault→exit 139) but must be validated before untrusted code.
* No interrupts wired (no timer, no keyboard input — shell reads serial
  via polling `getc_poll`). Framebuffer is display-only.
* Kernel `printk` goes to serial + fb; fb console renders only ASCII
  (8×16 font) — keep printed strings to ASCII.

## Gotchas that will bite you

* **aarch64 `return_to_user` must not run the kernel on the ctx
  buffer.** Copy the ctx onto the kernel stack before `eret` — the bug
  this fixed corrupted the heap (EL1 instruction abort at PC=0).
* Limine base revision is **6** (`LIMINE_BASE_REVISION(6)` in
  `kernel/core/requests.c`); the check lives in `main.c` reading the
  *request* variable, not a local copy.
* On `virt` the PL011 (0x09000000) is NOT in the HHDM — graft it into
  the kernel map (`arch_late_init`) before first serial use; early
  prints buffer until `serial_bind_mmio`.
* aarch64 user pages need UXN cleared for exec and AF set on first
  map; MAIR index is discovered from Limine's MAIR (device idx vs
  normal idx differ across QEMU versions).
* x86 syscall entry: `aequi_syscall_entry` must save `%rax` before
  computing the stack switch (it is the syscall number).
* The aarch64 image is a raw FAT ESP: `dd` → `mformat` → `mcopy`
  BOOTAA64.EFI + boot files (see Makefile `image` target).

## Where to start next

1. **M1 process model** (`docs/ROADMAP.md`): timer interrupt +
   scheduler + `clone`/`execve`/`wait4` — the single biggest unlock.
2. Then **musl static BusyBox** — build `busybox` with
   `musl-gcc -static`, drop it in the initramfs, fill the syscall gaps
   from `docs/LINUX-ABI.md`.
3. Testing stays `make test ARCH=...` — extend EXPECT markers in
   `tools/vmtest.py` as features land.

Session notes: initial implementation on branch `devin/*` → PR on
`main`. The `website` branch holds the promo site — do not merge into
`main` without asking.

## 2026-10-11 (later) — Aura desktop v1 on PR #2

- `drivers/desktop.c` (compositor: wallpaper/menubar/dock/window/cursor),
  `drivers/input.c` (i8042 kbd set-1 via ctrl translation + PS/2 mouse),
  `framebuffer.c` console-in-rect + text grid; `input_wait_char()` merges
  kbd/mouse/serial. x86 user faults kill task (139); syscalls EFAULT-check.
- Gotcha: QEMU PS/2 kbd emits scancode set 2 — enable i8042 cmd-byte
  bit 0x40 (set2→set1 translation); F0/0x01 alone does NOT work.
- Gotcha: `glyph_at` must erase cell bg or scroll/repaint overlays strokes.
- aarch64: compositor renders; input is serial-only (i8042 is x86).
- Verified: vmtest 14/14 both arches; QMP sendkey + screendump x86_64.
- Next per ROADMAP M1: preemptive scheduling + tick IRQ (then real apps).
