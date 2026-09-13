#!/bin/bash

set -u

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
ORCHESTRATOR="$SCRIPT_DIR/capture-dd-rsp-mac.sh"
TEST_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/dd-rsp-mac-test.XXXXXX") || exit 1
trap 'rm -rf "$TEST_ROOT"' EXIT HUP INT TERM

PASS_COUNT=0
FAIL_COUNT=0

pass() {
    printf 'PASS %s\n' "$1"
    PASS_COUNT=$((PASS_COUNT + 1))
}

fail() {
    printf 'FAIL %s\n' "$1"
    FAIL_COUNT=$((FAIL_COUNT + 1))
}

assert_file_contains() {
    local file=$1
    local text=$2
    local description=$3
    if grep -F "$text" "$file" >/dev/null 2>&1; then
        pass "$description"
    else
        fail "$description (missing: $text)"
    fi
}

assert_file_exists() {
    local file=$1
    local description=$2
    if [ -e "$file" ]; then
        pass "$description"
    else
        fail "$description (missing: $file)"
    fi
}

FAKE_ADB="$TEST_ROOT/fake-adb"
FAKE_SHASUM="$TEST_ROOT/fake-shasum"
FAKE_DITTO="$TEST_ROOT/fake-ditto"
cat > "$FAKE_ADB" <<'EOF'
#!/bin/bash
set -u
printf 'adb' >> "$FAKE_ADB_LOG"
for arg in "$@"; do printf ' <%s>' "$arg" >> "$FAKE_ADB_LOG"; done
printf '\n' >> "$FAKE_ADB_LOG"
case "${1:-}" in
    install)
        exit 0
        ;;
    logcat)
        printf '%s\n' "$*" > "$FAKE_LOGCAT_ARGS"
        printf '%s\n' "$$" > "$FAKE_LOGCAT_PID"
        trap 'rm -f "$FAKE_LOGCAT_PID"; exit 0' TERM INT
        printf '09-13 00:00:00.000  1  1 I Core: fixture logcat\n'
        while :; do /bin/sleep 0.05; done
        ;;
    shell)
        command_text=${2:-}
        if [[ "$command_text" == *'for d in'* ]]; then
            count=$(cat "$FAKE_SNAPSHOT_COUNT")
            count=$((count + 1))
            printf '%s\n' "$count" > "$FAKE_SNAPSHOT_COUNT"
            printf '%s/capture.OLD\n' "$FAKE_REMOTE_ROOT"
            printf '%s/latest-complete\n' "$FAKE_REMOTE_ROOT"
            if [ "${FAKE_MODE:-success}" = success ] && [ "$count" -ge 2 ]; then
                printf '%s/capture.NEW\n' "$FAKE_REMOTE_ROOT"
            elif [ "${FAKE_MODE:-success}" = timeout ] && [ "$count" -ge 2 ]; then
                printf '%s/capture.NEW\n' "$FAKE_REMOTE_ROOT"
            fi
            exit 0
        fi
        if [[ "$command_text" == *status.txt* ]]; then
            count=$(cat "$FAKE_STATUS_COUNT")
            count=$((count + 1))
            printf '%s\n' "$count" > "$FAKE_STATUS_COUNT"
            if [ "${FAKE_MODE:-success}" = success ] && [ "$count" -ge 2 ]; then
                printf 'state=complete\nfinished_at=fixture\n'
            else
                printf 'state=running\n'
            fi
            exit 0
        fi
        if [[ "$command_text" == *root-launch.log* ]]; then
            printf 'fixture root launch output\n'
            exit 0
        fi
        exit 1
        ;;
    pull)
        remote=$2
        destination=$3
        name=${remote##*/}
        mkdir -p "$destination/$name"
        printf 'state=complete\nfixture=partial-or-complete\n' > "$destination/$name/status.txt"
        printf 'fixture capture for %s\n' "$name" > "$destination/$name/native-stacks.txt"
        exit 0
        ;;
esac
exit 1
EOF
cat > "$FAKE_SHASUM" <<'EOF'
#!/bin/bash
/usr/bin/shasum "$@"
EOF
cat > "$FAKE_DITTO" <<'EOF'
#!/bin/bash
destination=${@: -1}
printf 'fixture zip\n' > "$destination"
exit 0
EOF
chmod +x "$FAKE_ADB" "$FAKE_SHASUM" "$FAKE_DITTO"

APK="$TEST_ROOT/DDSTART10-debug.apk"
printf 'fixture APK bytes\n' > "$APK"
EXPECTED_SHA=$(shasum -a 256 "$APK" | awk '{print $1}')

run_case() {
    local mode=$1
    local home=$TEST_ROOT/home-$mode
    local remote="$TEST_ROOT/remote-$mode"
    local log="$TEST_ROOT/$mode.stdout"
    mkdir -p "$home/Desktop"
    : > "$FAKE_ADB_LOG"
    : > "$FAKE_SNAPSHOT_COUNT"
    : > "$FAKE_STATUS_COUNT"
    : > "$FAKE_LOGCAT_ARGS"
    rm -f "$FAKE_LOGCAT_PID"
    (
        export HOME="$home"
        export FAKE_MODE="$mode"
        export FAKE_REMOTE_ROOT="$remote"
        export FAKE_ADB_LOG
        export FAKE_SNAPSHOT_COUNT
        export FAKE_STATUS_COUNT
        export FAKE_LOGCAT_ARGS
        export FAKE_LOGCAT_PID
        export DD_RSP_MAC_TEST_MODE=1
        export DD_RSP_MAC_TEST_ADB="$FAKE_ADB"
        export DD_RSP_MAC_TEST_SHA="$FAKE_SHASUM"
        export DD_RSP_MAC_TEST_DITTO="$FAKE_DITTO"
        export DD_RSP_MAC_TEST_CAPTURE_ROOT="$remote"
        export DD_RSP_MAC_TEST_ROOT_LAUNCH="$remote/root-launch.log"
        export DD_RSP_MAC_TEST_CAPTURE_WAIT=2
        export DD_RSP_MAC_TEST_STATUS_WAIT=2
        export DD_RSP_MAC_TEST_POLL_INTERVAL=1
        export DD_RSP_MAC_TEST_SKIP_PROMPT=1
        printf '\n' | "$ORCHESTRATOR" "$APK" "$EXPECTED_SHA"
    ) > "$log" 2>&1
    return $?
}

export FAKE_ADB_LOG="$TEST_ROOT/adb.log"
export FAKE_SNAPSHOT_COUNT="$TEST_ROOT/snapshot.count"
export FAKE_STATUS_COUNT="$TEST_ROOT/status.count"
export FAKE_LOGCAT_ARGS="$TEST_ROOT/logcat.args"
export FAKE_LOGCAT_PID="$TEST_ROOT/logcat.pid"

if run_case success; then
    pass 'successful fresh-capture orchestration exits zero'
else
    fail 'successful fresh-capture orchestration exits zero'
fi
SUCCESS_DIR=$(find "$TEST_ROOT/home-success/Desktop" -maxdepth 1 -type d -name 'ddstart10-rsp.*' | head -1)
assert_file_contains "$FAKE_ADB_LOG" ' <install> <-r>' 'APK uses install -r'
if grep -E 'uninstall|clear|pm clear' "$FAKE_ADB_LOG" >/dev/null 2>&1; then
    fail 'ADB flow has no uninstall or clear-data operation'
else
    pass 'ADB flow has no uninstall or clear-data operation'
fi
assert_file_contains "$FAKE_LOGCAT_ARGS" 'logcat -v threadtime -T 1 Core:V *:S' \
    'bounded tagged logcat starts before polling'
assert_file_exists "$SUCCESS_DIR/retrieved/capture.NEW/native-stacks.txt" \
    'new capture is retrieved instead of old capture/global marker'
if [ -e "$SUCCESS_DIR/retrieved/capture.OLD" ]; then
    fail 'old capture/global marker is not selected'
else
    pass 'old capture/global marker is not selected'
fi
assert_file_contains "$SUCCESS_DIR/root-launch.log" 'fixture root launch output' \
    'root launch log is retrieved'
assert_file_exists "$SUCCESS_DIR.zip" 'successful run creates ditto ZIP'
if [ -e "$FAKE_LOGCAT_PID" ]; then
    fail 'cleanup stops only the orchestrator logcat'
else
    pass 'cleanup stops only the orchestrator logcat'
fi

if run_case timeout; then
    fail 'status timeout is reported nonzero'
else
    pass 'status timeout is reported nonzero'
fi
TIMEOUT_DIR=$(find "$TEST_ROOT/home-timeout/Desktop" -maxdepth 1 -type d -name 'ddstart10-rsp.*' | head -1)
assert_file_contains "$TEST_ROOT/timeout.stdout" 'retrieving exact folder as partial' \
    'status timeout is explicit'
assert_file_exists "$TIMEOUT_DIR/retrieved/capture.NEW/native-stacks.txt" \
    'status timeout preserves available exact capture'
assert_file_exists "$TIMEOUT_DIR/root-launch.log" \
    'status timeout preserves root launch log'
assert_file_exists "$TIMEOUT_DIR.zip" 'timeout run still creates ditto ZIP'

printf '\nHost tests: %s passed, %s failed\n' "$PASS_COUNT" "$FAIL_COUNT"
exit "$FAIL_COUNT"