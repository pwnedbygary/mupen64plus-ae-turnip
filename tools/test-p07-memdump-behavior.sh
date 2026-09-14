#!/usr/bin/env bash
# Behavioral failure-handling checks for the P07 capture helper
# (tools/p07-frozen-memdump.sh) — the evidence-review prompt §6 requires
# testing capture-helper failure handling, not only syntax or matching
# strings. tools/test-p07-memdump.sh keeps the structural invariants; this
# suite runs the helper for real.
#
# Method: the helper is executed with a PATH whose `dd` stubs return chosen
# results, against a REAL target process: a small C harness that maps a
# 608 MiB anonymous region (above the helper's 500 MB mem_base gate) at a
# fixed 64 KiB-aligned address and names it `scudo:secondary` via
# prctl(PR_SET_VMA_ANON_NAME), the pattern the helper's discovery greps for.
# The harness writes two known patterns into the region, so the test's
# expectations are generated independently (from the write spec) rather than
# from the helper. DEST_DIR redirects every output into a temporary
# directory.
#
# Covered: the no-process gate; the canary gate; fail-closed reason
# accumulation (implausible anchor + short/errored window reads) with a
# nonzero exit and no success message; and — where /proc/<pid>/mem is
# readable — the complete-and-verified path with both dumped windows
# byte-compared against the independently generated patterns, the anchor
# word read back, and the target observed stopped. SIGCONT restoration is
# checked after every run.
# Not covered here: the "process not confirmed stopped" reason (it cannot be
# induced through a stub without the helper reading a different file than it
# does); on-device mksh parsing (tools/test-p07-memdump.sh, conditional);
# only the LE host word order is exercised (the device is LE).
set -euo pipefail
cd "$(dirname "$0")/.."

helper=$PWD/tools/p07-frozen-memdump.sh
failures=0
note() { echo "FAIL: $*" >&2; failures=$((failures + 1)); }

CURTASK_OFF_HEX_LC=771d68   # the helper's constant; tools/test-p07-memdump.sh locks it
BASE_HEX=600000000000       # fixed, 64 KiB-aligned, in a range hosts leave free
KSEG0_WORD=806eeaa0

work="$(mktemp -d)"
sleeper=""
cleanup() {
    if [ -n "$sleeper" ]; then kill "$sleeper" 2>/dev/null || true; fi
    rm -rf "$work"
}
trap cleanup EXIT

avail_kb="$(df -Pk "$work" | awk 'NR == 2 {print $4}')"
if [ "${avail_kb:-0}" -lt 65536 ]; then
    echo "SKIP: p07-memdump behavioral checks need ~64 MB free (found ${avail_kb:-0} KB)" >&2
    exit 77
fi

# 1. Target process: maps the region the helper reads, named as the device's
#    allocator names it, with the content the expectations below describe.
cat > "$work/mapharness.c" <<'EOF'
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <unistd.h>

#ifndef PR_SET_VMA
#define PR_SET_VMA 0x53564d41
#define PR_SET_VMA_ANON_NAME 0
#endif
#ifndef PR_SET_PTRACER
#define PR_SET_PTRACER 0x59616d61
#define PR_SET_PTRACER_ANY ((unsigned long)-1)
#endif

int main(int argc, char **argv)
{
    unsigned long base = argc > 1 ? strtoul(argv[1], NULL, 16) : 0x600000000000UL;
    unsigned long curtask = argc > 2 ? strtoul(argv[2], NULL, 16) : 0x771d68UL;
    size_t len = 608u * 1024u * 1024u;
    unsigned char *p;
    size_t i;

    p = mmap((void *)base, len, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (p == MAP_FAILED) { perror("mmap"); return 1; }
    if ((unsigned long)p != base) { fprintf(stderr, "not at the fixed base\n"); return 1; }
    if (prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, (unsigned long)p, len,
              "scudo:secondary") != 0) {
        perror("prctl"); return 1;
    }
    /* /proc/<pid>/mem reads come from dd, a sibling process; yama's
       ptrace_scope=1 would refuse a non-ancestor tracer. This test process
       allows any tracer deliberately (its content is a synthetic pattern). */
    if (prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0) != 0) {
        perror("prctl(PR_SET_PTRACER)"); return 1;
    }
    for (i = 0; i < 0x10000; i++) p[i] = (unsigned char)(i & 0xff);
    memcpy(p + curtask, "\xa0\xea\x6e\x80", 4);          /* 0x806EEAA0, host LE */
    for (i = 0; i < 0x2000; i++) p[0x4000000 + i] = (unsigned char)(0xa5 ^ (i & 0xff));
    printf("READY %p\n", (void *)p);
    fflush(stdout);
    pause();
    return 0;
}
EOF
if ! cc -O0 -o "$work/mapharness" "$work/mapharness.c" 2>"$work/cc.log"; then
    if ! clang -O0 -o "$work/mapharness" "$work/mapharness.c" 2>>"$work/cc.log"; then
        echo "SKIP: no working C compiler for the behavioral harness (see cc.log)" >&2
        exit 77
    fi
