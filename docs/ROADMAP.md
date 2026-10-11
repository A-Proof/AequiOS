# AequiOS Roadmap

Where this is going, honestly scoped. Each milestone lists what it
unblocks and roughly how big it is.

```
        done                next            later                    far
 [kernel+ABI v0] → [process model] → [LumenFS write] → [TCP/IP + NIC] → [desktop]
                         ↓                                  ↑
                   [BusyBox static]                  [real Linux apps]
```

## M0 — foundation (this release)

* Limine boot, serial console, PMM/VMM/kmalloc, initramfs VFS,
  ring-3 tasks, spawn/wait, Aura framebuffer splash + console
* Linux ABI v0 (12 calls each arch) — `linux-hello` passes on
  x86_64 and aarch64
* vmtest harness: `make test` on both arches

## M1 — real process model (2–3 sessions)

* `clone`/`execve`/`wait4` instead of blocking spawn; round-robin
  scheduler + timer tick (APIC on x86, generic timer on ARM)
* Proper task teardown: free pages, close fds, reparent
* Fork a second user task; run BusyBox `sh` **statically linked with
  musl** — needs the syscalls in LINUX-ABI.md groups 1–5 and 7
* Deliverable: `busybox ls`, `busybox cat`, `busybox echo` work

## M2 — storage & files (2 sessions)

* LumenFS v1: writable fs — start with FAT32 on a virtio-blk disk,
  tar stays for initramfs
* `O_WRONLY`/`O_CREAT`/`O_TRUNC`, `unlink`, `mkdir`, `rename`,
  `getdents64`, `stat*`
* Deliverable: `busybox vi`-editable file that survives reboot

## M3 — internet (3–4 sessions)

* virtio-net driver (or e1000 on x86), ARP/IPv4/ICMP first
* UDP then TCP — port lwIP rather than writing a stack from scratch
  (it is what every hobby OS does; writing TCP by hand is a trap)
* `socket`/`connect`/`send`/`recv`/`bind` in the Linux layer
* Deliverable: `ping`, then BusyBox `wget http://...`

## M4 — desktop UI (4+ sessions)

* Aura v1: framebuffer compositor — window rects, damage tracking,
  mouse (virtio-input/PS2), a real widget set before any "apps"
* GUI story for Linux apps is **X11/Wayland-free at first**: a
  framebuffer-backed `wl_shm`-like protocol, then run framebuffer apps
  (fbv, links2 -g, custom). Real X/Wayland clients are a distant port.
* Deliverable: windowed terminal + a GUI demo app side by side

## M5 — broader Linux compatibility (ongoing)

* Dynamic loader support (`mmap` file-backed, `mprotect`, auxv),
  signals, pipes, `poll`/`epoll`
* Then: musl or glibc toolchain sysroot → port packages
* Long tail: GPU accel, sound, USB — after everything else

## Non-goals (for now)

* SMP/multicore, 32-bit compat (`int 0x80` / EABI), kernel modules,
  POSIX-perfect semantics, secure sandboxing of untrusted Linux apps.
