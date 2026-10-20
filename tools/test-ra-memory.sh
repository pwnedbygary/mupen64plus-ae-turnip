#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -fcommon -pthread \
  -ffunction-sections -fdata-sections \
  -I mupen64plus-core/upstream/src \
  -I mupen64plus-core/rcheevos/include \
  tools/tests/ra-memory-test.c \
  mupen64plus-core/rcheevos/src/rcheevos/consoleinfo.c \
  -Wl,--gc-sections -o "$work/ra-memory-test"
"$work/ra-memory-test"
echo "RetroAchievements memory read check passed"
