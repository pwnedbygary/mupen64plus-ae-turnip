#!/bin/bash
#
# DDSTART10 host-side capture orchestrator for macOS.
# It installs the supplied APK, starts a narrowly filtered logcat, and waits
# for the user to run the already-installed root entry and reproduce the stall.

set -u
export LC_ALL=C

ADB="$HOME/Downloads/platform-tools/adb"
APK_DEFAULT="$HOME/Downloads/DDSTART10-debug.apk"
REMOTE_CAPTURE_ROOT="/sdcard/Download/ddstart9-root-capture"
REMOTE_ROOT_LAUNCH="/sdcard/Download/ddstart9-root-launch.log"
CAPTURE_WAIT_SECONDS=600
STATUS_WAIT_SECONDS=360
POLL_INTERVAL_SECONDS=5
SHA_BIN=shasum
DITTO_BIN=ditto
SLEEP_BIN=sleep
TEST_MODE=${DD_RSP_MAC_TEST_MODE:-0}

if [ "$TEST_MODE" = 1 ]; then
    ADB=${DD_RSP_MAC_TEST_ADB:-$ADB}
    SHA_BIN=${DD_RSP_MAC_TEST_SHA:-$SHA_BIN}
    DITTO_BIN=${DD_RSP_MAC_TEST_DITTO:-$DITTO_BIN}
    SLEEP_BIN=${DD_RSP_MAC_TEST_SLEEP:-$SLEEP_BIN}
    REMOTE_CAPTURE_ROOT=${DD_RSP_MAC_TEST_CAPTURE_ROOT:-$REMOTE_CAPTURE_ROOT}
    REMOTE_ROOT_LAUNCH=${DD_RSP_MAC_TEST_ROOT_LAUNCH:-$REMOTE_ROOT_LAUNCH}
    CAPTURE_WAIT_SECONDS=${DD_RSP_MAC_TEST_CAPTURE_WAIT:-$CAPTURE_WAIT_SECONDS}
    STATUS_WAIT_SECONDS=${DD_RSP_MAC_TEST_STATUS_WAIT:-$STATUS_WAIT_SECONDS}
    POLL_INTERVAL_SECONDS=${DD_RSP_MAC_TEST_POLL_INTERVAL:-$POLL_INTERVAL_SECONDS}
fi

APK_PATH=${1:-$APK_DEFAULT}
EXPECTED_SHA=${2:-}

RUN_DIR=
RUN_LOG=
ZIP_PATH=
LOGCAT_FILE=
LOGCAT_PID=0
EXIT_CODE=0
BASELINE_FILE=
FRESH_FILE=
CURRENT_FILE=
STATUS_TMP=
PULL_ROOT=
ADB_ERRORS=

log() {
    printf '%s\n' "$*" >> "$RUN_LOG"
    printf '%s\n' "$*"
}

error() {
    printf 'ERROR: %s\n' "$*" >> "$RUN_LOG"
    printf 'ERROR: %s\n' "$*" >&2
}

warn() {
    printf 'WARNING: %s\n' "$*" >> "$RUN_LOG"
    printf 'WARNING: %s\n' "$*" >&2
}

fatal() {
    error "$1"
    EXIT_CODE=1
    exit 1
}

cleanup() {
    trap - EXIT
    if [ "$LOGCAT_PID" -ne 0 ]; then
        kill "$LOGCAT_PID" 2>/dev/null || :
        wait "$LOGCAT_PID" 2>/dev/null || :
        LOGCAT_PID=0
    fi
    if [ -n "$RUN_DIR" ] && [ -d "$RUN_DIR" ]; then
        if ! "$DITTO_BIN" -c -k --sequesterRsrc --keepParent "$RUN_DIR" "$ZIP_PATH" \
            >> "$RUN_LOG" 2>&1; then
            printf 'ERROR: could not create diagnostic ZIP; partial files remain at %s\n' \
                "$RUN_DIR" >> "$RUN_LOG"
            printf 'ERROR: could not create diagnostic ZIP; partial files remain at %s\n' \
                "$RUN_DIR" >&2
            [ "$EXIT_CODE" -eq 0 ] && EXIT_CODE=1
        fi
    fi
    exit "$EXIT_CODE"
}

