#!/system/bin/sh
#
# DDSTART9 native-stack capture helper.  The installed copy is invoked by the
# handheld Settings root-script runner; it deliberately does not change the
# application, settings, or SELinux state. debuggerd may briefly pause the
# target while each requested stack dump is collected.

umask 022

EXPECTED_PROCESS=org.mupen64plusae.turnip.pwnedbygary.debug:EmulationProcess
DELAY_SECONDS=90
SAMPLE_GAP_SECONDS=2
DUMP_TIMEOUT_SECONDS=20
MAX_MATCHING_PIDS=4
MAX_THREAD_IDS=128
PROC_FILE_LIMIT=1048576
THREAD_FILE_LIMIT=8192

CAPTURE_ROOT=${DD_CAPTURE_ROOT:-/sdcard/Download/ddstart9-root-capture}
SCRIPT_PATH=${DD_SCRIPT_PATH:-/sdcard/Download/ddstart9-root-stacks.sh}
SHELL_BIN=${DD_SHELL_BIN:-/system/bin/sh}
NOHUP_BIN=${DD_NOHUP_BIN:-nohup}
ID_BIN=${DD_ID_BIN:-id}
MKDIR_BIN=${DD_MKDIR_BIN:-mkdir}
MKTEMP_BIN=${DD_MKTEMP_BIN:-mktemp}
CHMOD_BIN=${DD_CHMOD_BIN:-chmod}
MV_BIN=${DD_MV_BIN:-mv}
DATE_BIN=${DD_DATE_BIN:-date}
SLEEP_BIN=${DD_SLEEP_BIN:-sleep}
PIDOF_BIN=${DD_PIDOF_BIN:-pidof}
DEBUGGERD_BIN=${DD_DEBUGGERD_BIN:-debuggerd}
TIMEOUT_BIN=${DD_TIMEOUT_BIN:-}
PROC_ROOT=${DD_PROC_ROOT:-/proc}
TR_BIN=${DD_TR_BIN:-tr}
SED_BIN=${DD_SED_BIN:-sed}
AWK_BIN=${DD_AWK_BIN:-awk}
GETCONF_BIN=${DD_GETCONF_BIN:-getconf}
HEAD_BIN=${DD_HEAD_BIN:-head}
CAT_BIN=${DD_CAT_BIN:-cat}
WC_BIN=${DD_WC_BIN:-wc}
RM_BIN=${DD_RM_BIN:-rm}
# DD_* overrides are test-only fixture hooks. The installed Settings action
# supplies no overrides and therefore uses the fixed Android defaults above.

MARKER=$CAPTURE_ROOT/latest-complete
OUTPUT_DIR=
STATUS_FILE=
METADATA_FILE=
STACK_FILE=
MAP_FILE_1=
MAP_FILE_2=
THREAD_FILE_1=
THREAD_FILE_2=
FAILURE=0
ERROR_NUMBER=0
ROOT_UID=
CAPTURE_NAME=
PINNED_PID=
PINNED_STARTTIME=
IDENTITY_PINNED=0
READ_SEQUENCE=0
CLK_TCK=unavailable

timestamp() {
    value=$("$DATE_BIN" '+%Y-%m-%dT%H:%M:%S%z' 2>/dev/null)
    if [ -n "$value" ]; then
        printf '%s' "$value"
    else
        printf '%s' unknown
    fi
}

usage_error() {
    printf 'ddstart9-root-stacks: %s\n' "$1" >&2
    return 2
}

check_root() {
    ROOT_UID=$("$ID_BIN" -u 2>/dev/null)
    if [ "$ROOT_UID" != 0 ]; then
        if [ -n "$ROOT_UID" ]; then
            printf 'ddstart9-root-stacks: root required (id -u=%s)\n' "$ROOT_UID" >&2
        else
            printf 'ddstart9-root-stacks: root required (id -u failed)\n' >&2
        fi
        return 1
    fi
    return 0
}

