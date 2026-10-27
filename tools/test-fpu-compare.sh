#!/usr/bin/env bash
# Host check of the MIPS FPU compare helpers that the interpreters use.
# See tools/tests/fpu-compare-test.c.
set -euo pipefail
cd "$(dirname "$0")/.."
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
"${CC:-cc}" -std=gnu11 -O2 -Wall -Wextra -Werror \
  -I mupen64plus-core/upstream/src \
  tools/tests/fpu-compare-test.c -lm -o "$work/fpu-compare-test"
"$work/fpu-compare-test"
echo "FPU compare check passed"
