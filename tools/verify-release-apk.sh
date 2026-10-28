#!/usr/bin/env bash
# Verify release identity, original signing continuity, and absent DD tracing.
set -euo pipefail
if [[ $# -ne 2 ]]; then
  echo "Usage: $0 candidate.apk previous-release.apk" >&2
  exit 2
fi
version_file="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/build_common/version_common.gradle"
[[ -f "$version_file" ]] || { echo "Cannot find $version_file" >&2; exit 2; }
version_code="$(sed -nE 's/^[[:space:]]*versionCode[[:space:]]*=?[[:space:]]*([0-9]+)[[:space:]]*$/\1/p' "$version_file")"
version_name="$(sed -nE 's/^[[:space:]]*versionName[[:space:]]*=?[[:space:]]*"([0-9]+(\.[0-9]+)*) "[[:space:]]*\+[[:space:]]*gitCommitHash\(\)[[:space:]]*$/\1/p' "$version_file")"
if [[ ! "$version_code" =~ ^[0-9]+$ ]]; then
  echo "Cannot read exactly one versionCode from $version_file" >&2
  exit 2
fi
if [[ ! "$version_name" =~ ^[0-9]+(\.[0-9]+)*$ ]]; then
  echo "Cannot read exactly one versionName of the form \"<x.y.z> \" + gitCommitHash() from $version_file" >&2
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
if ! grep -Eq "^package: name='org\.mupen64plusae\.turnip\.pwnedbygary' versionCode='$version_code' versionName='${version_name//./[.]} " "$work/badging.txt"; then
  echo "The candidate is not org.mupen64plusae.turnip.pwnedbygary with versionCode $version_code and versionName $version_name plus the commit hash" >&2
  exit 1
fi
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
echo "PASS: $version_name (versionCode $version_code) release identity, previous release signer, and no DD trace markers"
sha256sum "$candidate"