#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

test_binary="$(mktemp)"
trap 'rm -f "$test_binary"' EXIT

"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror \
    -ffunction-sections -fdata-sections \
    -I mupen64plus-core/upstream/src \
    tools/tests/dd-core-imem-dma-test.c \
    mupen64plus-core/upstream/src/device/rcp/rsp/rsp_core.c \
    mupen64plus-core/upstream/src/device/dd/dd_load_history.c \
    mupen64plus-core/upstream/src/device/dd/dd_cmd_watch.c \
    -Wl,--gc-sections -o "$test_binary"
"$test_binary"
echo "DDSTART11 core CPU-to-IMEM DMA tests passed"