#!/usr/bin/env bash
# P03: explicit DD runtime-policy seam tests.
#
# The core-state suite compiles the real api/callbacks.c and asserts the
# policy truth table (default off, strict values, independence from the
# debug callback and from the DD startup-diagnostics gate).  The receiver
# suite compiles the Parallel-RSP plugin's dd_policy.cpp plus rsp_diag.cpp
# and asserts the readback contract the core's optional capability uses.
set -euo pipefail
cd "$(dirname "$0")/.."

core_binary="$(mktemp)"
rsp_binary="$(mktemp)"
trap 'rm -f "$core_binary" "$rsp_binary"' EXIT

"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror \
	-I mupen64plus-core/upstream/src \
	-I mupen64plus-core/upstream/src/api \
	tools/tests/dd-runtime-policy-test.c \
	mupen64plus-core/upstream/src/api/callbacks.c \
	-o "$core_binary"
"$core_binary"
echo "DD runtime policy core-state tests passed"

"${CXX:-c++}" -std=c++14 -Wall -Wextra -Werror \
	-I mupen64plus-rsp-parallel/upstream \
	tools/tests/rsp-dd-policy-test.cpp \
	mupen64plus-rsp-parallel/upstream/dd_policy.cpp \
	mupen64plus-rsp-parallel/upstream/rsp_diag.cpp \
	-o "$rsp_binary"
"$rsp_binary"
echo "DD runtime policy receiver tests passed"
