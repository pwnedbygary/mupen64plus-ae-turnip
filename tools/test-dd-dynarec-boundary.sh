#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
  -I mupen64plus-core/upstream/src \
  -I mupen64plus-core/upstream/subprojects/md5 \
  tools/tests/new-dynarec-boundary-test.c \
  mupen64plus-core/upstream/src/api/callbacks.c \
  -Wl,--gc-sections -o "$work/boundary-test"
"$work/boundary-test"
echo "DD dynarec boundary policy check passed"