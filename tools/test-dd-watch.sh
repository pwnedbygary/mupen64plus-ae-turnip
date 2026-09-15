#!/usr/bin/env bash
# P08a: watched-range observation infrastructure tests.
#
# Compiles the real device/dd/dd_watch.c together with the real core policy
# state (api/callbacks.c, P03) and drives the module with synthetic events —
# the policy gate, range normalization, generation stamping, the bounded
# ring with its drop/rejection counters and session isolation.  No writer is
# wired yet (P08b), so nothing here touches an emulation path.
set -euo pipefail
cd "$(dirname "$0")/.."

test_binary="$(mktemp)"
trap 'rm -f "$test_binary"' EXIT

"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror \
    -I mupen64plus-core/upstream/src \
    -I mupen64plus-core/upstream/src/api \
    tools/tests/dd-watch-test.c \
    mupen64plus-core/upstream/src/device/dd/dd_watch.c \
    mupen64plus-core/upstream/src/api/callbacks.c \
    -o "$test_binary"
"$test_binary"
echo "DD watched-range infrastructure tests passed"
