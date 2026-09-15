#!/bin/sh
set -eu

TEST_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
SRC_DIR=$(CDPATH= cd -- "$TEST_DIR/../../src" && pwd)
TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/dd-load-history-test.XXXXXX")
trap 'rm -rf "$TMP_DIR"' EXIT HUP INT TERM

: "${CC:=cc}"

"$CC" -std=gnu11 -Wall -Wextra -Werror \
    -I"$SRC_DIR" \
    "$TEST_DIR/dd_load_history_test.c" \
    "$SRC_DIR/device/dd/dd_load_history.c" \
    -o "$TMP_DIR/dd_load_history_test"
"$TMP_DIR/dd_load_history_test"

"$CC" -std=gnu11 -Wall -Wextra -Werror -DM64P_BIG_ENDIAN \
    -I"$SRC_DIR" \
    "$TEST_DIR/dd_load_history_test.c" \
    "$SRC_DIR/device/dd/dd_load_history.c" \
    -o "$TMP_DIR/dd_load_history_test_be"
"$TMP_DIR/dd_load_history_test_be"