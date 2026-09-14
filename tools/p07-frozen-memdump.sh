#!/system/bin/sh
# P07: bounded memory dump for the audio-command investigation.
#
# Root, read-only, against the emulation process. The process is briefly
# stopped (SIGSTOP/SIGCONT, restored by an EXIT trap) so the snapshot is
# coherent: a spinning RSP loop keeps rewriting DMEM/IMEM, and a frozen
# display does not mean a static memory image.
#
# Host addresses: the app uses the FULL 512 MiB mem_base allocation, and
# mem_base_u32() in full mode is identity (mem = mem_base + guest address;
# MEM_BASE_MODE == 0). The compressed-mode offsets (MB_RSP_MEM = 0x5000000)
# do NOT apply unless the small fallback allocation was selected.
#
#   guest MM_RDRAM_DRAM 0x00000000 -> mem_base + 0x00000000 (RDRAM)
#   guest MM_RSP_MEM    0x04000000 -> mem_base + 0x04000000 (DMEM; IMEM +0x1000)
#
# RDRAM physical = guest & 0x7fffff. Reads (each length-validated):
#   0x771D68   gCurAudioTask pointer (0x80771D68)    4 B
#   <pointer>  active AudioTask/OSTask: type at +0, data_ptr/data_size at
#              +0x30/+0x34; stored data_ptr is the VIRTUAL form (0x80411910
#              / 0x804132D0 for the two rspTask slots), so the probe
#              compares the masked physical form (0x411910 / 0x4132d0)
#   0x6EEAA0   gAudioCtx.rspTask[0] (0x806EEAA0)     0x50 B
#   0x6EEAF0   gAudioCtx.rspTask[1] (0x806EEAF0)     0x50 B
#   <data_ptr> audio command buffer, exactly the validated data_size
#   mem_base + 0x04000000   DMEM + IMEM              8 KiB
#   mem_base + 0x768e60     aspMain image in RDRAM   4 KiB
#
# Writes go only under a per-run capture directory
# /sdcard/Download/p07-memdump-<timestamp>/ containing run-metadata.txt,
# probe-log.txt (one line per mem_base candidate, including unreadable
# ones), the region files with exact expected lengths checked, and
# hashes.txt. Process/task identity is sampled before and after the reads.

PKG=org.mupen64plusae.turnip.pwnedbygary.debug:EmulationProcess
PID=$(pidof "$PKG")
if [ -z "$PID" ]; then
    echo "p07-memdump: no $PKG process; reproduce the freeze first"
    exit 1
fi

DIR=/sdcard/Download/p07-memdump-$(date '+%Y%m%d-%H%M%S')
if [ -e "$DIR" ]; then
    DIR="$DIR-$$"
fi
mkdir -p "$DIR"
: > "$DIR/probe-log.txt"
{
    echo "pid=$PID"
    echo "uid=$(id -u 2>/dev/null)"
    echo "started_at=$(date '+%Y-%m-%dT%H:%M:%S%z')"
} > "$DIR/run-metadata.txt"

# Memory-read canary: an actual /proc/<pid>/mem read at the start of the
# process's first mapping, keeping dd's exit status, byte count and stderr.
# (Reading /proc/<pid>/stat proves nothing about /proc/<pid>/mem access, and
# address 0 is usually unmapped, so the canary uses a real mapping.)
canary_addr=$(grep -m1 . /proc/$PID/maps 2>/dev/null | awk '{print $1}' | cut -d- -f1)
if [ -z "$canary_addr" ]; then
    echo "p07-memdump: cannot read /proc/$PID/maps; aborting" >> "$DIR/run-metadata.txt"
    echo "p07-memdump: cannot read /proc/$PID/maps; see $DIR"
    exit 1
fi
dd if=/proc/$PID/mem iflag=skip_bytes,count_bytes \
   skip=$((0x$canary_addr)) count=4 of="$DIR/canary-sample.bin" \
   2>"$DIR/canary-stderr.txt"
canary_rc=$?
canary_out=$(stat -c %s "$DIR/canary-sample.bin" 2>/dev/null)
{
    echo "canary_addr=0x$canary_addr"
    echo "canary_bytes=$canary_out"
    echo "canary_rc=$canary_rc"
    echo "canary_stderr=$(head -c 200 "$DIR/canary-stderr.txt" | tr '\n' ' ')"
} >> "$DIR/run-metadata.txt"
if [ "$canary_out" != "4" ]; then
    echo "p07-memdump: /proc/$PID/mem read failed (rc=$canary_rc, ${canary_out:-0} bytes); see $DIR"
    exit 1
fi

# Read 4 bytes at host address $1 as a little-endian 32-bit decimal value.
peek_u32() {
    dd if=/proc/$PID/mem iflag=skip_bytes,count_bytes skip=$1 count=4 \
        2>/dev/null | od -An -tu4 2>/dev/null | tr -d ' \n'
}

