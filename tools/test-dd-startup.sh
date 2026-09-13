#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
test_binary="$(mktemp)"
layout_binary="$(mktemp)"
dynarec_binary="$(mktemp)"
rsp_diag_binary="$(mktemp)"
trap 'rm -f "$test_binary" "$layout_binary" "$dynarec_binary" "$rsp_diag_binary"' EXIT
"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -I mupen64plus-core/upstream/src -I mupen64plus-core/upstream/src/api tools/tests/dd-startup-callbacks-test.c mupen64plus-core/upstream/src/api/callbacks.c -o "$test_binary"
"$test_binary"
echo "DD startup callback tests passed"
"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections -I mupen64plus-core/upstream/src -I mupen64plus-core/upstream/subprojects/md5 tools/tests/dd-startup-dynarec-observer-test.c mupen64plus-core/upstream/src/api/callbacks.c -Wl,--gc-sections -o "$dynarec_binary"
"$dynarec_binary"
echo "DD startup dynarec observer tests passed"
"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -I mupen64plus-core/upstream/src tools/tests/dd-startup-fault-layout-test.c -o "$layout_binary"
"$layout_binary"
echo "DD startup fault-layout ABI tests passed"
"${CXX:-c++}" -std=c++14 -Wall -Wextra -Werror -I mupen64plus-rsp-parallel/upstream \
    tools/tests/rsp-dd-provenance-test.cpp \
    mupen64plus-rsp-parallel/upstream/rsp_diag.cpp -o "$rsp_diag_binary"
"$rsp_diag_binary"
echo "DD startup RSP provenance tests passed"