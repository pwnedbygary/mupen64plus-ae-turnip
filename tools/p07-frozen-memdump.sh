#!/system/bin/sh
# P07: bounded frozen-memory dump for the audio-command investigation.
#
# Root, read-only, against a FROZEN emulation process. Fixed regions plus
# the active-descriptor pointer; mem_base is located by probing the app's one
# 512 MiB anonymous mapping (64 KiB posix_memalign alignment bounds the
# interior offset below 64 KiB) and validating the descriptor it resolves to.
#
# Addresses (jp/ek decompilation symbols; RDRAM physical = guest & 0x7fffff,
# MB_RDRAM_DRAM = 0, MB_RSP_MEM = RDRAM_16MB_SIZE + CART_ROM_MAX_SIZE =
# 0x1000000 + 0x4000000 per mupen64plus-core memory.c):
#
#   0x771D68   gCurAudioTask pointer (0x80771D68)    4 B
#   <pointer>  active AudioTask/OSTask: type, data_ptr/data_size at +0x30/
#              +0x34; the stored data_ptr is the VIRTUAL form (0x80411910 /
#              0x804132D0 for the two rspTask slots), so the probe compares
#              the masked physical form (0x411910 / 0x4132d0)
#   0x6EEAA0   gAudioCtx.rspTask[0] (0x806EEAA0)     0x50 B
#   0x6EEAF0   gAudioCtx.rspTask[1] (0x806EEAF0)     0x50 B
#   <data_ptr> audio command buffer (derived from the active descriptor)
#   mem_base + 0x5000000   DMEM + IMEM               8 KiB
#   mem_base + 0x768e60    aspMain image in RDRAM    4 KiB
#
# Nothing is written to the process; outputs and SHA-256s land in
# /sdcard/Download/p07-memdump/. run-metadata.txt records uid, a canary
# read of /proc/<pid>/stat, the resolved mem_base and descriptor, and
# probe-log.txt records one line per mem_base candidate (including
# unreadable ones) so a failed probe is diagnosable from the pull alone.

PKG=org.mupen64plusae.turnip.pwnedbygary.debug:EmulationProcess
PID=$(pidof "$PKG")
if [ -z "$PID" ]; then
    echo "p07-memdump: no $PKG process; reproduce the freeze first"
    exit 1
fi

DIR=/sdcard/Download/p07-memdump
mkdir -p "$DIR"
{
    echo "pid=$PID"
    echo "uid=$(id -u 2>/dev/null)"
    echo "started_at=$(date '+%Y-%m-%dT%H:%M:%S%z')"
} > "$DIR/run-metadata.txt"
: > "$DIR/probe-log.txt"

# Canary: distinguishes 'cannot read process memory' (e.g. not root) from
# 'addresses wrong'. /proc/<pid>/stat is a plain text read of 8 bytes.
canary=$(dd if=/proc/$PID/stat iflag=skip_bytes,count_bytes skip=0 count=8 \
            2>/dev/null | od -An -tx1 2>/dev/null | tr -d ' \n')
echo "canary_hex=$canary" >> "$DIR/run-metadata.txt"
if [ -z "$canary" ]; then
    echo "p07-memdump: cannot read /proc/$PID/stat; memory reads will fail too"
fi

# Read 4 bytes at host address $1 as a little-endian 32-bit decimal value.
peek_u32() {
    dd if=/proc/$PID/mem iflag=skip_bytes,count_bytes skip=$1 count=4 \
        2>/dev/null | od -An -tu4 2>/dev/null | tr -d ' \n'
}

# Locate mem_base and the active descriptor.
BASE=""; DESC_PTR_PHYS=""; CMDBUF_PHYS=""
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
            break
        fi
        off=$((off + 0x1000))
    done
    [ -n "$BASE" ] && break
done

echo "probe_regions=$REGIONS probe_candidates=$CANDIDATES" >> "$DIR/run-metadata.txt"

if [ -z "$BASE" ]; then
    if [ -z "$canary" ]; then
        echo "p07-memdump: cannot read process memory (canary failed; running as uid $(id -u 2>/dev/null)?); see $DIR/probe-log.txt"
    else
        echo "p07-memdump: active audio descriptor not found ($CANDIDATES candidates in $REGIONS region(s)); see $DIR/probe-log.txt"
    fi
    exit 1
fi
{
    echo "mem_base=0x$(printf '%x' $BASE)"
    echo "active_descriptor_phys=0x$(printf '%x' $DESC_PTR_PHYS)"
    echo "cmdbuf_phys=0x$(printf '%x' $CMDBUF_PHYS)"
} >> "$DIR/run-metadata.txt"

read_region() {
    dd if=/proc/$PID/mem iflag=skip_bytes,count_bytes \
       skip=$((BASE + $1)) count=$2 of="$DIR/$3" 2>/dev/null
}

read_region 0x771D68        4    curtask-pointer.bin
read_region $DESC_PTR_PHYS  64   active-descriptor.bin
read_region 0x6EEAA0        0x50 rspTask0.bin
read_region 0x6EEAF0        0x50 rspTask1.bin
read_region $CMDBUF_PHYS    0x1a0 cmdbuf.bin
read_region 0x411910        0x1a0 cmdbuf-slot0.bin
read_region 0x5000000       8192 rspmem.bin
read_region 0x768e60        4096 ucode-rdram.bin

if [ ! -s "$DIR/active-descriptor.bin" ] || [ ! -s "$DIR/rspmem.bin" ]; then
    echo "p07-memdump: dd produced no data; kernel may forbid /proc/$PID/mem"
    exit 1
fi

sha256sum "$DIR"/*.bin 2>/dev/null > "$DIR/hashes.txt"
{
    echo "finished_at=$(date '+%Y-%m-%dT%H:%M:%S%z')"
    ls -la "$DIR"
} >> "$DIR/run-metadata.txt"
echo "p07-memdump: complete -> $DIR"