# Locate mem_base and the active descriptor.
BASE=""; DESC_PTR_PHYS=""; CMDBUF_PHYS=""; CMDBUF_SIZE=0
REGIONS=0; CANDIDATES=0
for region in $(grep 'rw-p' /proc/$PID/maps 2>/dev/null \
                    | grep '\[anon:scudo:secondary\]' | awk '{print $1}'); do
    start=$((0x${region%-*}))
    end=$((0x${region#*-}))
    [ $((end - start)) -lt 500000000 ] && continue
    REGIONS=$((REGIONS + 1))
    off=0
    # mksh's [ does not parse hex in integer comparisons: decimal 0xf000.
    while [ $off -le 61440 ]; do
        CANDIDATES=$((CANDIDATES + 1))
        cand=$((start + off))
        val=$(peek_u32 $((cand + 0x771D68)))
        if [ -n "$val" ]; then
            case "$val" in *[!0-9]*) val="";; esac
        fi
        if [ -z "$val" ]; then
            echo "offset=0x$(printf '%x' $off) cand=0x$(printf '%x' $cand) ptr=unreadable" \
                >> "$DIR/probe-log.txt"
            off=$((off + 0x1000))
            continue
        fi
        phys=$((val & 0x7fffff))
        d0=$(peek_u32 $((cand + phys)))
        dp=$(peek_u32 $((cand + phys + 0x30)))
        ds=$(peek_u32 $((cand + phys + 0x34)))
        echo "offset=0x$(printf '%x' $off) cand=0x$(printf '%x' $cand) ptr=$val phys=0x$(printf '%x' $phys) type=$d0 dptr=$dp dsize=$ds" \
            >> "$DIR/probe-log.txt"
        valid=1
        [ -z "$d0" ] && valid=0
        [ -z "$dp" ] && valid=0
        [ -z "$ds" ] && valid=0
        case "$d0$dp$ds" in *[!0-9]*) valid=0;; esac
        if [ "$valid" = "1" ] && [ "$d0" = "2" ] \
           && { [ $((dp & 0x7fffff)) -eq $((0x411910)) ] \
                || [ $((dp & 0x7fffff)) -eq $((0x4132d0)) ]; } \
           && [ "$ds" -gt 0 ] && [ "$ds" -le 4096 ]; then
            BASE=$cand
            DESC_PTR_PHYS=$phys
            CMDBUF_PHYS=$((dp & 0x7fffff))
            CMDBUF_SIZE=$ds
            break
        fi
        off=$((off + 0x1000))
    done
    [ -n "$BASE" ] && break
done

echo "probe_regions=$REGIONS probe_candidates=$CANDIDATES" >> "$DIR/run-metadata.txt"

if [ -z "$BASE" ]; then
    echo "p07-memdump: active audio descriptor not found ($CANDIDATES candidates in $REGIONS region(s)); see $DIR/probe-log.txt"
    exit 1
fi
{
    echo "mem_base=0x$(printf '%x' $BASE)"
    echo "active_descriptor_phys=0x$(printf '%x' $DESC_PTR_PHYS)"
    echo "cmdbuf_phys=0x$(printf '%x' $CMDBUF_PHYS)"
    echo "cmdbuf_size=$CMDBUF_SIZE"
} >> "$DIR/run-metadata.txt"

# Coherent snapshot: stop the process for the reads, restore on any exit.
kill -STOP $PID 2>/dev/null
trap 'kill -CONT $PID 2>/dev/null' EXIT
sleep 1
state=$(sed 's/.*) //' /proc/$PID/stat 2>/dev/null | awk '{print $1}')
echo "stopped_state=$state" >> "$DIR/run-metadata.txt"

# Identity snapshot before/after: process plus task pointers.
identity() {
    echo "curtask=0x$(printf '%x' $(peek_u32 $((BASE + 0x771D68))))"
    echo "task0_dptr=0x$(printf '%x' $(peek_u32 $((BASE + 0x6EEAA0 + 0x30))))"
    echo "task1_dptr=0x$(printf '%x' $(peek_u32 $((BASE + 0x6EEAF0 + 0x30))))"
}

# Read region: $1 host-relative address, $2 bytes, $3 filename; validates the
# exact output length and records the outcome.
read_region() {
    dd if=/proc/$PID/mem iflag=skip_bytes,count_bytes \
       skip=$((BASE + $1)) count=$2 of="$DIR/$3" 2>/dev/null
    rc=$?
    got=$(stat -c %s "$DIR/$3" 2>/dev/null)
    want=$(( $2 ))
    if [ -n "$got" ] && [ "$got" -eq "$want" ]; then
        echo "read_ok $3 $got rc=$rc" >> "$DIR/run-metadata.txt"
    else
        echo "read_SHORT $3 expected=$want got=${got:-0} rc=$rc" >> "$DIR/run-metadata.txt"
    fi
}

{
    echo "identity_before:"
    identity
} >> "$DIR/run-metadata.txt"

read_region 0x771D68        4              curtask-pointer.bin
read_region $DESC_PTR_PHYS  64             active-descriptor.bin
read_region 0x6EEAA0        0x50           rspTask0.bin
read_region 0x6EEAF0        0x50           rspTask1.bin
read_region $CMDBUF_PHYS    $CMDBUF_SIZE   cmdbuf.bin
read_region 0x4000000       8192           rspmem.bin
read_region 0x768e60        4096           ucode-rdram.bin

{
    echo "identity_after:"
    identity
} >> "$DIR/run-metadata.txt"

if [ ! -s "$DIR/active-descriptor.bin" ] || [ ! -s "$DIR/rspmem.bin" ]; then
    echo "p07-memdump: dd produced no data; see $DIR/run-metadata.txt"
    exit 1
fi

sha256sum "$DIR"/*.bin 2>/dev/null > "$DIR/hashes.txt"
{
    echo "finished_at=$(date '+%Y-%m-%dT%H:%M:%S%z')"
    ls -la "$DIR"
} >> "$DIR/run-metadata.txt"
echo "p07-memdump: complete -> $DIR"