fi

# Expected window contents, generated from the harness write spec above.
python3 - "$work/expected-rdram.bin" "$work/expected-rsp.bin" "$CURTASK_OFF_HEX_LC" <<'EOF'
import sys
rd, rsp, off = sys.argv[1], sys.argv[2], int(sys.argv[3], 16)
buf = bytearray(8388608)
for i in range(0x10000):
    buf[i] = i & 0xff
buf[int(off):int(off) + 4] = (0x806EEAA0).to_bytes(4, "little")
with open(rd, "wb") as f:
    f.write(buf)
buf2 = bytearray(8192)
for i in range(8192):
    buf2[i] = 0xa5 ^ (i & 0xff)
with open(rsp, "wb") as f:
    f.write(buf2)
EOF

"$work/mapharness" "$BASE_HEX" "$CURTASK_OFF_HEX_LC" > "$work/sleeper.out" 2>&1 &
sleeper=$!
ready=0
for _ in $(seq 1 100); do
    if grep -q '^READY' "$work/sleeper.out" 2>/dev/null; then ready=1; break; fi
    sleep 0.1
done
if [ "$ready" -ne 1 ]; then
    echo "FAIL: the harness process did not start: $(cat "$work/sleeper.out" 2>/dev/null)" >&2
    exit 1
fi

# 2. Stubs. Modes for dd, selected per test via P07_STUB_MODE:
#      real       - exec the real dd (a working read)
#      partial    - canary read: 4 bytes (passes its length gate); window
#                   dumps: 1 byte; peeks: 'z' (not a KSEG0 word)
#      canaryfail - every call exits 1 without producing data
mkdir -p "$work/stubs" "$work/out"
cat > "$work/stubs/dd" <<'STUB'
#!/usr/bin/env bash
# dd double for the helper. The host's command wrapper rejects hex numbers
# that contain letter digits (e.g. skip=0x561a3acb6000), so in real mode the
# stub normalizes skip=0x... to decimal before exec'ing the real dd. This is
# a host-environment substitution only: on the device, toybox dd accepted the
# helper's hex form unchanged (the 11:39 capture read its 8 MiB window with
# skip=0x<base>).
mode="${P07_STUB_MODE:-real}"
of=""
for a in "$@"; do case "$a" in of=*) of="${a#of=}" ;; esac; done
case "$mode" in
    real)
        args=()
        for a in "$@"; do
            case "$a" in
                skip=0x*|skip=0X*)
                    h="${a#skip=0x}"; h="${h#skip=0X}"
                    args+=("skip=$(printf '%d' "0x$h")") ;;
                *) args+=("$a") ;;
            esac
        done
        exec /usr/bin/dd "${args[@]}" ;;
    partial)
        case "$of" in
            *canary-sample.bin) printf 'zzzz' > "$of" ;;
            "") printf 'z' ;;
            *) printf 'z' > "$of" ;;
        esac
        exit 0 ;;
    canaryfail) exit 1 ;;
esac
STUB
cat > "$work/stubs/sha256sum" <<'STUB'
#!/usr/bin/env bash
# sha256sum double: P07_STUB_SHA=fail models a hashing step that produces
# nothing, which must degrade the capture instead of letting it verify.
if [ "${P07_STUB_SHA:-real}" = "fail" ]; then
    exit 1