on_signal() {
    EXIT_CODE=130
    exit 130
}

trap cleanup EXIT
trap on_signal INT TERM

if [ "$#" -gt 2 ]; then
    printf 'Usage: %s [APK_PATH] EXPECTED_SHA256\n' "$0" >&2
    exit 2
fi
if [ -z "$EXPECTED_SHA" ]; then
    printf 'ERROR: expected APK SHA-256 is required as the second argument\n' >&2
    exit 2
fi
if [ "${#EXPECTED_SHA}" -ne 64 ]; then
    printf 'ERROR: expected APK SHA-256 must be exactly 64 hexadecimal characters\n' >&2
    exit 2
fi
case "$EXPECTED_SHA" in
    *[!0123456789abcdefABCDEF]*)
        printf 'ERROR: expected APK SHA-256 must be exactly 64 hexadecimal characters\n' >&2
        exit 2
        ;;
esac

if [ ! -x "$ADB" ]; then
    printf 'ERROR: adb is not executable: %s\n' "$ADB" >&2
    exit 1
fi
if [ ! -f "$APK_PATH" ]; then
    printf 'ERROR: APK not found: %s\n' "$APK_PATH" >&2
    exit 1
fi
if ! command -v "$SHA_BIN" >/dev/null 2>&1 && [ ! -x "$SHA_BIN" ]; then
    printf 'ERROR: shasum is unavailable: %s\n' "$SHA_BIN" >&2
    exit 1
fi
if [ ! -d "$HOME/Desktop" ]; then
    printf 'ERROR: Desktop directory is unavailable: %s\n' "$HOME/Desktop" >&2
    exit 1
fi

RUN_DIR=$(mktemp -d "$HOME/Desktop/ddstart10-rsp.XXXXXX") || {
    printf 'ERROR: could not create a fresh Desktop capture directory\n' >&2
    exit 1
}
ZIP_PATH="${RUN_DIR}.zip"
RUN_LOG="$RUN_DIR/orchestrator.log"
LOGCAT_FILE="$RUN_DIR/logcat-threadtime.txt"
BASELINE_FILE="$RUN_DIR/baseline-captures.txt"
FRESH_FILE="$RUN_DIR/fresh-captures.txt"
CURRENT_FILE="$RUN_DIR/current-captures.txt"
STATUS_TMP="$RUN_DIR/status-latest.txt"
PULL_ROOT="$RUN_DIR/retrieved"
ADB_ERRORS="$RUN_DIR/adb-errors.txt"
: > "$RUN_LOG" || exit 1
: > "$ADB_ERRORS" || fatal "cannot create adb error log"

log "run_dir=$RUN_DIR"
log "apk=$APK_PATH"
log "expected_sha256=$EXPECTED_SHA"

ACTUAL_SHA=$("$SHA_BIN" -a 256 "$APK_PATH" 2>>"$ADB_ERRORS" | awk '{print $1}')
if [ "$ACTUAL_SHA" != "$EXPECTED_SHA" ] && [ "$ACTUAL_SHA" != "$(printf '%s' "$EXPECTED_SHA" | tr '[:upper:]' '[:lower:]')" ]; then
    fatal "APK SHA-256 mismatch (actual=$ACTUAL_SHA expected=$EXPECTED_SHA)"
fi
printf '%s  %s\n' "$ACTUAL_SHA" "$APK_PATH" > "$RUN_DIR/apk.sha256"
log "apk_sha256_verified=1"

log "adb_install=install -r"
if ! "$ADB" install -r "$APK_PATH" > "$RUN_DIR/install-r.txt" 2>>"$ADB_ERRORS"; then
    fatal "adb install -r failed; no uninstall or clear-data operation was attempted"
fi

