#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

ndk="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
if [[ -z "$ndk" && -n "${ANDROID_HOME:-}" ]]; then
    ndk="${ANDROID_HOME}/ndk/26.1.10909125"
fi
if [[ -z "$ndk" || ! -d "$ndk" ]]; then
    echo "ANDROID_NDK_HOME (NDK 26.1.10909125) is required" >&2
    exit 2
fi

case "$(uname -s)" in
    Darwin) host_tag="darwin-x86_64" ;;
    *) host_tag="linux-x86_64" ;;
esac
clang="${ndk}/toolchains/llvm/prebuilt/${host_tag}/bin/aarch64-linux-android23-clang"
if [[ ! -x "$clang" ]]; then
    echo "missing NDK 26.1.10909125 ARM64 compiler: $clang" >&2
    exit 2
fi

"$clang" -std=gnu11 -fsyntax-only \
    -DNEW_DYNAREC=4 -DANDROID -D_GNU_SOURCE \
    -I mupen64plus-core/upstream/src \
    -I mupen64plus-core/upstream/subprojects/md5 \
    -I mupen64plus-core/upstream/subprojects/minizip \
    -I mupen64plus-core/upstream/subprojects/xxhash \
    -I mupen64plus-core/upstream/src/asm_defines/arm64-v8a \
    mupen64plus-core/upstream/src/device/r4300/new_dynarec/new_dynarec.c

echo "DD ARM64 dynarec syntax check passed (NDK 26.1.10909125)"