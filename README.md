# AequiOS

*Even in Architecture, Design, and Spirit.*

A from-scratch operating system by Nova Developers: our own **Ordo**
kernel (Initium boot, Memoria memory, Vita tasks, LumenFS, Aura display,
Serenus userspace) with a **Linux syscall-compatibility layer** so real
Linux binaries run on it — on **x86_64 and aarch64**.

## Status — v0.1

* Boots in QEMU on x86_64 (BIOS + UEFI) and aarch64 (UEFI `virt`)
* Bitmap PMM, 4-level VMM, slab heap, initramfs VFS (LumenFS v0)
* Ring-3/EL0 tasks, blocking spawn+wait, ELF loader
* **Ratio** shell over serial; Aura framebuffer splash + console
* Linux ABI v0 — a Linux-syscall demo binary runs on both arches

```bash
make image ARCH=x86_64 && make run ARCH=x86_64   # boot in QEMU
make test  ARCH=aarch64                          # scripted QEMU test
```

See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md),
[`docs/LINUX-ABI.md`](docs/LINUX-ABI.md),
[`docs/ROADMAP.md`](docs/ROADMAP.md) (internet, desktop UI, full Linux
ABI), and [`HANDOFF.md`](HANDOFF.md).

## Meta

This repository shows the official versions and website of AequiOS,
created by Nova Developers. The promo site lives on the `website`
branch.