logcat_args="-v threadtime -T 1 Core:V *:S"
log "starting_bounded_logcat=$logcat_args"
"$ADB" logcat -v threadtime -T 1 'Core:V' '*:S' > "$LOGCAT_FILE" 2>>"$ADB_ERRORS" &
LOGCAT_PID=$!
"$SLEEP_BIN" 1
if ! kill -0 "$LOGCAT_PID" 2>/dev/null; then
    warn "logcat exited immediately; inspect $LOGCAT_FILE and $ADB_ERRORS"
fi

SNAPSHOT_CMD="for d in $REMOTE_CAPTURE_ROOT/capture.*; do [ -d \"\$d\" ] || continue; printf '%s\n' \"\$d\"; done"
snapshot_dirs() {
    local output_file=$1
    local raw_file="$RUN_DIR/snapshot.raw"
    local capture_name
    : > "$raw_file"
    if ! "$ADB" shell "$SNAPSHOT_CMD" > "$raw_file" 2>>"$ADB_ERRORS"; then
        error "could not snapshot remote capture directories"
        return 1
    fi
    : > "$output_file"
    while IFS= read -r line; do
        line=${line%$'\r'}
        case "$line" in
            "$REMOTE_CAPTURE_ROOT"/capture.*)
                capture_name=${line#"$REMOTE_CAPTURE_ROOT"/capture.}
                case "$capture_name" in
                    ''|*[!A-Za-z0-9_-]*)
                        warn "ignoring malformed capture directory name"
                        continue
                        ;;
                esac
                printf '%s\n' "$line" >> "$output_file"
                ;;
        esac
    done < "$raw_file"
    return 0
}

if ! snapshot_dirs "$BASELINE_FILE"; then
    fatal "remote capture snapshot failed before launch instructions"
fi
log "baseline_capture_count=$(awk 'END { print NR + 0 }' "$BASELINE_FILE")"

printf '\nRun the existing /sdcard/Download/ddstart9-run-as-root.sh entry on the device.\n'
printf 'Immediately open the game with DD enabled and dynarec enabled, reproduce the stall, and leave it frozen.\n'
if [ "$TEST_MODE" = 1 ] && [ "${DD_RSP_MAC_TEST_SKIP_PROMPT:-0}" = 1 ]; then
    :
elif ! read -r -p 'When the game is frozen, press Return to begin capture polling: ' _confirmation; then
    fatal "launch/reproduction was not confirmed"
fi
log "user_reproduction_confirmed=1"

is_baseline() {
    local candidate=$1
    local old
    while IFS= read -r old; do
        [ "$candidate" = "$old" ] && return 0
    done < "$BASELINE_FILE"
    return 1
}

collect_fresh() {
    local line
    : > "$FRESH_FILE"
    if ! snapshot_dirs "$CURRENT_FILE"; then
        return 1
    fi
    while IFS= read -r line; do
        [ -n "$line" ] || continue
        if ! is_baseline "$line" && ! grep -F -x "$line" "$FRESH_FILE" >/dev/null 2>&1; then
            printf '%s\n' "$line" >> "$FRESH_FILE"
        fi
    done < "$CURRENT_FILE"
    return 0
}

fresh_count() {
    awk 'END { print NR + 0 }' "$FRESH_FILE"
}

capture_waited=0
FRESH_DIR=
while [ "$capture_waited" -le "$CAPTURE_WAIT_SECONDS" ]; do
    if ! collect_fresh; then
        warn "capture-directory polling failed; will retry"
    else
        count=$(fresh_count)
        if [ "$count" -eq 1 ]; then
            FRESH_DIR=$(cat "$FRESH_FILE")
            break
        fi
        if [ "$count" -gt 1 ]; then
            error "ambiguous: multiple fresh capture directories appeared; refusing to guess"
            break
        fi
    fi
    log "waiting_for_unique_fresh_capture elapsed=${capture_waited}s limit=${CAPTURE_WAIT_SECONDS}s"
    [ "$capture_waited" -eq "$CAPTURE_WAIT_SECONDS" ] && break
    "$SLEEP_BIN" "$POLL_INTERVAL_SECONDS"
    capture_waited=$((capture_waited + POLL_INTERVAL_SECONDS))
done

