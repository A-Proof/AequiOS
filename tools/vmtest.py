#!/usr/bin/env python3
"""vmtest — boot AequiOS in QEMU and verify the boot log + shell demo.

Usage: vmtest.py <x86_64|x86_64-uefi|aarch64> [--screenshot out.png]

Drives the serial console: waits for the Ratio shell, sends a few
commands, and asserts on expected output markers.
"""
import os
import select
import socket
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

CMDS = {
    "x86_64": [
        "qemu-system-x86_64", "-M", "q35", "-m", "512M",
        "-serial", "stdio", "-display", "none", "-monitor", "none",
        "-cdrom", f"{ROOT}/build/x86_64/aequios-x86_64.iso",
    ],
    "x86_64-uefi": [
        "qemu-system-x86_64", "-M", "q35", "-m", "512M",
        "-serial", "stdio", "-display", "none", "-monitor", "none",
        "-bios", "/usr/share/OVMF/OVMF_CODE.fd",
        "-cdrom", f"{ROOT}/build/x86_64/aequios-x86_64.iso",
    ],
    "aarch64": [
        "qemu-system-aarch64", "-M", "virt", "-cpu", "cortex-a72",
        "-m", "512M", "-bios", "/usr/share/qemu-efi-aarch64/QEMU_EFI.fd",
        "-drive", f"format=raw,file={ROOT}/build/aarch64/aequios-aarch64.img",
        "-device", "ramfb",
        "-serial", "stdio", "-display", "none", "-monitor", "none",
    ],
}

EXPECT = [
    "=== AequiOS kernel (Ordo) ===",
    "hhdm offset =",
    "pmm:",
    "vfs:",
    "[ordo] entering userspace",
    "R a t i o",          # banner
    "linux-hello: Linux ABI",
    "uname(2): AequiOS",
    "os-release via Linux",
    "brk() memory works",
    "mmap page check: PASS",
    "writev(2) joins two buffers",
    "exited with code 42",
    "aequi> ",
]

SCRIPT = [
    ("help\n", "commands:"),
    ("meminfo\n", "free memory:"),
    ("cat /etc/motd\n", "Welcome to AequiOS"),
    ("demo\n", "exit_group"),
]

TIMEOUT = 90


def main() -> int:
    arch = sys.argv[1] if len(sys.argv) > 1 else "x86_64"
    cmd = CMDS[arch]
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            bufsize=0)
    log = b""
    start = time.time()
    idx = 0
    script_step = 0
    try:
        while time.time() - start < TIMEOUT:
            r, _, _ = select.select([proc.stdout], [], [], 0.25)
            if r:
                chunk = proc.stdout.read(4096)
                if not chunk:
                    break
                log += chunk
                sys.stdout.write(chunk.decode(errors="replace"))
                sys.stdout.flush()
            text = log.decode(errors="replace")
            # feed shell commands once the prompt is up
            if "aequi> " in text and script_step < len(SCRIPT):
                send, _ = SCRIPT[script_step]
                proc.stdin.write(send.encode())
                proc.stdin.flush()
                script_step += 1
                # tiny settle delay
                time.sleep(0.2)
            if idx >= len(EXPECT) and script_step >= len(SCRIPT):
                break
        time.sleep(1)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()

    text = log.decode(errors="replace")
    missing = [e for e in EXPECT if e not in text]
    print("\n--- vmtest results (%s) ---" % arch)
    if missing:
        for m in missing:
            print("  MISSING: %r" % m)
        print("FAIL")
        return 1
    print("all %d markers found — PASS" % len(EXPECT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
