#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
# qemu writes a core file into the current folder when a test aborts.
ulimit -c 0

# The executables run under qemu's user-mode emulation, in a new PID namespace,
# as 32-bit bionic refuses a PID above 65535.
for tool in qemu-arm qemu-i386 qemu-aarch64 qemu-x86_64 unshare timeout; do
    if ! command -v "$tool" > /dev/null; then
        echo "SKIP: $tool not found" >&2
        exit 77
    fi
done
if ! unshare -r -p -f --kill-child true 2> /dev/null; then
    echo "SKIP: unshare cannot make a user and PID namespace here" >&2
    exit 77
fi

# Build with the NDK that Gradle uses: the version version_common.gradle names,
# from the Android SDK's ndk folder.
ndk_version="$(sed -n 's/^ *ndkVersion *"\([^"]*\)".*/\1/p' build_common/version_common.gradle)"
api="$(sed -n 's/^ *minSdkVersion *\([0-9][0-9]*\).*/\1/p' build_common/version_common.gradle)"
sdk=""
if [[ -f local.properties ]]; then
    sdk="$(sed -n 's/^sdk\.dir=//p' local.properties)"
fi
sdk="${sdk:-${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}}"
ndk="$sdk/ndk/$ndk_version"
revision="$(sed -n 's/^Pkg\.Revision *= *//p' "$ndk/source.properties" 2> /dev/null || true)"
if [[ -z "$ndk_version" || -z "$api" || "$revision" != "$ndk_version" ]]; then
    echo "NDK ${ndk_version:-?} not found in the Android SDK (local.properties' sdk.dir or ANDROID_HOME)" >&2
    exit 2
fi
clang="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/clang"
if [[ ! -x "$clang" ]]; then
    echo "missing NDK $ndk_version compiler: $clang" >&2
    exit 2
fi

# Build as the app does: for its minimum API level, with the defines in the
# core's main LOCAL_CFLAGS list (not those that an ABI's own block adds below
# it), and with undeclared functions only a warning, as in native_common.mk.
defines=()
while read -r define; do
    defines+=("$define")
done < <(awk '/^LOCAL_CFLAGS :=/ { f = 1 }
              f { for (i = 1; i <= NF; ++i) if ($i ~ /^-D/) print $i }
              f && !/\\$/ { exit }' mupen64plus-core/mupen64plus-core.mk)
if [[ ${#defines[@]} -eq 0 ]]; then
    echo "could not read the core's defines from mupen64plus-core.mk" >&2
    exit 2
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

minizip=mupen64plus-core/upstream/subprojects/minizip
failed=0
for abi in armeabi-v7a:armv7a-linux-androideabi:qemu-arm \
           x86:i686-linux-android:qemu-i386 \
           arm64-v8a:aarch64-linux-android:qemu-aarch64 \
           x86_64:x86_64-linux-android:qemu-x86_64; do
    IFS=: read -r name triple qemu <<< "$abi"
    exe="$work/minizip-$name"
    echo "$name:"
    if ! "$clang" --target="$triple$api" -static -fno-emulated-tls -Oz \
            -Wno-error=implicit-function-declaration "${defines[@]}" -I "$minizip" \
            tools/tests/minizip-android-test.c "$minizip/ioapi.c" "$minizip/unzip.c" \
            "$minizip/zip.c" -lz -o "$exe" 2> "$work/$name.log"; then
        sed 's/^/    /' "$work/$name.log"
        echo "  built: FAILED"
        failed=1
        continue
    fi
    # A function called undeclared is called with the wrong type, as minizip
    # called ftello and fseeko.
    if grep -q 'Wimplicit-function-declaration' "$work/$name.log"; then
        grep 'Wimplicit-function-declaration' "$work/$name.log" | sed 's/^/    /'
        echo "  every function declared: FAILED"
        failed=1
    else
        echo "  every function declared: ok"
    fi
    # PID 1 ignores the signals it has no handler for, so qemu cannot run as
    # PID 1: an abort() in the test would not end it. The shell takes PID 1.
    if ! timeout -k 10 60 unshare -r -p -f --kill-child sh -c '"$@"; exit $?' sh \
            "$qemu" "$exe" "$work/$name.zip"; then
        failed=1
    fi
done
if [[ "$failed" -ne 0 ]]; then
    echo "minizip Android check FAILED" >&2
    exit 1
fi
echo "minizip Android check passed (NDK $ndk_version, API $api)"
