#!/system/bin/sh
# P07: windowed memory dump for the audio-command investigation.
#
# Root, read-only. mksh arithmetic is 32-BIT on this device:
# `$((0x6fc2c5f000))` evaluates to -1027215360, so no host address is ever
# computed with shell arithmetic. Hex-string addition uses an awk helper
# (decimal doubles are exact below 2^53) and dd receives raw 64-bit hex or
# decimal skip values. Descriptor validation and command-buffer extraction
# happen OFFLINE on the host, from the dumped windows.
#
# Host layout (full 512 MiB mem_base allocation; mem_base_u32() in full mode
# is identity: mem = mem_base + guest address, MEM_BASE_MODE == 0):
#   RDRAM        = mem_base + 0x00000000
#   RSP memory   = mem_base + 0x04000000 (guest MM_RSP_MEM; DMEM +0, IMEM +0x1000)
#   compressed-mode offsets (MB_RSP_MEM = 0x5000000) do NOT apply here.
#
# Dumped windows (all offsets of interest are inside them):
#   [mem_base, mem_base + 8 MiB)  RDRAM: gCurAudioTask 0x771D68, rspTask
#       slots 0x6EEAA0/0x6EEAF0, both command buffers 0x411910/0x4132d0,
#       aspMain image 0x768e60
#   [mem_base + 0x04000000, +8 KiB) DMEM + IMEM
#
# The process is briefly stopped (SIGSTOP, restored by an EXIT trap) so the
# snapshot is coherent — a spinning RSP loop keeps rewriting DMEM/IMEM, and
# a frozen display does not imply a static memory image. The active-task
# pointer word is sampled before and after the stop as a race check.
#
# Writes go only under a per-run
# /sdcard/Download/p07-memdump-<timestamp>/ directory.

PKG=org.mupen64plusae.turnip.pwnedbygary.debug:EmulationProcess
PID=$(pidof "$PKG")
if [ -z "$PID" ]; then
    echo "p07-memdump: no $PKG process; reproduce the freeze first"
    exit 1
fi

MM_RSP_MEM_HEX=4000000
RDRAM_WINDOW_BYTES=8388608
RSP_WINDOW_BYTES=8192
CURTASK_OFF_HEX=771D68

# awk hex helper: parse hex-digit strings exactly (doubles are exact below
# 2^53). Usage: a64 <hexstring> +|- <hexstring>  -> decimal (for dd)
#               a64 <hexstring> round ""          -> HEX to the next 64 KiB
# boundary (posix_memalign alignment in mupen64plus-core memory.c); it must
# stay hex so the result can be fed to the other operations unchanged.
a64() {
    awk -v a="$1" -v op="$2" -v b="$3" '
        function h2d(s,   v,i,d,ch){ v=0
            for (i=1; i<=length(s); i++) {
                ch=tolower(substr(s,i,1))
                d=index("0123456789abcdef",ch)-1
                if (d<0) return -1
                v=v*16+d
            }
            return v }
        function d2h(v,   s,r,dig){ dig="0123456789abcdef"; s=""
            if (v==0) return "0"
            while (v>0) { r=v-int(v/16)*16; s=substr(dig,r+1,1) s; v=int(v/16) }
            return s }
        BEGIN { x=h2d(a); y=h2d(b)
            if (op=="round") printf "%s", d2h(int((x + 65535) / 65536) * 65536)
            else printf "%.0f", (op=="-") ? x-y : x+y }'
}

DIR=/sdcard/Download/p07-memdump-$(date '+%Y%m%d-%H%M%S')
if [ -e "$DIR" ]; then
    DIR="$DIR-$$"
fi
mkdir -p "$DIR"
{
    echo "pid=$PID"
    echo "uid=$(id -u 2>/dev/null)"
    echo "started_at=$(date '+%Y-%m-%dT%H:%M:%S%z')"
} > "$DIR/run-metadata.txt"

# Memory-read canary: a real /proc/<pid>/mem read at the first mapping's
# start, with dd's exit status, byte count and stderr preserved. (Reading
# /proc/<pid>/stat proves nothing about /proc/<pid>/mem access.)
canary_addr=$(grep -m1 . /proc/$PID/maps 2>/dev/null | awk '{print $1}' | cut -d- -f1)
if [ -z "$canary_addr" ]; then
    echo "p07-memdump: cannot read /proc/$PID/maps; aborting"
    exit 1
