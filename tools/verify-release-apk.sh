#!/usr/bin/env bash
# Verify release identity, original signing continuity, and absent DD tracing.
set -euo pipefail
if [[ $# -ne 2 ]]; then
  echo "Usage: $0 candidate.apk original-v336.apk" >&2
  exit 2
fi
sdk="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
[[ -n "$sdk" ]] || { echo "ANDROID_HOME is required" >&2; exit 2; }
tools="$sdk/build-tools/34.0.0"
candidate="$1"
original="$2"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
"$tools/aapt" dump badging "$candidate" > "$work/badging.txt"
grep -Eq "^package: name='org\.mupen64plusae\.turnip\.pwnedbygary' versionCode='337' versionName='3\.0\.337 " "$work/badging.txt"
if grep -q '^application-debuggable' "$work/badging.txt"; then
  echo "Refusing a debuggable release APK" >&2
  exit 1
fi
"$tools/apksigner" verify --print-certs "$candidate" > "$work/candidate-cert.txt"
"$tools/apksigner" verify --print-certs "$original" > "$work/original-cert.txt"
grep '^Signer #[0-9]* certificate SHA-256 digest:' "$work/candidate-cert.txt" > "$work/candidate-signers.txt"
grep '^Signer #[0-9]* certificate SHA-256 digest:' "$work/original-cert.txt" > "$work/original-signers.txt"
cmp "$work/candidate-signers.txt" "$work/original-signers.txt"
unzip -q "$candidate" 'lib/*/*.so' -d "$work/native"
find "$work/native" -name '*.so' -exec strings {} \; > "$work/native-strings.txt"
if grep -E 'DDSTART[0-9]|M64P_DD_STARTUP_DIAGNOSTICS|SetDdStartupDiagnostics' "$work/native-strings.txt"; then
  echo "Investigation tracing remains in the release libraries" >&2
  exit 1
fi
echo "PASS: v337 release identity, original v336 signer, and no DD trace markers"
sha256sum "$candidate"