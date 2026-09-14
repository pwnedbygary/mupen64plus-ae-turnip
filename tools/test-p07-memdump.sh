#!/usr/bin/env bash
# Host checks for the P07 memory-dump helper and its one-line launcher.
#
# These lock the launcher content and the failure classes actually hit on
# this device: mksh's `[` rejects hexadecimal in integer comparisons
# (`0xf000: bad number`), and mksh arithmetic is 32-bit so 64-bit host
# addresses wrap (`$((0x6fc2c5f000))` = -1027215360). Host addresses must
# therefore come from raw map strings or the awk hex helper, never from
# shell arithmetic.
set -euo pipefail
cd "$(dirname "$0")/.."

helper=tools/p07-frozen-memdump.sh
launcher=tools/launch-p07-memdump.sh
failures=0

note() { echo "FAIL: $*" >&2; failures=$((failures + 1)); }

# Comment-stripped code, used by the structural checks below.
code="$(grep -vE '^[[:space:]]*#' "$helper")"

# 1. POSIX syntax for both files.
sh -n "$helper" || note "helper fails sh -n"
sh -n "$launcher" || note "launcher fails sh -n"

# 2. Launcher is exactly the one-line convention (single command line).
expected='/system/bin/sh /sdcard/Download/p07-memdump.sh >> /sdcard/Download/p07-launch.log 2>&1'
actual="$(sed -e 's/[[:space:]]*$//' "$launcher" | head -1)"
[ "$actual" = "$expected" ] || note "launcher content mismatch: '$actual'"
lines="$(grep -cve '^[[:space:]]*$' "$launcher")"
[ "$lines" -eq 1 ] || note "launcher has $lines non-empty lines (must be 1)"

# 3. Strong invariant: after removing comments and arithmetic expansions, no
#    `0x` literal may appear inside any `[ ]` in the helper (mksh's `[` does
#    not parse hex in integer comparisons).
stripped="$(sed -e 's/#.*//' -e 's/\$(([^)]*))//g' "$helper")"
if printf '%s\n' "$stripped" | grep -nE '\[[^]]*0x'; then
    note "hex literal reaches [ ] directly somewhere in the helper"
fi

# 4. 32-bit arithmetic invariant: no hexadecimal literal at all inside
#    shell arithmetic in the code (host addresses are 64-bit; mksh wraps
#    them, and the removed forms included uppercase digits and 0x${...}).
#    The awk helper is the only address-arithmetic path. The invariant is
#    self-tested below so it cannot silently regress.
arith_bad='\$\(\(.*0x'
if printf '%s\n' "$code" | grep -nE "$arith_bad"; then
    note "hex literal in shell arithmetic (mksh arithmetic is 32-bit)"
fi
for probe in \
    'cand=$((start + 0x6fc2c5f000))' \
    'cand=$((start + 0x771D68))' \
    'cand=$((start + 0x6EEAA0))' \
    'start=$((0x${region%-*}))' \
    'skip=$((0x$canary_addr))'; do
    printf '%s\n' "$probe" | grep -qE "$arith_bad" \
        || note "32-bit invariant does not catch: $probe"
done
printf '%s\n' 'n=$((1 + 2))' | grep -qE "$arith_bad" \
    && note "32-bit invariant false-positives on decimal arithmetic"

# 5. Window design anchors: full-mode MM_RSP_MEM offset, the RDRAM window,
#    the awk helper, and the RDRAM window read.
grep -q 'MM_RSP_MEM_HEX=4000000' "$helper" || note "missing full-mode MM_RSP_MEM offset"
grep -q 'RDRAM_WINDOW_BYTES=8388608' "$helper" || note "missing 8 MiB RDRAM window"
grep -q 'a64()' "$helper" || note "missing awk hex-arithmetic helper"
grep -q 'read_window "0x\$BASE" \$RDRAM_WINDOW_BYTES' "$helper" || note "RDRAM window read missing"
grep -q 'a64 "\$BASE_MAP_START" round' "$helper" || note "missing 64 KiB mem_base rounding"
printf '%s\n' "$code" | grep -q '0x5000000' && note "compressed-mode offset used in code"

# 6. Ordering invariants: the canary read must precede any test of its
#    result, and the coherent-snapshot stop must be restored on exit.
c=$(printf '%s\n' "$code" | grep -n 'of="\$DIR/canary-sample.bin"' | head -1 | cut -d: -f1)
t=$(printf '%s\n' "$code" | grep -n 'if \[ "\$canary_out" != "4" \]' | head -1 | cut -d: -f1)
if [ -z "$c" ] || [ -z "$t" ] || [ "$c" -ge "$t" ]; then
    note "canary dd read does not precede its failure test"
fi
printf '%s\n' "$code" | grep -q "trap 'kill -CONT" || note "no SIGCONT restore trap"

# 7. Anchor addresses/sizes that the evidence doc depends on.
grep -q '0x771D68' "$helper" || note "missing gCurAudioTask pointer address"
grep -q '0x411910' "$helper" || note "missing command-buffer address"
grep -q '0x768e60' "$helper" || note "missing aspMain image address"
grep -q 'CURTASK_OFF_HEX=771D68' "$helper" || note "missing curtask offset constant"

# 8. Executable regression for the awk address helper: a grep cannot catch
#    operand-base bugs (round once returned decimal into a hex parser).
a64_code="$(sed -n '/^a64() {/,/^}$/p' "$helper")"
if [ -n "$a64_code" ]; then
    eval "$a64_code"
    r="$(a64 6fc2c5f000 round "")"
    [ "$r" = "6fc2c60000" ] || note "a64 round: got '$r', want 6fc2c60000"
    v="$(a64 "$r" + 4000000)"
    [ "$v" = "480076234752" ] || note "a64 round+add(rsp): got '$v', want 480076234752 (rounded base + 0x4000000)"
    v="$(a64 "$r" + 771d68)"
    [ "$v" = "480016932200" ] || note "a64 round+add(curtask): got '$v', want 480016932200 (rounded base + 0x771D68)"
    v="$(a64 6fc2e5f000 - 6fc2c5f000)"
    [ "$v" = "2097152" ] || note "a64 sub: got '$v', want 2097152"
    v="$(a64 6fc2c60000 round "")"
    [ "$v" = "6fc2c60000" ] || note "a64 round identity: got '$v', want 6fc2c60000"
    v="$(a64 1 round "")"
    [ "$v" = "10000" ] || note "a64 round(1): got '$v', want 10000"
    unset -f a64
else
    note "could not extract the a64 helper for execution"
fi

# 9. If mksh is available locally, parse both files with it (the device shell).
if command -v mksh >/dev/null 2>&1; then
    mksh -n "$helper" || note "helper fails mksh -n"
    mksh -n "$launcher" || note "launcher fails mksh -n"
fi

if [ "$failures" -ne 0 ]; then
    echo "p07-memdump host checks: $failures failure(s)" >&2
    exit 1
fi
echo "p07-memdump host checks passed"
