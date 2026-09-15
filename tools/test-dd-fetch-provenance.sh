#!/usr/bin/env bash
# P08b: fetch-provenance fixtures.
#
# Compiles the real DMA read path (rsp/cp0.cpp), the real policy receiver
# (dd_policy.cpp) and the real diagnostics/emission code (rsp_diag.cpp) with
# PARALLEL_INTEGRATION, and drives the watch and the read path through the
# production RSP_MTC0 entry.  Expectations are computed inside the fixture
# from the RDRAM pattern it installs.
set -euo pipefail
cd "$(dirname "$0")/.."

binary="$(mktemp)"
trap 'rm -f "$binary"' EXIT

${CXX:-c++} -std=c++14 -Wall -Wextra -Werror -DPARALLEL_INTEGRATION -DM64P_PLUGIN_API \
	-I mupen64plus-rsp-parallel/upstream \
	-I mupen64plus-rsp-parallel/upstream/api \
	-I mupen64plus-rsp-parallel/upstream/arch/simd/rsp \
	tools/tests/rsp-dd-fetch-provenance-test.cpp \
	mupen64plus-rsp-parallel/upstream/rsp/cp0.cpp \
	mupen64plus-rsp-parallel/upstream/dd_policy.cpp \
	mupen64plus-rsp-parallel/upstream/rsp_diag.cpp \
	-o "$binary"

"$binary"
echo "DD fetch-provenance tests passed"
