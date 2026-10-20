#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cc="${CC:-cc}"
includes=(
  -I mupen64plus-core/upstream/subprojects/md5
  -I mupen64plus-core/upstream/src
  -I mupen64plus-core/rcheevos/include
  -I mupen64plus-core/rcheevos/src
  -I mupen64plus-core/rcheevos/src/rhash
)
# md5.c and the rcheevos sources that mupen64plus-core.mk builds into the core.
sources=(
  mupen64plus-core/upstream/subprojects/md5/md5.c
  mupen64plus-core/rcheevos/src/rc_client.c
  mupen64plus-core/rcheevos/src/rc_util.c
  mupen64plus-core/rcheevos/src/rc_compat.c
  mupen64plus-core/rcheevos/src/rc_version.c
  mupen64plus-core/rcheevos/src/rcheevos/alloc.c
  mupen64plus-core/rcheevos/src/rcheevos/condition.c
  mupen64plus-core/rcheevos/src/rcheevos/condset.c
  mupen64plus-core/rcheevos/src/rcheevos/consoleinfo.c
  mupen64plus-core/rcheevos/src/rcheevos/format.c
  mupen64plus-core/rcheevos/src/rcheevos/lboard.c
  mupen64plus-core/rcheevos/src/rcheevos/memref.c
  mupen64plus-core/rcheevos/src/rcheevos/operand.c
  mupen64plus-core/rcheevos/src/rcheevos/rc_validate.c
  mupen64plus-core/rcheevos/src/rcheevos/richpresence.c
  mupen64plus-core/rcheevos/src/rcheevos/runtime.c
  mupen64plus-core/rcheevos/src/rcheevos/runtime_progress.c
  mupen64plus-core/rcheevos/src/rcheevos/trigger.c
  mupen64plus-core/rcheevos/src/rcheevos/value.c
  mupen64plus-core/rcheevos/src/rapi/rc_api_common.c
  mupen64plus-core/rcheevos/src/rapi/rc_api_editor.c
  mupen64plus-core/rcheevos/src/rapi/rc_api_info.c
  mupen64plus-core/rcheevos/src/rapi/rc_api_runtime.c
  mupen64plus-core/rcheevos/src/rapi/rc_api_user.c
  mupen64plus-core/rcheevos/src/rhash/aes.c
  mupen64plus-core/rcheevos/src/rhash/cdreader.c
  mupen64plus-core/rcheevos/src/rhash/hash.c
  mupen64plus-core/rcheevos/src/rhash/hash_disc.c
  mupen64plus-core/rcheevos/src/rhash/hash_encrypted.c
  mupen64plus-core/rcheevos/src/rhash/hash_rom.c
  mupen64plus-core/rcheevos/src/rhash/hash_zip.c
)
objects=()
for source in "${sources[@]}"; do
  object="$work/$(basename "$source" .c).o"
  # Vendored sources are not edited here, so their warnings are off.
  "$cc" -std=gnu11 -w -pthread -ffunction-sections -fdata-sections \
    "${includes[@]}" -c "$source" -o "$object"
  objects+=("$object")
done
"$cc" -std=gnu11 -Wall -Wextra -Werror -fcommon -pthread \
  -ffunction-sections -fdata-sections "${includes[@]}" \
  tools/tests/ra-session-test.c "${objects[@]}" \
  -Wl,--gc-sections -lm -o "$work/ra-session-test"
"$work/ra-session-test"
echo "RetroAchievements session check passed"
