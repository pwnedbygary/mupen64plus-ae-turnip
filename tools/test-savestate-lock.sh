#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cc="${CC:-cc}"
# savestates.c includes <minizip/unzip.h> and <minizip/zip.h>: the
# subprojects folder makes those the core's copies, not the system's.
includes=(
  -isystem mupen64plus-core/upstream/subprojects/md5
  -isystem mupen64plus-core/upstream/subprojects/minizip
  -isystem mupen64plus-core/upstream/subprojects
  -isystem ndkLibs/SDL2_stub
  -I mupen64plus-core/upstream/src
)
# The core's helpers that the save path calls. Their warnings are off, as
# they are not edited here.
objects=()
for source in mupen64plus-core/upstream/src/main/util.c \
              mupen64plus-core/upstream/src/osal/files_unix.c; do
  object="$work/$(basename "$source" .c).o"
  "$cc" -std=gnu11 -w -pthread -ffunction-sections -fdata-sections \
    "${includes[@]}" -c "$source" -o "$object"
  objects+=("$object")
done
# The test includes savestates.c and the SDL stub headers, so its warnings are
# off too. --gc-sections drops the parts of savestates.c that it does not use.
"$cc" -std=gnu11 -w -pthread -ffunction-sections -fdata-sections \
  "${includes[@]}" tools/tests/savestate-lock-test.c "${objects[@]}" \
  -Wl,--gc-sections -lz -o "$work/savestate-lock-test"
# The test's state folder goes under $work, so the trap removes it even
# after a crash.
TMPDIR="$work" "$work/savestate-lock-test"
echo "Save state lock check passed"