ensure_capture_root() {
    if [ -L "$CAPTURE_ROOT" ]; then
        printf 'ddstart9-root-stacks: refusing symlink diagnostic root: %s\n' "$CAPTURE_ROOT" >&2
        return 1
    fi
    if [ -e "$CAPTURE_ROOT" ]; then
        if [ ! -d "$CAPTURE_ROOT" ]; then
            printf 'ddstart9-root-stacks: diagnostic root is not a directory: %s\n' "$CAPTURE_ROOT" >&2
            return 1
        fi
    else
        "$MKDIR_BIN" "$CAPTURE_ROOT" 2>/dev/null || {
            printf 'ddstart9-root-stacks: cannot create diagnostic root: %s\n' "$CAPTURE_ROOT" >&2
            return 1
        }
    fi
    "$CHMOD_BIN" 755 "$CAPTURE_ROOT" 2>/dev/null || {
        printf 'ddstart9-root-stacks: cannot set diagnostic root mode\n' >&2
        return 1
    }
    if [ -L "$MARKER" ]; then
        printf 'ddstart9-root-stacks: refusing symlink completion marker: %s\n' "$MARKER" >&2
        return 1
    fi
    return 0
}

directory_is_empty() {
    entry=
    # A menu launch pre-creates only launcher.log so launch failures are
    # pullable. All other pre-existing entries are stale/collision data.
    for entry in "$1"/* "$1"/.[!.]* "$1"/..?*; do
        if [ "$entry" = "$1/launcher.log" ] && [ -f "$entry" ] && [ ! -L "$entry" ]; then
            continue
        fi
        if [ -e "$entry" ] || [ -L "$entry" ]; then
            return 1
        fi
    done
    return 0
}

validate_output_dir() {
    if [ "$#" -ne 1 ]; then
        usage_error '--worker requires exactly one output directory'
        return 1
    fi
    OUTPUT_DIR=$1
    case "$CAPTURE_ROOT" in */)
        printf 'ddstart9-root-stacks: diagnostic root must not end with /\n' >&2
        return 1
    esac
    case "$OUTPUT_DIR" in
        "$CAPTURE_ROOT"/*) ;;
        *)
            printf 'ddstart9-root-stacks: output directory is outside diagnostic root\n' >&2
            return 1
            ;;
    esac
    CAPTURE_NAME=${OUTPUT_DIR#"$CAPTURE_ROOT"/}
    case "$CAPTURE_NAME" in
        ''|.|..|.*|*/*|*..*|*[!ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-]*)
            printf 'ddstart9-root-stacks: invalid output directory name\n' >&2
            return 1
            ;;
    esac
    if [ -L "$OUTPUT_DIR" ] || [ ! -d "$OUTPUT_DIR" ]; then
        printf 'ddstart9-root-stacks: output directory is not a real directory\n' >&2
        return 1
    fi
    if ! directory_is_empty "$OUTPUT_DIR"; then
        printf 'ddstart9-root-stacks: refusing stale or colliding output directory: %s\n' "$OUTPUT_DIR" >&2
        return 1
    fi
    return 0
}

