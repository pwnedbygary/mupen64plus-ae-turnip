#!/usr/bin/env bash
# Host checks for the P07 capture verifier (tools/verify-p07-captures.py):
# the fail-closed contract of docs/N64DD_EVIDENCE_REVIEW_PROMPT.md §6 —
# a verifier must fail (or explicitly label) missing, mutated, truncated
# and mis-analyzed evidence, and must never print an unconditional overall
# PASS after skipped or failed gates.
#
# Fixture checks assemble the verifier's flat layout from the local raw
# captures (.fzxwork/p07-final/, gitignored, never published; override the
# root with P07_CAPTURE_ROOT). Without local captures the fixture checks
# SKIP with exit 77 — a skip is not a pass. Structural checks always run.
set -euo pipefail
cd "$(dirname "$0")/.."

verifier=tools/verify-p07-captures.py
failures=0

note() { echo "FAIL: $*" >&2; failures=$((failures + 1)); }

# 1. Structural checks (no captures needed): the verifier must fail closed
#    on missing required files before reading anything, aggregate every
#    failure into a nonzero exit, and label the optional sample-series
#    gates with an explicit SKIP rather than silent success.
python3 -c 'import ast,sys; ast.parse(open(sys.argv[1]).read())' "$verifier" \
    || note "verifier fails to parse"
for need in \
    'missing capture files' \
    'return 2' \
    'return 1 if FAILURES else 0' \
    'SKIP  sample-series checks' \
    'SKIP  P06 logcat checks'; do
    grep -qF "$need" "$verifier" || note "verifier missing fail-closed element: $need"
done
# Skipped groups must be reported on the result line, not just printed.
grep -qF 'skipped check group(s)' "$verifier" \
    || note "verifier result line does not report skipped groups"
gate_line="$(grep -n 'missing capture files' "$verifier" | head -1 | cut -d: -f1)"
load_line="$(grep -n 'caps.load(' "$verifier" | head -1 | cut -d: -f1)"
if [ -z "$gate_line" ] || [ -z "$load_line" ] || [ "$gate_line" -ge "$load_line" ]; then
    note "missing-file gate must precede the first capture load"
fi

# 2. Fixture checks: locate the local raw captures.
root="${P07_CAPTURE_ROOT:-.fzxwork/p07-final}"
have_all=1
for f in \
    p07-memdump-20260914-113959/rdram-window.bin \
    p07-memdump-20260914-113959/rspmem.bin \
    imem-recapture/rspmem-rerun.bin \
    recapture2/rdram-window.bin \
    recapture2/rspmem.bin \
    recapture3/rdram-window.bin \
    recapture3/rspmem.bin; do
    [ -f "$root/$f" ] || have_all=0
done
for i in $(seq 1 10); do [ -f "$root/dmem-series/dmem-$i.bin" ] || have_all=0; done
for i in $(seq 1 20); do [ -f "$root/dmem-rapid/r-$i.bin" ] || have_all=0; done

if [ "$have_all" -ne 1 ]; then
    echo "p07-verifier host checks: structural only, fixture checks SKIP" \
         "(local captures not under $root; set P07_CAPTURE_ROOT)"
    [ "$failures" -eq 0 ] || exit 1
    exit 77
fi

fixture="$(mktemp -d)"
trap 'rm -rf "$fixture"' EXIT
restore() { cp "$root/$1" "$fixture/$2"; }
cp "$root/p07-memdump-20260914-113959/rdram-window.bin" "$fixture/rdram-window-113959.bin"
cp "$root/p07-memdump-20260914-113959/rspmem.bin"        "$fixture/rspmem-113959.bin"
cp "$root/imem-recapture/rspmem-rerun.bin"               "$fixture/rspmem-rerun-1201.bin"
cp "$root/recapture2/rdram-window.bin"                   "$fixture/rdram-window-1202.bin"
cp "$root/recapture2/rspmem.bin"                         "$fixture/rspmem-1202.bin"
cp "$root/recapture3/rdram-window.bin"                   "$fixture/rdram-window-1427.bin"
cp "$root/recapture3/rspmem.bin"                         "$fixture/rspmem-1427.bin"
for i in $(seq 1 10); do
    cp "$root/dmem-series/dmem-$i.bin" "$(printf '%s/dmem-series-%02d.bin' "$fixture" "$i")"
done
for i in $(seq 1 20); do
    cp "$root/dmem-rapid/r-$i.bin" "$(printf '%s/dmem-rapid-%02d.bin' "$fixture" "$i")"
done
# The P06 logcat drives four crucial checks; copy it when the local capture is
# available so those gates actually run here instead of skipping.
logcat="${P07_LOGCAT:-.fzxwork/p06-capture/logcat-p06-route.txt}"
have_logcat=0
if [ -f "$logcat" ]; then
    cp "$logcat" "$fixture/logcat-p06-route.txt"
    have_logcat=1
fi

run_verifier() {
    _out="$(python3 "$verifier" "$fixture" 2>&1)" && _rc=0 || _rc=$?
}

# 2a. Pristine fixture: every gate passes and the overall result says so.
run_verifier
[ "$_rc" -eq 0 ] || note "pristine fixture: exit $_rc (want 0)"
printf '%s\n' "$_out" | grep -qF 'RESULT: 0 failure(s)' \
    || note "pristine fixture: missing 'RESULT: 0 failure(s)'"
if [ "$have_logcat" -eq 1 ]; then
    printf '%s\n' "$_out" | grep -qF 'RESULT: 0 failure(s), 0 skipped check group(s)' \
        || note "pristine fixture: skipped groups not reported on the result line"
    if printf '%s\n' "$_out" | grep -qF 'SKIP  '; then
        note "pristine fixture: a group skipped while all evidence was present"
    fi
