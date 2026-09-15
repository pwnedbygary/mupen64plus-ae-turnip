#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
test_binary="$(mktemp)"
trap 'rm -f "$test_binary"' EXIT
"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror \
    -I mupen64plus-core/upstream/src \
    mupen64plus-core/upstream/tools/tests/dd_cmd_watch_test.c \
    mupen64plus-core/upstream/src/device/dd/dd_cmd_watch.c \
    -o "$test_binary"
"$test_binary"