new_output_dir() {
    OUTPUT_DIR=$("$MKTEMP_BIN" -d "$CAPTURE_ROOT/capture.XXXXXX" 2>/dev/null) || {
        printf 'ddstart9-root-stacks: mktemp could not create a capture directory\n' >&2
        return 1
    }
    case "$OUTPUT_DIR" in "$CAPTURE_ROOT"/*) ;; *)
        printf 'ddstart9-root-stacks: mktemp returned an unsafe capture path\n' >&2
        return 1
        ;;
    esac
    if [ -L "$OUTPUT_DIR" ] || [ ! -d "$OUTPUT_DIR" ]; then
        printf 'ddstart9-root-stacks: mktemp returned a non-directory capture path\n' >&2
        return 1
    fi
    CAPTURE_NAME=${OUTPUT_DIR#"$CAPTURE_ROOT"/}
    "$CHMOD_BIN" 755 "$OUTPUT_DIR" 2>/dev/null || {
        printf 'ddstart9-root-stacks: cannot set capture directory mode\n' >&2
        return 1
    }
    return 0
}

prepare_files() {
    STATUS_FILE=$OUTPUT_DIR/status.txt
    METADATA_FILE=$OUTPUT_DIR/run-metadata.txt
    STACK_FILE=$OUTPUT_DIR/native-stacks.txt
    MAP_FILE_1=$OUTPUT_DIR/sample-1-maps.txt
    MAP_FILE_2=$OUTPUT_DIR/sample-2-maps.txt
    THREAD_FILE_1=$OUTPUT_DIR/sample-1-threads.txt
    THREAD_FILE_2=$OUTPUT_DIR/sample-2-threads.txt
    : > "$STATUS_FILE" || return 1
    : > "$METADATA_FILE" || return 1
    : > "$STACK_FILE" || return 1
    : > "$MAP_FILE_1" || return 1
    : > "$MAP_FILE_2" || return 1
    : > "$THREAD_FILE_1" || return 1
    : > "$THREAD_FILE_2" || return 1
    "$CHMOD_BIN" 644 "$STATUS_FILE" "$METADATA_FILE" "$STACK_FILE" \
        "$MAP_FILE_1" "$MAP_FILE_2" "$THREAD_FILE_1" "$THREAD_FILE_2" \
        2>/dev/null || return 1
    printf 'state=running\nstarted_at=%s\n' "$(timestamp)" > "$STATUS_FILE"
    printf 'format=ddstart9-root-stacks-v2\n' > "$METADATA_FILE"
    printf 'capture_dir=%s\n' "$OUTPUT_DIR" >> "$METADATA_FILE"
    printf 'root_uid=%s\n' "$ROOT_UID" >> "$METADATA_FILE"
    printf 'expected_process=%s\n' "$EXPECTED_PROCESS" >> "$METADATA_FILE"
    printf 'delay_seconds=%s\nsample_gap_seconds=%s\n' "$DELAY_SECONDS" "$SAMPLE_GAP_SECONDS" >> "$METADATA_FILE"
    printf 'sample_count=2\nmax_matching_pids=%s\ndump_timeout_seconds=%s\n' \
        "$MAX_MATCHING_PIDS" "$DUMP_TIMEOUT_SECONDS" >> "$METADATA_FILE"
    printf 'max_thread_ids=%s\nproc_file_limit_bytes=%s\nthread_file_limit_bytes=%s\n' \
        "$MAX_THREAD_IDS" "$PROC_FILE_LIMIT" "$THREAD_FILE_LIMIT" >> "$METADATA_FILE"
    printf 'selected_expected_name=%s\n' "$EXPECTED_PROCESS" >> "$METADATA_FILE"
    if command -v "$GETCONF_BIN" >/dev/null 2>&1; then
        CLK_TCK=$("$GETCONF_BIN" CLK_TCK 2>/dev/null)
        case "$CLK_TCK" in
            ''|*[!0-9]*) CLK_TCK=unavailable; record_error 'getconf_CLK_TCK_failed' ;;
        esac
    else
        CLK_TCK=unavailable
    fi
    printf 'clk_tck=%s\n' "$CLK_TCK" >> "$METADATA_FILE"
    return 0
}

read_starttime() {
    stat_file=$PROC_ROOT/$1/stat
    if [ ! -r "$stat_file" ]; then
        return 1
    fi
    # /proc/PID/stat's comm may contain spaces and parentheses.  Strip through
    # the final ") " delimiter first; the resulting tail starts at state
    # (tail field 1), making starttime tail field 20.
    stat_tail=$("$SED_BIN" -n 's/^.*) //p' "$stat_file" 2>/dev/null)
    if ! STARTTIME=$(printf '%s\n' "$stat_tail" | "$AWK_BIN" '
        NF >= 20 && $20 ~ /^[0-9][0-9]*$/ { print $20; found++ }
        END { if (found != 1) exit 1 }
    ' 2>/dev/null); then
        return 1
    fi
    case "$STARTTIME" in
        ''|*[!0-9]*) return 1 ;;
    esac
    return 0
}

revalidate_identity() {
    sample=$1
    pid=$2
    context=$3
    if [ "$IDENTITY_PINNED" -ne 1 ] || [ "$pid" != "$PINNED_PID" ]; then
        record_error "sample_${sample}_${context}_identity_not_pinned"
        return 1
    fi
    cmdline_file=$PROC_ROOT/$pid/cmdline
    if [ ! -r "$cmdline_file" ]; then
        record_error "sample_${sample}_${context}_cmdline_unreadable"
        return 1
    fi
    first_arg=$("$TR_BIN" '\000' '\n' < "$cmdline_file" 2>/dev/null | "$SED_BIN" -n '1p' 2>/dev/null)
    if [ "$first_arg" != "$EXPECTED_PROCESS" ]; then
        record_error "sample_${sample}_${context}_wrong_first_arg"
        return 1
    fi
    if ! read_starttime "$pid"; then
        record_error "sample_${sample}_${context}_starttime_unreadable"
        return 1
    fi
    if [ "$STARTTIME" != "$PINNED_STARTTIME" ]; then
        record_error "sample_${sample}_${context}_starttime_changed"
        return 1
    fi
    return 0
}

read_proc_file() {
    sample=$1
    label=$2
    source_file=$3
    target_file=$4
    limit_bytes=$5
    READ_SEQUENCE=$((READ_SEQUENCE + 1))
    temp_file=$OUTPUT_DIR/.proc-read.$$.$READ_SEQUENCE
    printf '\n[file=%s source=%s]\nread_started=%s\n' "$label" "$source_file" \
        "$(timestamp)" >> "$target_file"
    if ! revalidate_identity "$sample" "$SELECTED_PID" "before_${label}_read"; then
        printf 'read_outcome=identity_changed\nread_finished=%s\n' "$(timestamp)" \
            >> "$target_file"
        return 1
    fi
    if [ ! -e "$source_file" ]; then
        printf 'read_outcome=disappeared\nread_finished=%s\n' "$(timestamp)" \
            >> "$target_file"
        record_error "sample_${sample}_${label}_disappeared"
        return 1
    fi
    if [ ! -r "$source_file" ]; then
        printf 'read_outcome=permission_denied\nread_finished=%s\n' "$(timestamp)" \
            >> "$target_file"
        record_error "sample_${sample}_${label}_permission_denied"
        return 1
    fi
    if [ -e "$temp_file" ] || [ -L "$temp_file" ]; then
        printf 'read_outcome=temp_collision\nread_finished=%s\n' "$(timestamp)" \
            >> "$target_file"
        record_error "sample_${sample}_${label}_temp_collision"
        return 1
    fi
    if [ "$TIMEOUT_AVAILABLE" -ne 1 ]; then
        printf 'read_outcome=timeout_unavailable\nread_finished=%s\n' "$(timestamp)" \
            >> "$target_file"
        record_error "sample_${sample}_${label}_timeout_unavailable"
        return 1
    fi
    "$TIMEOUT_BIN" "$DUMP_TIMEOUT_SECONDS" "$HEAD_BIN" -c "$limit_bytes" \
        "$source_file" > "$temp_file" 2>&1
    read_rc=$?
    "$CAT_BIN" "$temp_file" >> "$target_file" 2>/dev/null || {
        record_error "sample_${sample}_${label}_output_write_failed"
    }
    read_bytes=$("$WC_BIN" -c < "$temp_file" 2>/dev/null)
    case "$read_bytes" in
        ''|*[!0-9]*) read_bytes=unknown ;;
    esac
    if [ "$read_rc" -ne 0 ]; then
        printf 'read_outcome=read_failed_rc_%s\n' "$read_rc" >> "$target_file"
        record_error "sample_${sample}_${label}_read_failed_rc=$read_rc"
    elif [ "$read_bytes" = unknown ]; then
        printf 'read_outcome=size_unknown\n' >> "$target_file"
        record_error "sample_${sample}_${label}_size_unknown"
    elif [ "$read_bytes" -ge "$limit_bytes" ]; then
        printf 'read_outcome=file_limit_reached\n' >> "$target_file"
        record_error "sample_${sample}_${label}_file_limit_reached=$limit_bytes"
    else
        printf 'read_outcome=ok\n' >> "$target_file"
    fi
    printf 'bytes=%s\nread_finished=%s\n' "$read_bytes" "$(timestamp)" >> "$target_file"
    "$RM_BIN" -f "$temp_file" 2>/dev/null || :
    if [ "$read_rc" -ne 0 ] || [ "$read_bytes" = unknown ]; then
        return 1
    fi
    if [ "$read_bytes" -ge "$limit_bytes" ]; then
        return 1
    fi
    return 0
}

append_metadata() {
    printf '%s\n' "$1" >> "$METADATA_FILE"
}

record_error() {
    FAILURE=1
    ERROR_NUMBER=$((ERROR_NUMBER + 1))
    printf 'error_%s=%s\n' "$ERROR_NUMBER" "$1" >> "$STATUS_FILE"
}

run_sleep() {
    "$SLEEP_BIN" "$1" >/dev/null 2>&1
    if [ "$?" -ne 0 ]; then
        record_error "sleep_failed_seconds=$1"
        return 1
    fi
    return 0
}

select_verified_pid() {
    sample=$1
    SELECTED_PID=
    SELECTED_STARTTIME=
    pid_list=$("$PIDOF_BIN" "$EXPECTED_PROCESS" 2>/dev/null)
    pidof_rc=$?
    append_metadata "sample_${sample}_pidof_rc=$pidof_rc"
    case "$pidof_rc" in
        0|1) ;;
        *) record_error "sample_${sample}_pidof_failed_rc=$pidof_rc" ;;
    esac
    candidate_count=0
    candidate_cap_hit=0
    matching_count=0
    matching_pid=
    for pid in $pid_list; do
        candidate_count=$((candidate_count + 1))
        if [ "$candidate_count" -gt "$MAX_MATCHING_PIDS" ]; then
            candidate_cap_hit=1
            record_error "sample_${sample}_pid_cap_reached=${MAX_MATCHING_PIDS}"
            break
        fi
        case "$pid" in ''|*[!0-9]*|0)
            record_error "sample_${sample}_non_numeric_pid=$pid"
            continue
            ;;
        esac
        cmdline_file=$PROC_ROOT/$pid/cmdline
        if [ ! -r "$cmdline_file" ]; then
            record_error "sample_${sample}_pid_${pid}_cmdline_unreadable"
            continue
        fi
        first_arg=$("$TR_BIN" '\000' '\n' < "$cmdline_file" 2>/dev/null | "$SED_BIN" -n '1p' 2>/dev/null)
        if [ "$first_arg" != "$EXPECTED_PROCESS" ]; then
            record_error "sample_${sample}_pid_${pid}_wrong_first_arg"
            continue
        fi
        # pidof should not duplicate a PID, but count unique exact matches so
        # a malformed fixture cannot turn one process into a false multiple.
        if [ "$matching_pid" != "$pid" ]; then
            matching_count=$((matching_count + 1))
            matching_pid=$pid
        fi
        if [ "$matching_count" -gt 1 ]; then
            break
        fi
    done
    append_metadata "sample_${sample}_candidate_count=$candidate_count"
    append_metadata "sample_${sample}_exact_match_count=$matching_count"
    if [ "$candidate_cap_hit" -eq 1 ]; then
        append_metadata "sample_${sample}_pid=none"
        return 1
    fi
    if [ "$matching_count" -ne 1 ]; then
        if [ "$matching_count" -gt 1 ]; then
            record_error "sample_${sample}_multiple_exact_process_matches"
        elif [ -z "$pid_list" ]; then
            record_error "sample_${sample}_no_matching_pid"
        else
            record_error "sample_${sample}_no_verified_exact_process"
        fi
        append_metadata "sample_${sample}_pid=none"
        return 1
    fi
    SELECTED_PID=$matching_pid
    append_metadata "sample_${sample}_pid=$SELECTED_PID"
    append_metadata "sample_${sample}_verified_at=$(timestamp)"
    if ! read_starttime "$SELECTED_PID"; then
        record_error "sample_${sample}_pid_${SELECTED_PID}_missing_stat_starttime"
        append_metadata "sample_${sample}_starttime=none"
        return 1
    fi
    SELECTED_STARTTIME=$STARTTIME
    append_metadata "sample_${sample}_starttime=$SELECTED_STARTTIME"
    if [ "$sample" -eq 1 ]; then
        PINNED_PID=$SELECTED_PID
        PINNED_STARTTIME=$SELECTED_STARTTIME
        IDENTITY_PINNED=1
        append_metadata "identity_pinned_pid=$PINNED_PID"
        append_metadata "identity_pinned_starttime=$PINNED_STARTTIME"
    else
        if [ "$IDENTITY_PINNED" -ne 1 ]; then
            record_error 'sample_2_initial_identity_was_not_pinned'
            return 1
        fi
        if [ "$SELECTED_PID" != "$PINNED_PID" ]; then
            record_error "sample_2_pid_changed_from_${PINNED_PID}_to_${SELECTED_PID}"
            return 1
        fi
        if [ "$SELECTED_STARTTIME" != "$PINNED_STARTTIME" ]; then
            record_error "sample_2_starttime_changed_for_pid_${SELECTED_PID}"
            return 1
        fi
    fi
    return 0
}

collect_sample_metadata() {
    sample=$1
    pid=$2
    case "$sample" in
        1) map_file=$MAP_FILE_1; thread_file=$THREAD_FILE_1 ;;
        2) map_file=$MAP_FILE_2; thread_file=$THREAD_FILE_2 ;;
        *) record_error "sample_${sample}_invalid_metadata_index"; return 1 ;;
    esac
    append_metadata "sample_${sample}_metadata_started=$(timestamp)"
    printf 'format=ddstart9-maps-v1\nsample=%s\npid=%s\nexpected_process=%s\n' \
        "$sample" "$pid" "$EXPECTED_PROCESS" >> "$map_file"
    printf 'format=ddstart9-threads-v1\nsample=%s\npid=%s\nexpected_process=%s\nclk_tck=%s\n' \
        "$sample" "$pid" "$EXPECTED_PROCESS" "$CLK_TCK" >> "$thread_file"
    read_proc_file "$sample" process-maps "$PROC_ROOT/$pid/maps" "$map_file" "$PROC_FILE_LIMIT" || :
    read_proc_file "$sample" process-stat "$PROC_ROOT/$pid/stat" "$thread_file" "$PROC_FILE_LIMIT" || :
    task_dir=$PROC_ROOT/$pid/task
    thread_count=0
    thread_cap_hit=0
    if ! revalidate_identity "$sample" "$pid" before-thread-enumeration; then
        printf 'thread_enumeration=identity_changed\n' >> "$thread_file"
    elif [ ! -d "$task_dir" ] || [ ! -r "$task_dir" ]; then
        printf 'thread_enumeration=unreadable\n' >> "$thread_file"
        record_error "sample_${sample}_thread_directory_unreadable"
    else
        for tid_path in "$task_dir"/*; do
            tid=${tid_path##*/}
            case "$tid" in
                ''|*[!0-9]*|0) continue ;;
            esac
            thread_count=$((thread_count + 1))
            if [ "$thread_count" -gt "$MAX_THREAD_IDS" ]; then
                thread_cap_hit=1
                record_error "sample_${sample}_thread_cap_reached=$MAX_THREAD_IDS"
                break
            fi
            if [ ! -d "$tid_path" ]; then
                printf '[tid=%s]\nthread_outcome=disappeared\n' "$tid" >> "$thread_file"
                record_error "sample_${sample}_tid_${tid}_disappeared"
                continue
            fi
            printf '\n[tid=%s]\nthread_started=%s\n' "$tid" "$(timestamp)" >> "$thread_file"
            read_proc_file "$sample" "tid-${tid}-stat" \
                "$task_dir/$tid/stat" "$thread_file" "$THREAD_FILE_LIMIT" || :
            read_proc_file "$sample" "tid-${tid}-schedstat" \
                "$task_dir/$tid/schedstat" "$thread_file" "$THREAD_FILE_LIMIT" || :
            read_proc_file "$sample" "tid-${tid}-status" \
                "$task_dir/$tid/status" "$thread_file" "$THREAD_FILE_LIMIT" || :
            read_proc_file "$sample" "tid-${tid}-wchan" \
                "$task_dir/$tid/wchan" "$thread_file" "$THREAD_FILE_LIMIT" || :
            read_proc_file "$sample" "tid-${tid}-comm" \
                "$task_dir/$tid/comm" "$thread_file" "$THREAD_FILE_LIMIT" || :
            printf 'thread_finished=%s\n' "$(timestamp)" >> "$thread_file"
        done
    fi
    if [ "$thread_count" -eq 0 ] && [ "$thread_cap_hit" -eq 0 ]; then
        record_error "sample_${sample}_no_numeric_threads"
    fi
    append_metadata "sample_${sample}_thread_count=$thread_count"
    append_metadata "sample_${sample}_metadata_finished=$(timestamp)"
    return 0
}

