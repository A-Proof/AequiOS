#!/usr/bin/env bash
# AequiOS build script — convenience wrapper over the Makefile.
#   ./build.sh x86_64 [run|test]   ./build.sh aarch64 [run|test]
set -euo pipefail
ARCH="${1:-x86_64}"
ACTION="${2:-image}"
case "$ACTION" in
    run)  make ARCH="$ARCH" run ;;
    test) python3 tools/vmtest.py "$ARCH" ;;
    *)    make ARCH="$ARCH" image ;;
esac
