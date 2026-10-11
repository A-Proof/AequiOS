#!/usr/bin/env bash
# Fetch and build the Limine bootloader into third_party/limine.
set -euo pipefail
DEST="${1:-third_party/limine}"
REF="v12.9.3"
if [ ! -d "$DEST" ]; then
    git clone --depth 1 --branch "$REF" https://github.com/limine-bootloader/limine.git "$DEST"
fi
cd "$DEST"
if [ ! -x ./limine ]; then
    ./bootstrap
    ./configure --enable-bios --enable-bios-cd --enable-uefi-x86-64 \
                --enable-uefi-aarch64 --enable-uefi-cd
    make -j"$(nproc)"
fi
