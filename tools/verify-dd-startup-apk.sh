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
echo "DDSTART2 comparison and DDSTART1 logging markers verified in packaged arm64-v8a native core"
shasum -a 256 "$1"