#!/usr/bin/env bash
# Android check of the core's float compares, built as the app's debug and
# release builds build the core: the MIPS FPU compares that the interpreters use
# (tools/tests/fpu-compare-test.c) and rcheevos' achievement conditions on floats
# (tools/tests/rcheevos-float-test.c).
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

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

rcheevos=mupen64plus-core/rcheevos
sources=("$rcheevos/src/rc_compat.c" "$rcheevos/src/rc_util.c" "$rcheevos"/src/rcheevos/*.c
         mupen64plus-core/upstream/subprojects/md5/md5.c)

# A host build without fast math is the reference for rcheevos' results.
"${CC:-cc}" -O2 -fno-fast-math -w -I "$rcheevos/include" -I "$rcheevos/src" \
    tools/tests/rcheevos-float-test.c "${sources[@]}" -lm -o "$work/rcheevos-host"
"$work/rcheevos-host" > "$work/rcheevos-host.txt"

# Compiles files as ndk-build compiles the core's and links them as it links the
# core, but into a static executable: build <executable> <files...>. In the
# release build the objects are LLVM bitcode, and the link generates the code.
build() {
    local executable="$1" file object objects=()
    shift
    for file in "$@"; do
        object="$executable-${#objects[@]}.o"
        "$compiler" "${flags[@]}" -fno-emulated-tls -c "$file" -o "$object" || return 1
        objects+=("$object")
    done
    "$compiler" "${link_flags[@]}" -static -fno-emulated-tls "${objects[@]}" -lm -o "$executable"
}

failed=0
for abi in armeabi-v7a:qemu-arm x86:qemu-i386 arm64-v8a:qemu-aarch64 x86_64:qemu-x86_64; do
    IFS=: read -r name qemu <<< "$abi"
    # Gradle's debug build runs ndk-build with NDK_DEBUG=1, its release build with
    # NDK_DEBUG=0.
    for variant in debug:1 release:0; do
        IFS=: read -r kind ndk_debug <<< "$variant"
        echo "$name, $kind build:"
        # ndk-build's dry run prints the commands that Gradle's build runs. It still
        # runs the makefile's shell commands, which write the gitignored asm_defines
        # headers, as a build does.
        "$ndk/ndk-build" -n -B NDK_PROJECT_PATH=null \
            APP_BUILD_SCRIPT="$PWD/mupen64plus-core/Android.mk" \
            NDK_APPLICATION_MK="$PWD/mupen64plus-core/Application.mk" \
            APP_ABI="$name" NDK_ALL_ABIS="$name" NDK_DEBUG="$ndk_debug" APP_PLATFORM="android-$api" \
            NDK_OUT="$work/obj" NDK_LIBS_OUT="$work/lib" mupen64plus-core > "$work/$name-$kind.txt"
        # Every file of the core is built with the same flags: take cp1.c's. From the
        # command that links the core, take the target and the flags that control
        # the code it generates.
        read -r -a command <<< "$(grep -m1 '/device/r4300/cp1\.c ' "$work/$name-$kind.txt" || true)"
        read -r -a link <<< "$(grep -m1 -- ' -shared ' "$work/$name-$kind.txt" || true)"
        if [[ ${#command[@]} -eq 0 || ${#link[@]} -eq 0 ]]; then
            echo "could not find the core's compile and link commands in ndk-build's dry run" >&2
            exit 2
        fi
        compiler="${command[0]}"
        flags=()
        link_flags=()
        for ((i = 1; i < ${#command[@]}; ++i)); do
            case "${command[i]}" in
                -MF|-o) i=$((i + 1)) ;;
                -MMD|-MP|-c|*.c) ;;
                --sysroot)
                    flags+=(--sysroot "${command[i + 1]}")
                    link_flags+=(--sysroot "${command[i + 1]}")
                    i=$((i + 1))
                    ;;
                *) flags+=("${command[i]}") ;;
            esac
        done
        for ((i = 1; i < ${#link[@]}; ++i)); do
            case "${link[i]}" in
                -target) link_flags+=(-target "${link[i + 1]}"); i=$((i + 1)) ;;
                -O*|-f*|-m*|-no-canonical-prefixes) link_flags+=("${link[i]}") ;;
            esac
        done

        for test in fpu rcheevos; do
            files=("tools/tests/$test-compare-test.c")
            if [[ "$test" == rcheevos ]]; then
                files=(tools/tests/rcheevos-float-test.c "${sources[@]}")
            fi
            executable="$work/$test-$name-$kind"
            if ! build "$executable" "${files[@]}" 2> "$executable.log"; then
                sed 's/^/    /' "$executable.log"
                echo "  $test test built: FAILED"
                failed=1
                continue
            fi
            # PID 1 ignores the signals it has no handler for, so qemu cannot run as
            # PID 1: an abort() in the test would not end it. The shell takes PID 1.
            if ! timeout 60 unshare -r -p -f --kill-child sh -c '"$@"; exit $?' sh \
                    "$qemu" "$executable" > "$executable.txt" 2>&1; then
                sed 's/^/    /' "$executable.txt"
                echo "  $test test ran: FAILED"
                failed=1
                continue
            fi
            if [[ "$test" == fpu ]]; then
                echo "  FPU compares: ok ($(cat "$executable.txt"))"
            elif diff -u "$work/rcheevos-host.txt" "$executable.txt" > "$executable.diff"; then
                echo "  rcheevos float conditions, same as a strict host build: ok"
            else
                sed 's/^/    /' "$executable.diff"
                echo "  rcheevos float conditions, same as a strict host build: FAILED"
                failed=1
            fi
        done
    done
done
if [[ "$failed" -ne 0 ]]; then
    echo "float compare Android check FAILED" >&2
    exit 1
fi
echo "float compare Android check passed, debug and release builds (NDK $ndk_version, API $api)"