find_timeout() {
    if [ -n "$TIMEOUT_BIN" ]; then
        if [ -x "$TIMEOUT_BIN" ] || command -v "$TIMEOUT_BIN" >/dev/null 2>&1; then
            TIMEOUT_AVAILABLE=1
            return 0
        fi
        TIMEOUT_AVAILABLE=0
        return 1
    fi
    if command -v timeout >/dev/null 2>&1; then
        TIMEOUT_BIN=timeout
        TIMEOUT_AVAILABLE=1
        return 0
    fi
    TIMEOUT_AVAILABLE=0
    return 1
}

run_dump() {
    sample=$1
    pid=$2
    # Re-read cmdline directly before debuggerd.  The earlier pidof result is
    # intentionally not sufficient because a PID may have been recycled.
    if ! revalidate_identity "$sample" "$pid" before-debuggerd; then
        return 1
    fi
    append_metadata "sample_${sample}_pid_rechecked_at=$(timestamp)"
    dump_started=$(timestamp)
    append_metadata "sample_${sample}_dump_started=$dump_started"
    {
        printf '\n=== debuggerd_sample=%s pid=%s started=%s ===\n' "$sample" "$pid" "$dump_started"
        if [ "$TIMEOUT_AVAILABLE" -eq 1 ]; then
            "$TIMEOUT_BIN" "$DUMP_TIMEOUT_SECONDS" "$DEBUGGERD_BIN" -b "$pid"
            dump_rc=$?
        else
            printf 'ERROR: timeout command unavailable; debuggerd not run\n'
            dump_rc=127
        fi
        printf '=== debuggerd_sample=%s exit=%s finished=%s ===\n' "$sample" "$dump_rc" "$(timestamp)"
    } >> "$STACK_FILE" 2>&1
    append_metadata "sample_${sample}_dump_rc=$dump_rc"
    if [ "$dump_rc" -ne 0 ]; then
        record_error "sample_${sample}_debuggerd_failed_rc=$dump_rc"
        return 1
    fi
    return 0
}

