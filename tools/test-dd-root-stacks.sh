#!/bin/sh
#
# Host-only tests for capture-dd-root-stacks.sh.  Every Android-facing command
# and /proc lookup is replaced with a fixture in a private temporary tree.

set -u

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
CAPTURE_SCRIPT=$SCRIPT_DIR/capture-dd-root-stacks.sh
TEST_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/dd-root-stacks-test.XXXXXX") || exit 1
CAPTURE_ROOT=$TEST_ROOT/captures
MOCK_BIN=$TEST_ROOT/mock-bin
PROC_ROOT=$TEST_ROOT/proc
OLD_ROOT=$CAPTURE_ROOT/old-capture
EXPECTED=org.mupen64plusae.turnip.pwnedbygary.debug:EmulationProcess
PASS_COUNT=0
FAIL_COUNT=0

cleanup() {
    rm -rf "$TEST_ROOT"
}
trap cleanup EXIT HUP INT TERM

mkdir -p "$MOCK_BIN" "$PROC_ROOT" "$OLD_ROOT" || exit 1

cat > "$MOCK_BIN/id" <<'EOF'
#!/bin/sh
if [ "$1" = -u ]; then
    printf '%s\n' "${MOCK_UID:-0}"
else
    exit 2
fi
EOF
cat > "$MOCK_BIN/pidof" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >> "$MOCK_PIDOF_CALLS"
call_count=$(wc -l < "$MOCK_PIDOF_CALLS")
pid_list=${MOCK_PID_LIST:-}
if [ "$call_count" -eq 2 ] && [ -n "${MOCK_PID_LIST_2:-}" ]; then
    pid_list=$MOCK_PID_LIST_2
fi
if [ -n "$pid_list" ]; then
    printf '%s\n' "$pid_list"
fi
exit "${MOCK_PID_RC:-0}"
EOF
cat > "$MOCK_BIN/sleep" <<'EOF'
#!/bin/sh
printf '%s\n' "$1" >> "$MOCK_SLEEP_CALLS"
exit "${MOCK_SLEEP_RC:-0}"
EOF
cat > "$MOCK_BIN/timeout" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >> "$MOCK_TIMEOUT_CALLS"
seconds=$1
shift
"$@"
EOF
cat > "$MOCK_BIN/debuggerd" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >> "$MOCK_DEBUGGERD_CALLS"
count=$(wc -l < "$MOCK_DEBUGGERD_CALLS")
if [ "$count" -eq 1 ] && [ -n "${MOCK_DUMP_GATE:-}" ]; then
    : > "$MOCK_DUMP_GATE"
    while [ ! -e "$MOCK_DUMP_RELEASE" ]; do
        /bin/sleep 0.01
    done
fi
if [ "$count" -eq 1 ] && [ -n "${MOCK_AFTER_FIRST_DUMP_STAT:-}" ]; then
    printf '%s\n' "${MOCK_AFTER_FIRST_DUMP_STAT_VALUE:-1 (fixture) S 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 999 0}" \
        > "$MOCK_AFTER_FIRST_DUMP_STAT"
