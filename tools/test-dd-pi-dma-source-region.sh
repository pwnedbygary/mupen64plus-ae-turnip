#!/usr/bin/env bash
# P09: pure source-region classifier for PI-to-RDMA DMA transfers.
#
# Compiles the real production dd_load_history.c (the receiver of dd_pi...source_region) and links it with a small core-state fixture that asserts the helper's contract against the N64DD memory-map rules -- not against production constants. The expected classifications come from the documented contract; device.h supplies neither the windows nor the enum mapping, so this cannot pass by luck.
#
# clang is used explicitly: host gcc's cc1/cc1plus spawn broken on this host (see
# docs/DEVELOPMENT_PROCESS.md tooling note), while clang compiles cleanly.  The CC
# override still applies when a caller wants to force a specific compiler.
set -euo pipefail
cd "$(dirname "$0")/.."

binary="$(mktemp)"
trap 'rm -f "$binary"' EXIT

"${CC:-clang}" -std=gnu11 -Wall -Wextra \
	-I mupen64plus-core/upstream/src \
	-I mupen64plus-core/upstream/src/api \
	tools/tests/dd-pi-dma-source-region-test.c \
	mupen64plus-core/upstream/src/device/dd/dd_load_history.c \
	mupen64plus-core/upstream/src/api/callbacks.c \
	-o "$binary"

"$binary"
echo "DD PI DMA source-region classifier tests passed"
