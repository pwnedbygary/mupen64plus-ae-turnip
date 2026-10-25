#!/usr/bin/env bash
# Host check that SRAM, FlashRAM and the IS-Viewer stay inside their buffers
# and RDRAM. See tools/tests/cart-save-bounds-test.c.
set -euo pipefail
cd "$(dirname "$0")/.."
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cc="${CC:-cc}"
# AddressSanitizer, where the compiler has it, also catches the IS-Viewer's
# overlapping copy, which leaves no trace without it.
sanitize=()
if echo 'int main(void) { return 0; }' | "$cc" -x c -fsanitize=address - -o "$work/probe" 2>/dev/null; then
  sanitize=(-fsanitize=address -fno-omit-frame-pointer)
fi
objects=()
for source in mupen64plus-core/upstream/src/device/cart/sram.c \
              mupen64plus-core/upstream/src/device/cart/flashram.c \
              mupen64plus-core/upstream/src/device/cart/is_viewer.c; do
  object="$work/$(basename "$source" .c).o"
  "$cc" -std=gnu11 -w ${sanitize[@]+"${sanitize[@]}"} \
    -I mupen64plus-core/upstream/src -c "$source" -o "$object"
  objects+=("$object")
done
"$cc" -std=gnu11 -Wall -Wextra -Werror ${sanitize[@]+"${sanitize[@]}"} \
  -I mupen64plus-core/upstream/src \
  tools/tests/cart-save-bounds-test.c "${objects[@]}" \
  -o "$work/cart-save-bounds-test"
"$work/cart-save-bounds-test"
if [ ${#sanitize[@]} -gt 0 ]; then
  echo "Cartridge save bounds check passed (with AddressSanitizer)"
else
  echo "Cartridge save bounds check passed"
fi
