#!/usr/bin/env bash
# Host checks for the P07 frozen-memory dump helper and its one-line launcher.
#
# The first device run of the helper failed because mksh's `[` does not parse
# hexadecimal literals in integer comparisons (`0xf000: bad number`), so the
# probe loop never executed. These checks lock the launcher content and forbid
# that whole class of bug (bare hex inside test brackets), plus verify the
# corrected loop terminates under a POSIX shell.
set -euo pipefail
cd "$(dirname "$0")/.."

helper=tools/p07-frozen-memdump.sh
launcher=tools/launch-p07-memdump.sh
failures=0

note() { echo "FAIL: $*" >&2; failures=$((failures + 1)); }

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
#    not parse hex in integer comparisons). Hex inside $(( )) is fine and is
#    removed first, so this cannot false-positive on the legitimate forms.
stripped="$(sed -e 's/#.*//' -e 's/\$(([^)]*))//g' "$helper")"
if printf '%s\n' "$stripped" | grep -nE '\[[^]]*0x'; then
    note "hex literal reaches [ ] directly somewhere in the helper"
fi

# 4. The probe loop, with the bound and step read from the helper itself,
#    terminates under a POSIX shell with bound/step + 1 candidates.
bound="$(grep -oE 'while \[ \$off -le [0-9]+ \]' "$helper" | head -1 | grep -oE '[0-9]+')"
step="$(grep -oE 'off=\$\(\(off \+ 0x[0-9a-f]+\)\)' "$helper" | head -1 | grep -oE '0x[0-9a-f]+')"
if [ -z "$bound" ] || [ -z "$step" ]; then
    note "probe bound or step not found in the helper"
else
    expected_n=$((bound / step + 1))
    sh -c "off=0; n=0; while [ \$off -le $bound ]; do off=\$((off + $step)); n=\$((n + 1)); done; [ \"\$n\" -eq $expected_n ]" \
        || note "probe loop (bound=$bound step=$step) does not yield $expected_n candidates"
fi

# 4c. Ordering invariants: the canary read must precede any test of its
#     result, and the coherent-snapshot stop must be restored on exit.
code="$(grep -vE '^[[:space:]]*#' "$helper")"
c=$(printf '%s\n' "$code" | grep -n 'of="\$DIR/canary-sample.bin"' | head -1 | cut -d: -f1)
t=$(printf '%s\n' "$code" | grep -n 'if \[ "\$canary_out" != "4" \]' | head -1 | cut -d: -f1)
if [ -z "$c" ] || [ -z "$t" ] || [ "$c" -ge "$t" ]; then
    note "canary dd read does not precede its failure test"
fi
printf '%s\n' "$code" | grep -q "trap 'kill -CONT" || note "no SIGCONT restore trap"
printf '%s\n' "$code" | grep -q 'read_region 0x4000000' || note "RSP memory read not at 0x4000000"

# 4b. If mksh is available locally, parse both files with it (the device shell).
if command -v mksh >/dev/null 2>&1; then
    mksh -n "$helper" || note "helper fails mksh -n"
    mksh -n "$launcher" || note "launcher fails mksh -n"
fi

# 5. Anchor addresses/sizes that the evidence doc depends on.
grep -q '0x771D68' "$helper" || note "missing gCurAudioTask pointer address"
grep -q '0x411910' "$helper" || note "missing command-buffer address"
grep -q '0x4000000' "$helper" || note "missing full-mode MM_RSP_MEM identity offset"
if grep -qE 'read_region +0x5000000' "$helper"; then
    note "compressed-mode MB_RSP_MEM offset used as a read address; full mode uses identity"
fi
grep -q '0x768e60' "$helper" || note "missing aspMain image address"

if [ "$failures" -ne 0 ]; then
    echo "p07-memdump host checks: $failures failure(s)" >&2
    exit 1
fi
echo "p07-memdump host checks passed"