fi
exec /usr/bin/sha256sum "$@"
STUB
cat > "$work/stubs/pidof" <<'STUB'
#!/usr/bin/env bash
# Test 3 uses this stub; tests 4+ override it with the live PID.
exit 0
STUB
chmod +x "$work/stubs"/*

run_helper() {  # run_helper <dd-mode> [sha-mode]
    env PATH="$work/stubs:$PATH" DEST_DIR="$work/out" P07_STUB_MODE="$1" \
        P07_STUB_SHA="${2:-real}" \
        sh "$helper" > "$work/stdout.txt" 2> "$work/stderr.txt"
}

last_dir() { ls -d "$work/out"/p07-memdump-* 2>/dev/null | tail -1 || true; }

check_restored() {  # the EXIT trap must have resumed the target
    if grep -qE '^State:[[:space:]]+T' "/proc/$sleeper/status" 2>/dev/null; then
        note "$1: SIGCONT not restored (target still stopped)"
    fi
}

# 3. No-process gate: the runner reports the absence and exits nonzero
#    without creating an output directory.
before_dirs="$(ls -d "$work/out"/p07-memdump-* 2>/dev/null | wc -l || true)"
if run_helper real; then
    note "no-process gate: helper exited 0 with no target process"
fi
grep -q 'reproduce the freeze first' "$work/stdout.txt" \
    || note "no-process gate: absence not reported"
after_dirs="$(ls -d "$work/out"/p07-memdump-* 2>/dev/null | wc -l || true)"
[ "$before_dirs" -eq "$after_dirs" ] \
    || note "no-process gate: an output directory was created anyway"

# Point the pidof stub at the live harness process for the remaining tests.
printf '#!/usr/bin/env bash\necho %s\nexit 0\n' "$sleeper" > "$work/stubs/pidof"
chmod +x "$work/stubs/pidof"

# 4. Canary gate: an unreadable /proc/<pid>/mem (dd fails, no data) must stop
#    the run with a message that names the failure, before any window read.
#    The run directory is still created (failure evidence is preserved); what
#    must not exist is window data or a success message.
if run_helper canaryfail; then
    note "canary gate: helper exited 0 after a failed canary read"
fi
grep -q '/proc/.*/mem read failed' "$work/stdout.txt" \
    || note "canary gate: failure not reported on stdout"
if grep -q 'complete (verified)' "$work/stdout.txt"; then
    note "canary gate: success message printed for a failed read"
fi
dir="$(last_dir)"
if [ -n "$dir" ]; then
    [ -f "$dir/canary-stderr.txt" ] \
        || note "canary gate: dd stderr not preserved as evidence"
    [ ! -e "$dir/rdram-window.bin" ] \
        || note "canary gate: a window file exists despite the failed canary"
else
    note "canary gate: no run directory created for the failed capture"
fi
check_restored "canary gate"