capture_sample() {
    sample=$1
    append_metadata "sample_${sample}_started=$(timestamp)"
    if select_verified_pid "$sample"; then
        collect_sample_metadata "$sample" "$SELECTED_PID" || :
        run_dump "$sample" "$SELECTED_PID" || :
    fi
}

publish_marker() {
    final_state=$1
    token="$(timestamp).$$.$CAPTURE_NAME"
    marker_tmp=$CAPTURE_ROOT/.latest-complete.$$.tmp
    if [ -e "$marker_tmp" ] || [ -L "$marker_tmp" ]; then
        record_error 'completion_marker_temp_collision'
        return 1
    fi
    {
        printf 'token=%s\n' "$token"
        printf 'capture_dir=%s\n' "$OUTPUT_DIR"
        printf 'state=%s\n' "$final_state"
        printf 'completed_at=%s\n' "$(timestamp)"
    } > "$marker_tmp" 2>/dev/null || {
        record_error 'completion_marker_write_failed'
        return 1
    }
    "$CHMOD_BIN" 644 "$marker_tmp" 2>/dev/null || {
        record_error 'completion_marker_mode_failed'
        return 1
    }
    if [ -L "$MARKER" ]; then
        record_error 'completion_marker_symlink_refused'
        return 1
    fi
    "$MV_BIN" "$marker_tmp" "$MARKER" 2>/dev/null || {
        record_error 'completion_marker_publish_failed'
        return 1
    }
    return 0
}

