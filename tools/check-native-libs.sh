#!/usr/bin/env bash
# Host checks of the native libraries in an APK or in native build output folders.
# Every library: its ELF machine matches its ABI folder, its SONAME is its file name, each
# NEEDED library is one of the checked libraries or an NDK system library for minSdkVersion,
# each undefined symbol (other than weak ones) is defined in its NEEDED tree, and its Android
# note names minSdkVersion and the pinned NDK (NDK r23b for the prebuilt libraries listed
# below). Then the libraries built from the sources of former prebuilts: libae-bridge.so
# exports EGLLoader's loader and its 173 g_gl* pointers; the core and Rice contain the libpng
# version of ndkLibs/png/png.h and no other libpng 1.6 version, and export no png_* symbol;
# the Glide64mk2 libraries contain no libpng.
#
# Usage: tools/check-native-libs.sh PATH...
# PATH is an APK, or a folder in which each library is in a folder named after its ABI, for
# example <module>/build/intermediates/cxx/Debug/<hash>/obj/local after a native build, or
# app/build/intermediates/merged_native_libs/debug/out/lib. Unstripped libraries, as in
# obj/local, also let the script check that Rice holds libpng's NEON filter code and that
# Glide64mk2's EGL library holds EGLLoader.
set -euo pipefail
export LC_ALL=C
if [[ $# -lt 1 ]]; then
  echo "Usage: $0 PATH..." >&2
  exit 2
fi
repo="$(cd "$(dirname "$0")/.." && pwd)"
gradle="$repo/build_common/version_common.gradle"
ndk_version="$(sed -n 's/.*ndkVersion "\([0-9.]*\)".*/\1/p' "$gradle")"
min_sdk="$(sed -n 's/.*minSdkVersion \([0-9]*\).*/\1/p' "$gradle")"
png_version="$(sed -n 's/^#define PNG_LIBPNG_VER_STRING "\(.*\)"$/\1/p' "$repo/ndkLibs/png/png.h")"
[[ -n "$ndk_version" && -n "$min_sdk" && -n "$png_version" ]] \
  || { echo "Cannot read the NDK version, minSdkVersion or libpng version" >&2; exit 2; }
sdk="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
if [[ -z "$sdk" && -f "$repo/local.properties" ]]; then
  sdk="$(sed -n 's/^sdk\.dir=//p' "$repo/local.properties")"
fi
ndk="$sdk/ndk/$ndk_version"
grep -qx "Pkg.Revision = $ndk_version" "$ndk/source.properties" 2>/dev/null \
  || { echo "NDK $ndk_version not found at $ndk (set ANDROID_HOME)" >&2; exit 2; }
bin=""
for candidate in "$ndk"/toolchains/llvm/prebuilt/*/bin; do
  [[ -x "$candidate/llvm-readelf" && -x "$candidate/llvm-nm" ]] && bin="$candidate"
done
[[ -n "$bin" ]] || { echo "llvm-readelf and llvm-nm not found under $ndk" >&2; exit 2; }
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

roots=()
n=0
for path in "$@"; do
  if [[ -d "$path" ]]; then
    roots+=("$path")
  elif [[ -f "$path" ]]; then
    n=$((n + 1))
    unzip -q "$path" 'lib/*' -d "$work/apk$n" || { echo "No lib/ folder in $path" >&2; exit 2; }
    roots+=("$work/apk$n/lib")
  else
    echo "Not found: $path" >&2
    exit 2
  fi
done

BIN="$bin" NDK_VERSION="$ndk_version" MIN_SDK="$min_sdk" PNG_VERSION="$png_version" \
  python3 - "${roots[@]}" <<'PY'
import collections
import hashlib
import os
import re
import subprocess
import sys
from pathlib import Path

BIN = Path(os.environ["BIN"])
SYSROOT_LIB = BIN.parent / "sysroot" / "usr" / "lib"
MIN_SDK = int(os.environ["MIN_SDK"])
major, minor, build = os.environ["NDK_VERSION"].split(".")
NDK_NOTE = ("r" + major + ("" if minor == "0" else chr(ord("a") + int(minor))), build)
PNG_VERSION = os.environ["PNG_VERSION"]
ABIS = {
    "arm64-v8a": ("AArch64", "aarch64-linux-android"),
    "armeabi-v7a": ("ARM", "arm-linux-androideabi"),
    "x86": ("Intel 80386", "i686-linux-android"),
    "x86_64": ("Advanced Micro Devices X86-64", "x86_64-linux-android"),
}
# Prebuilt libraries still committed in ndkLibs/libs, built in 2021 with NDK r23b. Remove each
# one from this list in the change that builds it from source.
PREBUILT_NOTE = dict.fromkeys(
    ("libSDL2_net.so", "libfreetype.so", "libsoundtouch.so", "libsoundtouch_fp.so",
     "libnatpmp.so", "libminiupnp-build.so"), ("r23b", "7779620"))
# Built elsewhere (the NDK, and the Oboe and JNA packages): their notes are not checked, and
# JNA's library has no SONAME.
NOT_BUILT_HERE = {"libc++_shared.so", "liboboe.so", "libjnidispatch.so"}
PNG_LIBS = ("libmupen64plus-core.so", "libmupen64plus-video-rice.so")
NO_PNG_LIBS = ("libmupen64plus-video-glide64mk2.so", "libmupen64plus-video-glide64mk2-egl.so")
EGL_LOADER = "_ZN9EGLLoader16loadEGLFunctionsEv"
PNG_VERSION_RE = re.compile(rb"(?<![0-9.])1\.6\.[0-9]+(?=\x00)")


def run(*args):
    return subprocess.run([str(a) for a in args], check=True, capture_output=True,
                          text=True).stdout


def dynamic_symbols(path):
    defined, undefined = {}, set()
    for line in run(BIN / "llvm-nm", "-D", "-P", path).splitlines():
        fields = line.split()
        name, kind = fields[0].split("@")[0], fields[1]
        if kind == "U":
            undefined.add(name)
        elif kind not in "wv":
            defined[name] = kind
    return defined, undefined


class Lib:
    def __init__(self, path):
        self.path = path
        info = run(BIN / "llvm-readelf", "--file-header", "--dynamic-table", "--notes", path)
        machine = re.search(r"^\s*Machine:\s*(.+?)\s*$", info, re.M)
        self.machine = machine.group(1) if machine else None
        self.soname = re.findall(r"\(SONAME\)\s+Library soname: \[(.*)\]", info)
        self.needed = re.findall(r"\(NEEDED\)\s+Shared library: \[(.*)\]", info)
        self.note = None
        note = re.search(r"NT_ANDROID_TYPE_IDENT\s*\n\s*description data:"
                         r"\s*((?:[0-9a-f]{2}\s*)+)", info)
        if note:
            # API level, then the NDK release and build number, 64 bytes each.
            data = bytes.fromhex(note.group(1))
            self.note = (int.from_bytes(data[0:4], "little"),
                         data[4:68].split(b"\0")[0].decode(),
                         data[68:132].split(b"\0")[0].decode())
        self.defined, self.undefined = dynamic_symbols(path)
        self._symtab = None

    def symtab(self):
        """Names in .symtab, or None for a stripped library."""
        if self._symtab is None:
            out = subprocess.run([str(BIN / "llvm-nm"), "-P", "--defined-only", str(self.path)],
                                 capture_output=True, text=True).stdout
            self._symtab = {line.split()[0] for line in out.splitlines() if line.strip()}
        return self._symtab or None


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


fails, passes, notes = [], [], []
libs = collections.defaultdict(dict)
for root in sys.argv[1:]:
    for path in sorted(Path(root).rglob("*.so")):
        abi = path.parent.name
        if abi not in ABIS or not path.is_file() or path.read_bytes()[:4] != b"\x7fELF":
            continue
        first = libs[abi].get(path.name)
        if first is None:
            libs[abi][path.name] = Lib(path)
        elif sha256(first.path) != sha256(path):
            notes.append(f"NOTE {abi}/{path.name}: {path} differs from {first.path}; "
                         "checked the first")
if not libs:
    print("FAIL: no libraries found in folders named after an ABI")
    sys.exit(1)

for abi in sorted(libs):
    machine, triple = ABIS[abi]
    stubs = {p.name: set(dynamic_symbols(p)[0])
             for p in sorted((SYSROOT_LIB / triple / str(MIN_SDK)).glob("*.so"))
             if p.read_bytes()[:4] == b"\x7fELF"}
    ndk_cxx_path = SYSROOT_LIB / triple / "libc++_shared.so"
    ndk_cxx = Lib(ndk_cxx_path) if ndk_cxx_path.is_file() else None
    print(f"{abi}: {len(libs[abi])} libraries: {' '.join(sorted(libs[abi]))}")
    library_passes = 0
    for name, lib in sorted(libs[abi].items()):
        problems = []
        if lib.machine != machine:
            problems.append(f"machine {lib.machine}, expected {machine}")
        if lib.soname != [name] and not (name in NOT_BUILT_HERE and not lib.soname):
            problems.append(f"SONAME {lib.soname}")
        if name not in NOT_BUILT_HERE:
            rel, bld = PREBUILT_NOTE.get(name, NDK_NOTE)
            if lib.note != (MIN_SDK, rel, bld):
                problems.append(f"Android note {lib.note}, expected {(MIN_SDK, rel, bld)}")
        exported, seen, queue = set(), set(), list(lib.needed)
        while queue:
            dep = queue.pop(0)
            if dep in seen:
                continue
            seen.add(dep)
            if dep in libs[abi]:
                exported |= set(libs[abi][dep].defined)
                queue += libs[abi][dep].needed
            elif dep in stubs:
                exported |= stubs[dep]
            elif dep == "libc++_shared.so" and ndk_cxx:
                notes.append(f"NOTE {abi}/{name}: libc++_shared.so is not among the checked "
                             "libraries; used the NDK's copy")
                exported |= set(ndk_cxx.defined)
                queue += ndk_cxx.needed
            else:
                problems.append(f"NEEDED {dep} is neither a checked library nor an NDK system "
                                f"library for API {MIN_SDK}")
        missing = sorted(lib.undefined - exported)
        if missing:
            problems.append(f"{len(missing)} undefined symbols not defined in its NEEDED tree: "
                            + " ".join(missing[:10]) + (" ..." if len(missing) > 10 else ""))
        if problems:
            fails += [f"FAIL {abi}/{name}: {p}" for p in problems]
        else:
            library_passes += 1
    if library_passes == len(libs[abi]):
        passes.append(f"{abi}: all {library_passes} libraries pass the ELF, NEEDED, symbol and "
                      "note checks")

    bridge = libs[abi].get("libae-bridge.so")
    if bridge:
        pointers = [n for n, k in bridge.defined.items() if n.startswith("g_gl") and k in "BDRV"]
        if bridge.defined.get(EGL_LOADER) == "T" and len(pointers) == 173:
            passes.append(f"{abi}/libae-bridge.so: exports EGLLoader::loadEGLFunctions() and "
                          "173 g_gl* pointers")
        else:
            fails.append(f"FAIL {abi}/libae-bridge.so: EGLLoader::loadEGLFunctions() "
                         f"{bridge.defined.get(EGL_LOADER, 'not exported')}, {len(pointers)} g_gl* "
                         "pointers exported (expected T and 173)")
    name = "libmupen64plus-video-glide64mk2-egl.so"
    egl = libs[abi].get(name)
    if egl:
        if egl.symtab() is None:
            notes.append(f"NOTE {abi}/{name}: stripped, EGLLoader not checked")
        elif EGL_LOADER in egl.symtab():
            passes.append(f"{abi}/{name}: contains EGLLoader")
        else:
            fails.append(f"FAIL {abi}/{name}: no EGLLoader")
    for name in PNG_LIBS:
        lib = libs[abi].get(name)
        if not lib:
            continue
        versions = sorted({v.decode() for v in PNG_VERSION_RE.findall(lib.path.read_bytes())})
        exports = sorted(n for n in lib.defined if n.startswith("png_"))
        if versions == [PNG_VERSION] and not exports:
            passes.append(f"{abi}/{name}: libpng {PNG_VERSION}, no png_* exported")
        else:
            fails.append(f"FAIL {abi}/{name}: libpng versions {versions or 'none'} (expected "
                         f"{PNG_VERSION}), {len(exports)} png_* exported")
        if name == "libmupen64plus-video-rice.so" and abi in ("arm64-v8a", "armeabi-v7a"):
            if lib.symtab() is None:
                notes.append(f"NOTE {abi}/{name}: stripped, libpng's NEON code not checked")
            elif "png_init_filter_functions_neon" in lib.symtab():
                passes.append(f"{abi}/{name}: holds libpng's NEON filter code"
                              " (png_init_filter_functions_neon)")
            else:
                fails.append(f"FAIL {abi}/{name}: no png_init_filter_functions_neon")
    for name in NO_PNG_LIBS:
        lib = libs[abi].get(name)
        if lib:
            versions = sorted({v.decode() for v in PNG_VERSION_RE.findall(lib.path.read_bytes())})
            if versions:
                fails.append(f"FAIL {abi}/{name}: contains libpng {versions}")
            else:
                passes.append(f"{abi}/{name}: no libpng")

for line in notes:
    print(line)
for line in passes:
    print("PASS " + line)
for line in fails:
    print(line)
print("RESULT: " + ("FAIL" if fails else "PASS"))
sys.exit(1 if fails else 0)
PY
