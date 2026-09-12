#!/usr/bin/env bash
set -euo pipefail
if [[ $# -ne 1 || ! -f "$1" ]]; then
    echo "Usage: bash tools/verify-dd-startup-apk.sh /path/to/build.apk" >&2
    exit 2
fi
core="$(mktemp)"
trap 'rm -f "$core"' EXIT
unzip -p "$1" lib/arm64-v8a/libmupen64plus-core.so > "$core"
grep -aFq 'DDSTART1 native:' "$core"
grep -aFq 'DDSTART2 boot selection:' "$core"
grep -aFq 'DDSTART2 disk: region=' "$core"
grep -aFq 'DDSTART1 limit reached;' "$core"
grep -aFq 'DDSTART3 reg read:' "$core"
grep -aFq 'DDSTART3 PI DMA start:' "$core"
grep -aFq 'DDSTART3 progress:' "$core"
grep -aFq 'DDSTART4 BM entry:' "$core"
grep -aFq 'DDSTART4 BM ack:' "$core"
grep -aFq 'DDSTART5 context:' "$core"
echo "DDSTART2 comparison, DDSTART1 logging, DDSTART3 trace markers, DDSTART4 BM handshake markers, and DDSTART5 context marker verified in packaged arm64-v8a native core"
shasum -a 256 "$1"