#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -fcommon \
  -ffunction-sections -fdata-sections \
  -I mupen64plus-core/upstream/src \
  -I mupen64plus-core/upstream/subprojects/md5 \
  tools/tests/big-rom-bounds-test.c \
  mupen64plus-core/upstream/src/device/cart/cart_rom.c \
  -Wl,--gc-sections -o "$work/big-rom-bounds-test"
"$work/big-rom-bounds-test"
echo "big-ROM bounds check passed"