retrieve_capture() {
    local remote_dir=$1
    local name=${remote_dir##*/}
    if [ ! -d "$PULL_ROOT" ] && ! mkdir "$PULL_ROOT"; then
        warn "cannot create local retrieval directory; retaining remote path $remote_dir"
        return 1
    fi
    log "retrieving_capture=$remote_dir"
    if ! "$ADB" pull "$remote_dir" "$PULL_ROOT" >> "$RUN_DIR/adb-pull.txt" 2>>"$ADB_ERRORS"; then
        warn "could not retrieve $remote_dir; partial local files remain"
        return 1
    fi
    if [ ! -d "$PULL_ROOT/$name" ]; then
        warn "adb pull did not produce expected exact folder $PULL_ROOT/$name"
        return 1
    fi
    return 0
}

retrieve_root_launch() {
    log "retrieving_root_launch_log=$REMOTE_ROOT_LAUNCH"
    if ! "$ADB" shell "cat '$REMOTE_ROOT_LAUNCH'" > "$RUN_DIR/root-launch.log" 2>>"$ADB_ERRORS"; then
        warn "root launch log could not be retrieved; see $ADB_ERRORS"
        return 1
    fi
    return 0
}

if [ -z "$FRESH_DIR" ]; then
    if [ "$(fresh_count)" -gt 1 ]; then
        : > "$RUN_DIR/ambiguity.txt"
        while IFS= read -r line; do
            printf '%s\n' "$line" >> "$RUN_DIR/ambiguity.txt"
            retrieve_capture "$line" || :
        done < "$FRESH_FILE"
        retrieve_root_launch || :
        error "capture aborted because fresh directory selection was ambiguous"
    else
        warn "no unique fresh capture appeared within ${CAPTURE_WAIT_SECONDS}s"
        retrieve_root_launch || :
    fi
    exit 1
fi

log "unique_fresh_capture=$FRESH_DIR"
STATUS_FILE_REMOTE="$FRESH_DIR/status.txt"
status_waited=0
FINAL_STATE=
while [ "$status_waited" -le "$STATUS_WAIT_SECONDS" ]; do
    if "$ADB" shell "cat '$STATUS_FILE_REMOTE'" > "$STATUS_TMP" 2>>"$ADB_ERRORS"; then
        state=$(awk -F= '/^state=/{ value=$2 } END { print value }' "$STATUS_TMP" | tr -d '\r')
        cp "$STATUS_TMP" "$RUN_DIR/capture-status.txt"
        if [ "$state" = complete ] || [ "$state" = complete_with_errors ]; then
            FINAL_STATE=$state
            break
        fi
        if [ -n "$state" ] && [ "$state" != running ]; then
            FINAL_STATE=$state
            warn "capture reported unexpected terminal state: $state"
            break
        fi
        log "waiting_for_capture_status state=${state:-unavailable} elapsed=${status_waited}s limit=${STATUS_WAIT_SECONDS}s"
    else
        warn "capture status is not readable yet; retaining partial retrieval state"
    fi
    [ "$status_waited" -eq "$STATUS_WAIT_SECONDS" ] && break
    "$SLEEP_BIN" "$POLL_INTERVAL_SECONDS"
    status_waited=$((status_waited + POLL_INTERVAL_SECONDS))
done

if [ -n "$FINAL_STATE" ]; then
    retrieve_capture "$FRESH_DIR" || :
    retrieve_root_launch || :
    if [ "$FINAL_STATE" = complete ]; then
        log "capture_finished=complete"
    elif [ "$FINAL_STATE" = complete_with_errors ]; then
        warn "capture_finished=complete_with_errors"
        EXIT_CODE=1
    else
        error "capture_finished_with_unexpected_state=$FINAL_STATE"
        EXIT_CODE=1
    fi
else
    warn "capture status did not reach a terminal state within ${STATUS_WAIT_SECONDS}s; retrieving exact folder as partial"
    retrieve_capture "$FRESH_DIR" || :
    retrieve_root_launch || :
    EXIT_CODE=1
fi

exit "$EXIT_CODE"