fi
printf '%s\n' "${MOCK_DUMP_TEXT:-fixture native stack}"
exit "${MOCK_DUMP_RC:-0}"
EOF
cat > "$MOCK_BIN/nohup-fail" <<'EOF'
#!/bin/sh
printf 'simulated launcher failure\n' >&2
exit 127
EOF
chmod 755 "$MOCK_BIN"/*

export DD_CAPTURE_ROOT=$CAPTURE_ROOT
export DD_ID_BIN=$MOCK_BIN/id
export DD_PIDOF_BIN=$MOCK_BIN/pidof
export DD_SLEEP_BIN=$MOCK_BIN/sleep
export DD_TIMEOUT_BIN=$MOCK_BIN/timeout
export DD_DEBUGGERD_BIN=$MOCK_BIN/debuggerd
export DD_PROC_ROOT=$PROC_ROOT
export MOCK_UID=0
export MOCK_PID_RC=0
export MOCK_DUMP_RC=0
export MOCK_PID_LIST=
export MOCK_PID_LIST_2=
export MOCK_SLEEP_CALLS=$TEST_ROOT/sleep.calls
export MOCK_TIMEOUT_CALLS=$TEST_ROOT/timeout.calls
export MOCK_DEBUGGERD_CALLS=$TEST_ROOT/debuggerd.calls
export MOCK_PIDOF_CALLS=$TEST_ROOT/pidof.calls
export MOCK_DUMP_GATE=
export MOCK_DUMP_RELEASE=$TEST_ROOT/dump.release
export MOCK_AFTER_FIRST_DUMP_STAT=
export MOCK_AFTER_FIRST_DUMP_STAT_VALUE=

pass() {
    PASS_COUNT=$((PASS_COUNT + 1))
    printf 'PASS %s\n' "$1"
}

fail() {
    FAIL_COUNT=$((FAIL_COUNT + 1))
    printf 'FAIL %s\n' "$1" >&2
}

assert_file_contains() {
    file=$1
    text=$2
    name=$3
    if grep -F -- "$text" "$file" >/dev/null 2>&1; then
        pass "$name"
    else
        fail "$name (missing: $text)"
    fi
}

assert_file_not_contains() {
    file=$1
    text=$2
    name=$3
    if grep -F -- "$text" "$file" >/dev/null 2>&1; then
        fail "$name (unexpected: $text)"
    else
        pass "$name"
    fi
}

assert_text_contains() {
    value=$1
    text=$2
    name=$3
    if printf '%s\n' "$value" | grep -F -- "$text" >/dev/null 2>&1; then
        pass "$name"
    else
        fail "$name (missing: $text)"
    fi
}

assert_equal() {
    actual=$1
    expected=$2
    name=$3
    if [ "$actual" = "$expected" ]; then
        pass "$name"
    else
        fail "$name (got '$actual', expected '$expected')"
    fi
}

reset_calls() {
    : > "$MOCK_SLEEP_CALLS"
    : > "$MOCK_TIMEOUT_CALLS"
    : > "$MOCK_DEBUGGERD_CALLS"
    : > "$MOCK_PIDOF_CALLS"
    rm -f "$MOCK_DUMP_GATE" "$MOCK_DUMP_RELEASE"
    MOCK_DUMP_GATE=
    MOCK_PID_LIST_2=
    MOCK_AFTER_FIRST_DUMP_STAT=
    MOCK_AFTER_FIRST_DUMP_STAT_VALUE=
    export MOCK_DUMP_GATE
}

new_capture() {
    dir=$CAPTURE_ROOT/$1
    mkdir "$dir"
    printf '%s\n' "$dir"
}

make_process() {
    pid=$1
    first_arg=$2
    starttime=${3:-100}
    mkdir -p "$PROC_ROOT/$pid"
    printf '%s\0fixture-argument\0' "$first_arg" > "$PROC_ROOT/$pid/cmdline"
    awk -v pid="$pid" -v starttime="$starttime" \
        'BEGIN { printf "%s (fixture) S", pid; for (i = 4; i <= 21; i++) printf " 0"; printf " %s 0\n", starttime }' \
        > "$PROC_ROOT/$pid/stat"
}

run_worker() {
    # The production shebang is /system/bin/sh; invoke the same script with
    # the host POSIX shell so this test never requires an Android runtime.
    sh "$CAPTURE_SCRIPT" --worker "$1" >/dev/null 2>&1
}

# Root gate must happen before creating the diagnostic root or touching an
# output directory.
ROOT_GATE=$TEST_ROOT/root-gate
MOCK_UID=2000
export MOCK_UID
ROOT_MENU_ERR=$TEST_ROOT/root-menu.err
if DD_CAPTURE_ROOT=$ROOT_GATE sh "$CAPTURE_SCRIPT" > /dev/null 2>"$ROOT_MENU_ERR"; then
    fail 'root gate rejects non-root menu invocation'
else
    pass 'root gate rejects non-root menu invocation'
fi
assert_file_contains "$ROOT_MENU_ERR" 'root required' 'menu root error is explicit'
if [ ! -e "$ROOT_GATE" ]; then
    pass 'root gate has no filesystem side effect'
else
    fail 'root gate has no filesystem side effect'
fi
mkdir -p "$ROOT_GATE/worker-capture"
ROOT_WORKER_ERR=$TEST_ROOT/root-worker.err
if DD_CAPTURE_ROOT=$ROOT_GATE sh "$CAPTURE_SCRIPT" --worker "$ROOT_GATE/worker-capture" \
    > /dev/null 2>"$ROOT_WORKER_ERR"; then
    fail 'root gate rejects non-root worker invocation'
else
    pass 'root gate rejects non-root worker invocation'
fi
assert_file_contains "$ROOT_WORKER_ERR" 'root required' 'worker root error is explicit'
MOCK_UID=0
export MOCK_UID

# A stale output directory is refused instead of being truncated.  This also
# establishes preservation of older capture logs.
mkdir -p "$OLD_ROOT"
printf 'old log must remain\n' > "$OLD_ROOT/native-stacks.txt"
printf 'token=old-token\n' > "$CAPTURE_ROOT/latest-complete"
STALE=$CAPTURE_ROOT/capture-stale
mkdir "$STALE"
printf 'stale metadata\n' > "$STALE/status.txt"
if run_worker "$STALE"; then
    fail 'stale output directory is refused'
else
    pass 'stale output directory is refused'
fi
assert_file_contains "$STALE/status.txt" 'stale metadata' 'stale metadata is preserved'

# Successful run: both samples use a fresh pidof result, exact cmdline first
# argument verification, timeout 20, and exactly two debuggerd -b calls.
reset_calls
make_process 4242 "$EXPECTED"
MOCK_PID_LIST=4242
export MOCK_PID_LIST
SUCCESS=$(new_capture capture-success)
if run_worker "$SUCCESS"; then
    pass 'successful worker exits'
else
    fail 'successful worker exits'
fi
assert_file_contains "$SUCCESS/status.txt" 'state=complete' 'success status is complete'
assert_file_contains "$SUCCESS/run-metadata.txt" "root_uid=0" 'root UID recorded'
assert_file_contains "$SUCCESS/run-metadata.txt" "selected_expected_name=$EXPECTED" 'expected process recorded'
assert_file_contains "$SUCCESS/native-stacks.txt" 'fixture native stack' 'native dump output recorded'
assert_equal "$(wc -l < "$MOCK_PIDOF_CALLS")" 2 'pidof is refreshed for each sample'
assert_equal "$(wc -l < "$MOCK_DEBUGGERD_CALLS")" 2 'two debuggerd calls made'
assert_equal "$(wc -l < "$MOCK_TIMEOUT_CALLS")" 2 'two bounded timeout calls made'
assert_file_contains "$MOCK_TIMEOUT_CALLS" '20 ' 'debuggerd timeout is about 20 seconds'
assert_file_contains "$MOCK_DEBUGGERD_CALLS" '-b 4242' 'debuggerd target is verified PID'
assert_equal "$(wc -l < "$MOCK_SLEEP_CALLS")" 2 'delay and sample gap are bounded calls'
assert_file_contains "$MOCK_SLEEP_CALLS" '90' 'ninety-second return delay recorded'
assert_file_contains "$MOCK_SLEEP_CALLS" '2' 'two-second sample gap recorded'
assert_file_contains "$CAPTURE_ROOT/latest-complete" "capture_dir=$SUCCESS" 'completion marker names successful capture'
assert_file_contains "$OLD_ROOT/native-stacks.txt" 'old log must remain' 'old logs are preserved'

# More than one exact cmdline match is an error, never an implicit first-PID
# choice.
reset_calls
MOCK_PID_LIST='4242 4343'
export MOCK_PID_LIST
make_process 4343 "$EXPECTED" 101
MULTIPLE=$(new_capture capture-multiple)
run_worker "$MULTIPLE"
assert_file_contains "$MULTIPLE/status.txt" 'multiple_exact_process_matches' \
    'multiple exact processes are rejected'
assert_equal "$(wc -l < "$MOCK_DEBUGGERD_CALLS")" 0 \
    'multiple exact processes get no debuggerd call'

# A changed PID between samples is not allowed to produce a second dump.
reset_calls
MOCK_PID_LIST=4242
MOCK_PID_LIST_2=4343
export MOCK_PID_LIST MOCK_PID_LIST_2
CHANGED_PID=$(new_capture capture-changed-pid)
run_worker "$CHANGED_PID"
assert_file_contains "$CHANGED_PID/status.txt" 'pid_changed_from_4242_to_4343' \
    'changed PID identity is rejected'
assert_equal "$(wc -l < "$MOCK_DEBUGGERD_CALLS")" 1 \
    'changed PID gets only the valid first dump'

# A reused PID with a changed /proc starttime is likewise rejected.
reset_calls
MOCK_PID_LIST=4242
export MOCK_PID_LIST
MOCK_AFTER_FIRST_DUMP_STAT=$PROC_ROOT/4242/stat
MOCK_AFTER_FIRST_DUMP_STAT_VALUE='4242 (fixture) S 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 200 0'
export MOCK_AFTER_FIRST_DUMP_STAT MOCK_AFTER_FIRST_DUMP_STAT_VALUE
REUSED_PID=$(new_capture capture-reused-pid)
run_worker "$REUSED_PID"
assert_file_contains "$REUSED_PID/status.txt" 'starttime_changed_for_pid_4242' \
    'reused PID starttime is rejected'
assert_equal "$(wc -l < "$MOCK_DEBUGGERD_CALLS")" 1 \
    'reused PID gets only the valid first dump'

# No initial stat identity means no native dump at all.
reset_calls
MOCK_PID_LIST=4242
export MOCK_PID_LIST
rm -f "$PROC_ROOT/4242/stat"
NO_IDENTITY=$(new_capture capture-no-identity)
run_worker "$NO_IDENTITY"
assert_file_contains "$NO_IDENTITY/status.txt" 'missing_stat_starttime' \
    'missing initial identity is explicit'
assert_equal "$(wc -l < "$MOCK_DEBUGGERD_CALLS")" 0 \
    'missing initial identity gets no debuggerd call'
make_process 4242 "$EXPECTED" 100

# Wrong process: pidof output is not trusted without the immediate /proc
# cmdline first-argument check.
reset_calls
make_process 5001 com.example.other:Process
MOCK_PID_LIST=5001
export MOCK_PID_LIST
WRONG=$(new_capture capture-wrong)
run_worker "$WRONG"
assert_file_contains "$WRONG/status.txt" 'no_verified_exact_process' 'wrong process is rejected'
assert_equal "$(wc -l < "$MOCK_DEBUGGERD_CALLS")" 0 'wrong process gets no debuggerd call'
assert_file_contains "$CAPTURE_ROOT/latest-complete" "capture_dir=$WRONG" 'wrong-process failure publishes completion'

# No PID is an explicit nonfatal error and still completes the worker.
reset_calls
MOCK_PID_LIST=
export MOCK_PID_LIST
NOPID=$(new_capture capture-no-pid)
run_worker "$NOPID"
assert_file_contains "$NOPID/status.txt" 'no_matching_pid' 'no PID is recorded as an error'
assert_file_contains "$NOPID/status.txt" 'state=complete_with_errors' 'no PID still publishes a completed failure'

# A denied debuggerd call is retained, with no rooting or permission
# workaround, and the second sample is still attempted.
reset_calls
MOCK_PID_LIST=4242
MOCK_DUMP_RC=13
export MOCK_PID_LIST MOCK_DUMP_RC
DENIED=$(new_capture capture-denied)
run_worker "$DENIED"
assert_file_contains "$DENIED/status.txt" 'debuggerd_failed_rc=13' 'debuggerd denial is recorded'
assert_equal "$(wc -l < "$MOCK_DEBUGGERD_CALLS")" 2 'denied debuggerd remains nonfatal'
assert_file_contains "$DENIED/native-stacks.txt" 'exit=13' 'denied debuggerd exit is retained'
MOCK_DUMP_RC=0
export MOCK_DUMP_RC

# More than the permitted number of candidates must not grow runtime or call
# debuggerd repeatedly.  None of these candidates has the exact process name.
reset_calls
MOCK_PID_LIST='9001 9002 9003 9004 9005 9006 9007'
export MOCK_PID_LIST
for pid in 9001 9002 9003 9004 9005 9006 9007; do
    make_process "$pid" com.example.wrong
done
BOUNDED=$(new_capture capture-bounded)
run_worker "$BOUNDED"
assert_file_contains "$BOUNDED/status.txt" 'pid_cap_reached=4' 'PID matching is capped'
assert_equal "$(wc -l < "$MOCK_DEBUGGERD_CALLS")" 0 'PID cap prevents unbounded debuggerd calls'
assert_equal "$(wc -l < "$MOCK_PIDOF_CALLS")" 2 'PID cap still makes one bounded lookup per sample'

# The marker remains at its old token while a debuggerd call is in progress,
# then changes only after the complete worker (including both samples) exits.
reset_calls
MOCK_PID_LIST=4242
export MOCK_PID_LIST
MOCK_DUMP_GATE=$TEST_ROOT/dump.gate
export MOCK_DUMP_GATE
GATED=$(new_capture capture-gated)
OLD_MARKER=$(cat "$CAPTURE_ROOT/latest-complete")
sh "$CAPTURE_SCRIPT" --worker "$GATED" >/dev/null 2>&1 &
WORKER_PID=$!
for n in 1 2 3 4 5 6 7 8 9 10; do
    [ -e "$MOCK_DUMP_GATE" ] && break
    /bin/sleep 0.02
done
if [ -e "$MOCK_DUMP_GATE" ] && [ "$(cat "$CAPTURE_ROOT/latest-complete")" = "$OLD_MARKER" ]; then
    pass 'completion marker is not published before capture finishes'
else
    fail 'completion marker is not published before capture finishes'
fi
: > "$MOCK_DUMP_RELEASE"
wait "$WORKER_PID"
NEW_MARKER=$(cat "$CAPTURE_ROOT/latest-complete")
if [ "$NEW_MARKER" != "$OLD_MARKER" ] && [ "$NEW_MARKER" != '' ]; then
    pass 'completion marker changes after worker finishes'
else
    fail 'completion marker changes after worker finishes'
fi
assert_file_contains "$CAPTURE_ROOT/latest-complete" "capture_dir=$GATED" \
    'gated capture is fully published'

# A missing installed script is rejected before a worker directory is made.
MISSING_SCRIPT=$TEST_ROOT/not-installed.sh
MISSING_ERR=$TEST_ROOT/missing-script.err
if DD_SCRIPT_PATH=$MISSING_SCRIPT sh "$CAPTURE_SCRIPT" > /dev/null 2>"$MISSING_ERR"; then
    fail 'unreadable script is rejected before launch'
else
    pass 'unreadable script is rejected before launch'
fi
assert_file_contains "$MISSING_ERR" 'script is not readable' \
    'unreadable script error is explicit'

# A failed detached launch is retained in the narrow per-capture launcher log,
# rather than being discarded while claiming that a worker started.
reset_calls
LAUNCH_FAIL_OUTPUT=$(DD_SCRIPT_PATH=$CAPTURE_SCRIPT DD_SHELL_BIN=sh \
    DD_NOHUP_BIN=$MOCK_BIN/nohup-fail sh "$CAPTURE_SCRIPT" 2>/dev/null)
LAUNCH_FAIL_DIR=$(printf '%s\n' "$LAUNCH_FAIL_OUTPUT" | sed -n 's/^capture_dir=//p')
for n in 1 2 3 4 5; do
    [ -e "$LAUNCH_FAIL_DIR/launcher.log" ] && break
    /bin/sleep 0.02
done
assert_text_contains "$LAUNCH_FAIL_OUTPUT" 'worker_launch_requested=1' \
    'failed launch is only reported as requested'
assert_file_contains "$LAUNCH_FAIL_DIR/launcher.log" 'simulated launcher failure' \
    'detached launch error is retained'

# Exercise the real menu path: it creates a unique directory and launches the
# production worker through nohup/sh without waiting for the 90-second delay.
reset_calls
MOCK_DUMP_GATE=
MOCK_PID_LIST=4242
export MOCK_DUMP_GATE MOCK_PID_LIST
MENU_OUTPUT=$(DD_SCRIPT_PATH=$CAPTURE_SCRIPT DD_SHELL_BIN=sh \
    sh "$CAPTURE_SCRIPT" 2>/dev/null)
MENU_DIR=$(printf '%s\n' "$MENU_OUTPUT" | sed -n 's/^capture_dir=//p')
if [ -n "$MENU_DIR" ] && [ -d "$MENU_DIR" ]; then
    pass 'menu invocation creates a unique capture directory'
else
    fail 'menu invocation creates a unique capture directory'
fi
assert_text_contains "$MENU_OUTPUT" 'worker_launch_requested=1' \
    'menu reports launch request without claiming completion'
assert_file_contains "$MENU_DIR/launcher.log" 'format=ddstart9-launcher-v1' \
    'menu launcher log is created readably'
for n in 1 2 3 4 5 6 7 8 9 10; do
    [ -e "$CAPTURE_ROOT/latest-complete" ] && \
        grep -F "capture_dir=$MENU_DIR" "$CAPTURE_ROOT/latest-complete" >/dev/null 2>&1 && break
    /bin/sleep 0.02
done
assert_file_contains "$CAPTURE_ROOT/latest-complete" "capture_dir=$MENU_DIR" \
    'detached menu worker eventually publishes completion'

printf '\nHost tests: %s passed, %s failed\n' "$PASS_COUNT" "$FAIL_COUNT"
printf 'Fixture files:\n'
find "$CAPTURE_ROOT" -type f -print | sort
if [ "$FAIL_COUNT" -ne 0 ]; then
    exit 1
fi