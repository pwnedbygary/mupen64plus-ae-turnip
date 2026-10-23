#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cc="${CC:-cc}"
includes=(
  -isystem mupen64plus-core/upstream/subprojects/md5
  -I mupen64plus-core/upstream/src
)
# The core's file helpers. Their warnings are off, as they are not edited here.
objects=()
for source in mupen64plus-core/upstream/src/main/util.c \
              mupen64plus-core/upstream/src/osal/files_unix.c; do
  object="$work/$(basename "$source" .c).o"
  "$cc" -std=gnu11 -w -ffunction-sections -fdata-sections \
    "${includes[@]}" -c "$source" -o "$object"
  objects+=("$object")
done
"$cc" -std=gnu11 -Wall -Werror -ffunction-sections -fdata-sections \
  "${includes[@]}" tools/tests/mempak-save-test.c \
  mupen64plus-core/upstream/src/backends/file_storage.c \
  mupen64plus-core/upstream/src/device/controllers/paks/mempak.c \
  "${objects[@]}" -Wl,--gc-sections -o "$work/mempak-save-test"
# The test's save file goes under $work, so the trap removes it even after a
# crash.
TMPDIR="$work" "$work/mempak-save-test"
echo "Controller Pak save check passed"