# 5. Fail-closed reason accumulation: a plausible canary but a non-KSEG0
#    anchor word and short window reads must exit nonzero, print no success
#    message, and record every degradation reason.
rm -rf "$work/out"/* 2>/dev/null || true
if run_helper partial; then
    note "incomplete capture: helper exited 0 despite degraded reads"
fi
grep -q 'INCOMPLETE' "$work/stdout.txt" \
    || note "incomplete capture: INCOMPLETE not reported"
if grep -q 'complete (verified)' "$work/stdout.txt"; then
    note "incomplete capture: success message printed for a degraded capture"
fi
dir="$(last_dir)"
if [ -n "$dir" ] && [ -f "$dir/run-metadata.txt" ]; then
    grep -q 'curtask_anchor_implausible' "$dir/run-metadata.txt" \
        || note "incomplete capture: anchor reason not recorded"
    grep -q 'read_rdram-window.bin_rc' "$dir/run-metadata.txt" \
        || note "incomplete capture: window read reason not recorded"
    grep -q 'status=incomplete' "$dir/run-metadata.txt" \
        || note "incomplete capture: status line not recorded"
else
    note "incomplete capture: no run-metadata.txt to inspect"
fi
check_restored "incomplete capture"

# 5b. Resume consent: a process that was already stopped externally must be
#     left stopped — this run did not stop it, so it must not resume it. The
#     partial-mode double keeps the helper's stop/consent logic on the real
#     path while the reads degrade.
kill -STOP "$sleeper" 2>/dev/null
sleep 0.2
rm -rf "$work/out"/* 2>/dev/null || true
if run_helper partial; then
    note "resume consent: helper exited 0 for an externally stopped process"
fi
dir="$(last_dir)"
if [ -n "$dir" ] && [ -f "$dir/run-metadata.txt" ]; then
    grep -q 'stopped_before_state=T' "$dir/run-metadata.txt" \
        || note "resume consent: pre-stop state not recorded"
else
    note "resume consent: no run-metadata.txt to inspect"
fi
if grep -qE '^State:[[:space:]]+T' "/proc/$sleeper/status" 2>/dev/null; then
    : # correct: the helper did not resume a process it had not stopped
else
    note "resume consent: helper resumed a process it did not stop"
fi
kill -CONT "$sleeper" 2>/dev/null
sleep 0.2
if grep -qE '^State:[[:space:]]+T' "/proc/$sleeper/status" 2>/dev/null; then
    note "resume consent: the test could not resume its own harness"
fi

# 6. The real-read path needs /proc/<pid>/mem access to a live process. On a
#    host where that is denied (e.g. a sandboxed shell), the check is
#    reported as SKIP, never silently passed. The probe passes the address in
#    decimal for the same host-wrapper reason the stub documents.
skipped=0
probe_ok=0
addr="$(head -1 "/proc/$sleeper/maps" 2>/dev/null | cut -d- -f1)"
dec="$(printf '%d' "0x$addr" 2>/dev/null || echo 0)"
if [ -n "$addr" ] && [ "$dec" != "0" ] && /usr/bin/dd if="/proc/$sleeper/mem" \
        iflag=skip_bytes,count_bytes skip="$dec" count=4 \
        of="$work/probe.bin" 2>"$work/probe.err"; then
    [ "$(stat -c %s "$work/probe.bin" 2>/dev/null)" = "4" ] && probe_ok=1
fi
if [ "$probe_ok" -ne 1 ]; then
    echo "SKIP: real /proc/<pid>/mem reads are unavailable here ($(head -1 "$work/probe.err" 2>/dev/null)); the verified-capture check needs them" >&2
    skipped=1
fi

# 6b. A capture whose hashing step produced nothing must degrade, never
#     verify (dropping the hashes_missing reason used to pass every suite).
if [ "$probe_ok" -eq 1 ]; then
    rm -rf "$work/out"/* 2>/dev/null || true
    if run_helper real fail; then
        note "hash-loss capture: helper exited 0 with no hashes written"
    fi
    grep -q 'INCOMPLETE' "$work/stdout.txt" \
        || note "hash-loss capture: INCOMPLETE not reported"
    if grep -q 'complete (verified)' "$work/stdout.txt"; then
        note "hash-loss capture: success message printed without hashes"
    fi
    dir="$(last_dir)"
    if [ -n "$dir" ] && [ -f "$dir/run-metadata.txt" ]; then
        grep -q 'hashes_missing' "$dir/run-metadata.txt" \
            || note "hash-loss capture: hashes_missing reason not recorded"
        [ -f "$dir/rdram-window.bin" ] \
            || note "hash-loss capture: evidence windows were not preserved"
    else
        note "hash-loss capture: no run-metadata.txt to inspect"
    fi
    check_restored "hash-loss capture"
fi

# 7. Complete path with real reads: the anchored capture must verify, exit 0,
#    and both dumped windows must byte-match the independently generated
#    patterns at the right offsets (8 MiB from the base; 8 KiB at
#    base + 0x04000000).
if [ "$probe_ok" -eq 1 ]; then
    rm -rf "$work/out"/* 2>/dev/null || true
    if ! run_helper real; then
        note "verified capture: helper exited nonzero on a healthy run: $(cat "$work/stdout.txt")"
    fi
    grep -q 'complete (verified)' "$work/stdout.txt" \
        || note "verified capture: success message missing"
    dir="$(last_dir)"
    if [ -n "$dir" ] && [ -f "$dir/run-metadata.txt" ]; then
        grep -q 'status=verified' "$dir/run-metadata.txt" \
            || note "verified capture: status=verified not recorded"
        grep -q "curtask_before_hex=$KSEG0_WORD" "$dir/run-metadata.txt" \
            || note "verified capture: the anchor word was not read back from the target"
        grep -q 'stopped_state=T' "$dir/run-metadata.txt" \
            || note "verified capture: the target was not observed stopped"
        cmp -s "$work/expected-rdram.bin" "$dir/rdram-window.bin" \
            || note "verified capture: rdram window differs from the expected pattern"
        cmp -s "$work/expected-rsp.bin" "$dir/rspmem.bin" \
            || note "verified capture: rsp window differs from the expected pattern"
    else
        note "verified capture: no run-metadata.txt to inspect"
    fi
    check_restored "verified capture"
fi

if [ "$failures" -ne 0 ]; then
    echo "p07-memdump behavioral checks: $failures failure(s)" >&2
    exit 1
fi
if [ "$skipped" -ne 0 ]; then
    echo "p07-memdump behavioral checks: stub-gate checks passed; real-read check SKIPPED" >&2
    exit 77
fi
echo "p07-memdump behavioral checks passed"