finish_worker() {
    if [ "$FAILURE" -eq 0 ]; then
        final_state=complete
    else
        final_state=complete_with_errors
    fi
    printf 'state=%s\nfinished_at=%s\n' "$final_state" "$(timestamp)" >> "$STATUS_FILE"
    append_metadata "finished_at=$(timestamp)"
    append_metadata "final_state=$final_state"
    publish_marker "$final_state" || :
    "$CHMOD_BIN" 644 "$STATUS_FILE" "$METADATA_FILE" "$STACK_FILE" \
        "$MAP_FILE_1" "$MAP_FILE_2" "$THREAD_FILE_1" "$THREAD_FILE_2" 2>/dev/null || :
}

worker() {
    if ! check_root; then
        return 1
    fi
    if ! ensure_capture_root; then
        return 1
    fi
    if ! validate_output_dir "$1"; then
        return 1
    fi
    if ! prepare_files; then
        printf 'ddstart9-root-stacks: cannot initialize capture files\n' >&2
        return 1
    fi
    if ! find_timeout; then
        append_metadata 'timeout_available=0'
        record_error 'timeout_command_unavailable'
    else
        append_metadata "timeout_available=1"
        append_metadata "timeout_command=$TIMEOUT_BIN"
    fi
    printf 'waiting_seconds=%s\n' "$DELAY_SECONDS" >> "$STATUS_FILE"
    run_sleep "$DELAY_SECONDS" || :
    capture_sample 1
    run_sleep "$SAMPLE_GAP_SECONDS" || :
    capture_sample 2
    finish_worker
    return 0
}