fi
dd if=/proc/$PID/mem iflag=skip_bytes,count_bytes \
   skip=0x$canary_addr count=4 of="$DIR/canary-sample.bin" \
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

# Locate mem_base: the first qualifying anonymous read-write mapping of at
# least 500 MB (the 512 MiB posix_memalign allocation). Sizes are computed
# and compared in awk because the endpoints exceed 32 bits.
BASE=""
for region in $(grep 'rw-p' /proc/$PID/maps 2>/dev/null \
                    | grep '\[anon:scudo:secondary\]' | awk '{print $1}'); do
    s=${region%-*}
    e=${region#*-}
    size=$(a64 "$e" - "$s")
    [ -z "$size" ] && continue
    # Compare in awk: device `[` wraps decimal operands >= 2^31.
    big=$(awk -v s="$size" 'BEGIN{print (s >= 500000000) ? 1 : 0}')
    [ "$big" = "1" ] || continue
    BASE_MAP_START=$s
    BASE_SIZE=$size
    break
done

if [ -z "$BASE_MAP_START" ]; then
    echo "p07-memdump: mem_base mapping not found; see $DIR/run-metadata.txt"
    exit 1
fi
# mem_base is the 64 KiB-aligned posix_memalign pointer inside the mapping
# (the mapping start itself need not be aligned; the first capture proved a
# +0x1000 interior offset). Round up rather than using the raw map start.
BASE=$(a64 "$BASE_MAP_START" round "")
RSP_PHYS=$(a64 "$BASE" + "$MM_RSP_MEM_HEX")
CURTASK_PHYS=$(a64 "$BASE" + "$CURTASK_OFF_HEX")
{
    echo "mem_base_map_start=0x$BASE_MAP_START"
    echo "mem_base=0x$BASE"
    echo "mem_base_size=$BASE_SIZE"
    echo "rsp_mem_phys=$RSP_PHYS"
    echo "curtask_phys=$CURTASK_PHYS"
} >> "$DIR/run-metadata.txt"

# Coherent snapshot: stop the process for the reads, restore on any exit.
kill -STOP $PID 2>/dev/null
trap 'kill -CONT $PID 2>/dev/null' EXIT
sleep 1
stopped=$(sed 's/.*) //' /proc/$PID/stat 2>/dev/null | awk '{print $1}')
echo "stopped_state=$stopped" >> "$DIR/run-metadata.txt"
if [ "$stopped" != "T" ]; then
    echo "snapshot_warning=process not confirmed stopped; memory may be racy" \
        >> "$DIR/run-metadata.txt"
fi

peek_u32() {
    dd if=/proc/$PID/mem iflag=skip_bytes,count_bytes skip=$1 count=4 \
        2>/dev/null | od -An -tu4 2>/dev/null | tr -d ' \n'
}
echo "curtask_before=$(peek_u32 $CURTASK_PHYS)" >> "$DIR/run-metadata.txt"

read_window() {
    dd if=/proc/$PID/mem iflag=skip_bytes,count_bytes \
       skip=$1 count=$2 of="$DIR/$3" 2>/dev/null
    rc=$?
    got=$(stat -c %s "$DIR/$3" 2>/dev/null)
    if [ -n "$got" ] && [ "$got" -eq "$2" ]; then
        echo "read_ok $3 $got rc=$rc" >> "$DIR/run-metadata.txt"
    else
        echo "read_SHORT $3 expected=$2 got=${got:-0} rc=$rc" >> "$DIR/run-metadata.txt"
    fi
}

read_window "0x$BASE" $RDRAM_WINDOW_BYTES rdram-window.bin
read_window "$RSP_PHYS" $RSP_WINDOW_BYTES rspmem.bin

echo "curtask_after=$(peek_u32 $CURTASK_PHYS)" >> "$DIR/run-metadata.txt"

if [ ! -s "$DIR/rdram-window.bin" ] || [ ! -s "$DIR/rspmem.bin" ]; then
    echo "p07-memdump: dd produced no data; see $DIR/run-metadata.txt"
    exit 1
fi

sha256sum "$DIR"/*.bin 2>/dev/null > "$DIR/hashes.txt"
if [ ! -s "$DIR/hashes.txt" ]; then
    echo "hash_warning=sha256sum produced no output" >> "$DIR/run-metadata.txt"
fi
{
    echo "finished_at=$(date '+%Y-%m-%dT%H:%M:%S%z')"
    ls -la "$DIR"
} >> "$DIR/run-metadata.txt"
echo "p07-memdump: complete -> $DIR"
