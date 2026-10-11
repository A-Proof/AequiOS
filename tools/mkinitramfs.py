#!/usr/bin/env python3
"""Pack a directory tree into a deterministic ustar initramfs."""
import os
import sys
import tarfile


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: mkinitramfs.py <root dir> <out.tar>")
        return 1
    root, out = sys.argv[1], sys.argv[2]

    def reset(ti: tarfile.TarInfo) -> tarfile.TarInfo:
        ti.uid = ti.gid = 0
        ti.uname = ti.gname = ""
        ti.mtime = 0
        return ti

    entries = []
    for dirpath, _dirs, files in os.walk(root):
        for name in files:
            full = os.path.join(dirpath, name)
            rel = os.path.relpath(full, root)
            entries.append((rel, full))
    entries.sort()

    with tarfile.open(out, "w", format=tarfile.USTAR_FORMAT) as tar:
        for rel, full in entries:
            tar.add(full, arcname=rel, filter=reset)
    print(f"initramfs: {len(entries)} files → {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
