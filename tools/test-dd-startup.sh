#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
test_binary="$(mktemp)"
trap 'rm -f "$test_binary"' EXIT
"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -I mupen64plus-core/upstream/src -I mupen64plus-core/upstream/src/api tools/tests/dd-startup-callbacks-test.c mupen64plus-core/upstream/src/api/callbacks.c -o "$test_binary"
"$test_binary"
echo "DD startup callback tests passed"