menu_start() {
    if ! check_root; then
        return 1
    fi
    if [ ! -r "$SCRIPT_PATH" ]; then
        printf 'ddstart9-root-stacks: script is not readable: %s\n' "$SCRIPT_PATH" >&2
        return 1
    fi
    if ! ensure_capture_root || ! new_output_dir; then
        return 1
    fi
    if ! command -v "$NOHUP_BIN" >/dev/null 2>&1; then
        printf 'ddstart9-root-stacks: nohup command unavailable\n' >&2
        return 1
    fi
    if ! command -v "$SHELL_BIN" >/dev/null 2>&1; then
        printf 'ddstart9-root-stacks: shell command unavailable\n' >&2
        return 1
    fi
    launcher_log=$OUTPUT_DIR/launcher.log
    if [ -e "$launcher_log" ] || [ -L "$launcher_log" ]; then
        printf 'ddstart9-root-stacks: launcher log collision\n' >&2
        return 1
    fi
    printf 'format=ddstart9-launcher-v1\nrequested_at=%s\n' "$(timestamp)" > "$launcher_log" 2>/dev/null || {
        printf 'ddstart9-root-stacks: cannot initialize launcher log\n' >&2
        return 1
    }
    "$CHMOD_BIN" 644 "$launcher_log" 2>/dev/null || {
        printf 'ddstart9-root-stacks: cannot set launcher log mode\n' >&2
        return 1
    }
    # With production defaults this is:
    # nohup /system/bin/sh /sdcard/Download/ddstart9-root-stacks.sh --worker
    # <mktemp output directory>.  No descriptor is left attached to the
    # Settings runner; launch errors remain in launcher.log.
    "$NOHUP_BIN" "$SHELL_BIN" "$SCRIPT_PATH" --worker "$OUTPUT_DIR" \
        </dev/null >>"$launcher_log" 2>&1 &
    printf 'capture_dir=%s\n' "$OUTPUT_DIR"
    printf 'worker_launch_requested=1\n'
    return 0
}

case "$1" in
    --worker)
        if [ "$#" -ne 2 ]; then
            usage_error '--worker requires one output directory'
            exit 2
        fi
        worker "$2"
        exit "$?"
        ;;
    '')
        if [ "$#" -ne 0 ]; then
            usage_error 'unexpected arguments'
            exit 2
        fi
        menu_start
        exit "$?"
        ;;
    *)
        usage_error 'only --worker is accepted'
        exit 2
        ;;
esac