#!/usr/bin/env bash
# Host check that TLB writes stay inside the TLB's 32 entries.
# See tools/tests/tlb-write-test.c.
set -euo pipefail
cd "$(dirname "$0")/.."
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
  -I mupen64plus-core/upstream/src \
  -I mupen64plus-core/upstream/subprojects/xxhash \
  tools/tests/tlb-write-test.c \
  -Wl,--gc-sections -o "$work/tlb-write-test"
"$work/tlb-write-test"
echo "TLB write check passed"
