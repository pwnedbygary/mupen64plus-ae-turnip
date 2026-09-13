#!/usr/bin/env bash
# P02/P04: production-path SP DMA transfer fixtures (see
# tools/tests/rsp-dd-dma-transfer-test.cpp for the case matrix and the
# docs/P01_DMA_POLICY_LEDGER.md oracle contract).
#
# The legacy suite drives the real P03 policy seam to off and pins the
# documented DD-off behavior (validation D01, D22, D23).
#
# Since P04, the corrected suite drives the same seam to on, so production
# rsp_dma_read takes the corrected arm implementing the P01 policy; both
# modes are hard gates.  (Before P04 this script tolerated documented
# corrected-mode XFAILs behind DD_DMA_REQUIRE_CORRECTED=1; that tolerance
# is gone now that the correction exists.)
set -euo pipefail
cd "$(dirname "$0")/.."

binary="$(mktemp)"
trap 'rm -f "$binary"' EXIT

# Production translation units are compiled with the same strict warnings as
# the other host suites. PARALLEL_INTEGRATION selects the production DMA path;
# dd_policy.cpp is the real policy receiver the fixture drives.
${CXX:-c++} -std=c++14 -Wall -Wextra -Werror -DPARALLEL_INTEGRATION -DM64P_PLUGIN_API \
	-I mupen64plus-rsp-parallel/upstream \
	-I mupen64plus-rsp-parallel/upstream/api \
	-I mupen64plus-rsp-parallel/upstream/arch/simd/rsp \
	tools/tests/rsp-dd-dma-transfer-test.cpp \
	mupen64plus-rsp-parallel/upstream/rsp/cp0.cpp \
	mupen64plus-rsp-parallel/upstream/dd_policy.cpp \
	mupen64plus-rsp-parallel/upstream/rsp_diag.cpp \
	-o "$binary"

"$binary" legacy
"$binary" corrected
echo "DD DMA transfer tests passed (legacy and corrected policies)"
