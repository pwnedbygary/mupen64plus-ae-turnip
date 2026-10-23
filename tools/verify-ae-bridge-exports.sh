#!/usr/bin/env bash
# Check that each ABI's libae-bridge.so in an APK exports every function AeBridgeLibrary
# declares. JNA looks a function up only when Java first calls it, so a missing export
# builds and installs cleanly and fails only at run time.
set -euo pipefail
export LC_ALL=C
if [[ $# -ne 1 ]]; then
  echo "Usage: $0 app.apk" >&2
  exit 2
fi
sdk="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
[[ -n "$sdk" ]] || { echo "ANDROID_HOME is required" >&2; exit 2; }
ndk="${ANDROID_NDK_HOME:-$sdk/ndk/26.1.10909125}"
nm=""
for candidate in "$ndk"/toolchains/llvm/prebuilt/*/bin/llvm-nm; do
  [[ -x "$candidate" ]] && nm="$candidate"
done
[[ -n "$nm" ]] || { echo "llvm-nm not found under $ndk" >&2; exit 2; }
repo="$(cd "$(dirname "$0")/.." && pwd)"
java="$repo/app/src/main/java/paulscode/android/mupen64plusae/jni/AeBridgeLibrary.java"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# Functions declared directly in the interface, not in its nested callback interfaces.
awk '
  /^[ \t]*(\/\*|\*)/ { next }
  {
    sub(/\/\/.*/, "")
    if (depth == 1 && match($0, /[A-Za-z_][A-Za-z0-9_]*[ \t]*\(/)) {
      name = substr($0, RSTART, RLENGTH)
      sub(/[ \t]*\($/, "", name)
      print name
    }
    depth += gsub(/\{/, "{") - gsub(/\}/, "}")
  }
' "$java" | sort -u > "$work/declared.txt"
[[ -s "$work/declared.txt" ]] || { echo "No functions found in $java" >&2; exit 2; }

unzip -q "$1" 'lib/*/libae-bridge.so' -d "$work/apk" \
  || { echo "No libae-bridge.so in $1" >&2; exit 2; }
status=0
for lib in "$work"/apk/lib/*/libae-bridge.so; do
  abi="$(basename "$(dirname "$lib")")"
  # Functions only (T, W): an exported variable of the same name would not help JNA.
  "$nm" -D --defined-only "$lib" | awk '$2 == "T" || $2 == "W" { print $NF }' \
    | sort -u > "$work/exported.txt"
  missing="$(comm -23 "$work/declared.txt" "$work/exported.txt" | paste -sd ' ' -)"
  if [[ -n "$missing" ]]; then
    echo "$abi: libae-bridge.so does not export: $missing" >&2
    status=1
  else
    echo "$abi: libae-bridge.so exports all $(wc -l < "$work/declared.txt" | tr -d ' ') functions"
  fi
done
exit "$status"