else
    printf '%s\n' "$_out" | grep -qF 'SKIP  P06 logcat checks' \
        || note "logcat-absent fixture: no explicit SKIP for the logcat group"
    printf '%s\n' "$_out" | grep -qF 'RESULT: 0 failure(s), 1 skipped check group(s)' \
        || note "logcat-absent fixture: skipped group not reported on the result line"
fi

# 2b. The raw/ subdirectory layout is accepted as an alternative to flat files.
mkdir "$fixture/raw"
mv "$fixture"/*.bin "$fixture/raw/"
run_verifier
[ "$_rc" -eq 0 ] || note "raw/ layout: exit $_rc (want 0)"
mv "$fixture/raw"/*.bin "$fixture/"
rmdir "$fixture/raw"

# 2c. A missing REQUIRED file: exit 2 with the file named, before any read.
rm "$fixture/rdram-window-1427.bin"
run_verifier
[ "$_rc" -eq 2 ] || note "missing-file fixture: exit $_rc (want 2)"
printf '%s\n' "$_out" | grep -qF 'missing capture files: rdram-window-1427.bin' \
    || note "missing-file fixture: file not named in the failure"
restore recapture3/rdram-window.bin rdram-window-1427.bin

# 2d. A mutated byte in a hash-tracked file: exit 1, FAIL names the file,
#     and no overall success line is printed.
printf 'Z' | dd of="$fixture/rspmem-1427.bin" bs=1 seek=64 conv=notrunc status=none
run_verifier
[ "$_rc" -eq 1 ] || note "mutated-byte fixture: exit $_rc (want 1)"
printf '%s\n' "$_out" | grep -qF 'FAIL  sha256 rspmem-1427.bin' \
    || note "mutated-byte fixture: identity gate did not catch the change"
printf '%s\n' "$_out" | grep -qF 'RESULT: 0 failure(s)' \
    && note "mutated-byte fixture: overall PASS printed despite a FAIL"
restore recapture3/rspmem.bin rspmem-1427.bin

# 2e. A truncated window: caught with exit 1 (short reads must not pass).
truncate -s -16 "$fixture/rdram-window-1202.bin"
run_verifier
[ "$_rc" -eq 1 ] || note "truncated-window fixture: exit $_rc (want 1)"
printf '%s\n' "$_out" | grep -qF 'FAIL  sha256 rdram-window-1202.bin' \
    || note "truncated-window fixture: not reported as a failure"
restore recapture2/rdram-window.bin rdram-window-1202.bin

# 2f. Missing OPTIONAL evidence (the sample series): explicit SKIP label,
#     required gates still pass, overall line still honest.
rm "$fixture"/dmem-series-*.bin "$fixture"/dmem-rapid-*.bin
run_verifier
[ "$_rc" -eq 0 ] || note "optional-evidence fixture: exit $_rc (want 0)"
printf '%s\n' "$_out" | grep -qF 'SKIP  sample-series checks' \
    || note "optional-evidence fixture: no explicit SKIP label"
printf '%s\n' "$_out" | grep -qF 'RESULT: 0 failure(s)' \
    || note "optional-evidence fixture: required gates did not pass"
want_skips=1
[ "$have_logcat" -eq 1 ] || want_skips=2
printf '%s\n' "$_out" | grep -qF "RESULT: 0 failure(s), $want_skips skipped check group(s)" \
    || note "optional-evidence fixture: expected $want_skips skipped group(s) on the result line"
for i in $(seq 1 10); do
    cp "$root/dmem-series/dmem-$i.bin" "$(printf '%s/dmem-series-%02d.bin' "$fixture" "$i")"
done
for i in $(seq 1 20); do
    cp "$root/dmem-rapid/r-$i.bin" "$(printf '%s/dmem-rapid-%02d.bin' "$fixture" "$i")"
done

# 2f2. Removing the logcat must announce the four affected gates as skipped
#      and count them — the group must never vanish silently into a passing run.
if [ "$have_logcat" -eq 1 ]; then
    mv "$fixture/logcat-p06-route.txt" "$fixture/logcat.saved"
    run_verifier
    [ "$_rc" -eq 0 ] || note "logcat-removed fixture: exit $_rc (want 0)"
    printf '%s\n' "$_out" | grep -qF 'SKIP  P06 logcat checks' \
        || note "logcat-removed fixture: no explicit SKIP line"
    printf '%s\n' "$_out" | grep -qF 'RESULT: 0 failure(s), 1 skipped check group(s)' \
        || note "logcat-removed fixture: skipped group not counted on the result line"
    mv "$fixture/logcat.saved" "$fixture/logcat-p06-route.txt"
else
    echo "SKIP: the logcat-removal case needs a local P06 logcat (set P07_LOGCAT)" >&2
fi

# 2g. Analytical gate: the sample files carry no hash expectations, so a
#     reclassified sample must be caught by the section-G content checks,
#     not by the identity gate. dmem-series-01 is a BLOCK-class sample
#     (1024 non-zero words); zeroing it changes the class counts
#     (zeroed 14 -> 15 at minimum), which must FAIL with exit 1.
dd if=/dev/zero of="$fixture/dmem-series-01.bin" bs=4096 count=1 status=none
run_verifier
[ "$_rc" -eq 1 ] || note "analytical fixture: exit $_rc (want 1)"
printf '%s\n' "$_out" | grep -q 'FAIL  ' \
    || note "analytical fixture: no FAIL despite the reclassified sample"
printf '%s\n' "$_out" | grep -q 'FAIL  sha256' \
    && note "analytical fixture: caught at the identity gate, not analytically"

if [ "$failures" -ne 0 ]; then
    echo "p07-verifier host checks: $failures failure(s)" >&2
    exit 1
fi
echo "p07-verifier host checks passed (structural + fixture)"
