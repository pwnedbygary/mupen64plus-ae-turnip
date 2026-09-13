#!/usr/bin/env bash
# P02: production-path SP DMA transfer fixtures (see
# tools/tests/rsp-dd-dma-transfer-test.cpp for the case matrix and the
# docs/P01_DMA_POLICY_LEDGER.md oracle contract).
#
# The legacy suite must always pass: it pins the documented DD-off behavior
# (validation D01, D22, D23) against the real production code.
#
# The corrected suite asserts the P01 corrected policy. Before P03/P04 land
# the explicit DD policy seam, production only implements the legacy path,
# so the corrected run reports XFAIL divergences and this script tolerates
# them (that IS the documented pre-fix failure). A corrected case that
# unexpectedly matches pre-fix fails the suite (XPASS = misclassification).
#
# After P04 delivers the correction: run with DD_DMA_REQUIRE_CORRECTED=1 so
# the corrected suite gates commits (the P04 patch must flip the default
# below or export the variable in CI).
set -euo pipefail
cd "$(dirname "$0")/.."

binary="$(mktemp)"
trap 'rm -f "$binary"' EXIT

# Production translation units are compiled with the same strict warnings as
# the other host suites. PARALLEL_INTEGRATION selects the production DMA path.
${CXX:-c++} -std=c++14 -Wall -Wextra -Werror -DPARALLEL_INTEGRATION -DM64P_PLUGIN_API \
	-I mupen64plus-rsp-parallel/upstream \
	-I mupen64plus-rsp-parallel/upstream/api \
	-I mupen64plus-rsp-parallel/upstream/arch/simd/rsp \
	tools/tests/rsp-dd-dma-transfer-test.cpp \
	mupen64plus-rsp-parallel/upstream/rsp/cp0.cpp \
	mupen64plus-rsp-parallel/upstream/rsp_diag.cpp \
	-o "$binary"

"$binary" legacy

require_corrected="${DD_DMA_REQUIRE_CORRECTED:-0}"
if [ "$require_corrected" = "1" ]; then
	"$binary" corrected --strict
	echo "DD DMA transfer tests passed (corrected policy required)"
else
	# Pre-fix: corrected divergences are the expected, documented failure.
	# The run fails if any corrected case matches (XPASS) or if a shared
	# case diverges, so oracle/classification mistakes still surface.
	if "$binary" corrected; then
		echo "DD DMA transfer tests passed (corrected policy XFAIL as" \
			"documented pre-P03/P04)"
	else
		echo "DD DMA transfer tests FAILED: corrected suite diverged " \
			"beyond the expected XFAIL pattern" >&2
		exit 1
	fi
fi
