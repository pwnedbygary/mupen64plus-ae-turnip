#!/usr/bin/env bash
# Host checks of the native libraries in an APK or in native build output folders.
# Every library: its ELF machine matches its ABI folder, its SONAME is its file name, each
# NEEDED library is one of the checked libraries or an NDK system library for minSdkVersion,
# each undefined symbol (other than weak ones) is defined in its NEEDED tree, and its Android
# note names minSdkVersion and the pinned NDK (NDK r23b for the prebuilt libraries listed
# below). Then the libraries built from the sources of former prebuilts: libae-bridge.so
# exports EGLLoader's loader and its 173 g_gl* pointers; the core and Rice contain the libpng
# version of ndkLibs/png/png.h and no other libpng 1.6 version, and export no png_* symbol;
# the Glide64mk2 libraries contain no libpng; libminiupnp-bridge.so takes its 12 miniupnpc
# imports from libminiupnp-build.so and its 5 libnatpmp imports from libnatpmp.so; the
# User-Agent in libminiupnp-build.so names the version of
# ndkLibs/miniupnpc/include/miniupnpc.h; SDLNet_Linked_Version() in libSDL2_net.so stores the
# version of ndkLibs/SDL_net/include/SDL_net.h (read from its machine code).
#
# Usage: tools/check-native-libs.sh [--expect LIB[,LIB...]]... PATH...
# PATH is an APK, or a folder in which each library is in a folder named after its ABI, for
# example <module>/build/intermediates/cxx/Debug/<hash>/obj/local after an ndk-build module's
# native build, <module>/build/intermediates/cxx/<Variant>/<hash>/obj after a CMake module's,
# or app/build/intermediates/merged_native_libs/debug/out/lib. Unstripped libraries, as in
# those obj folders, also let the script check that Rice holds libpng's NEON filter code and
# that Glide64mk2's EGL library holds EGLLoader.
# Missing libraries: the checks above that name a library run only where it is found, so an
# APK must hold every library that they name (CHECKED_BY_NAME below) for each of its ABIs,
# or the check fails. A folder often holds one module's libraries, so for folders the check
# fails on a missing library only for the libraries named with --expect (comma-separated,
# and the option can be repeated): each ABI found in the folders must hold each of them. For
# example, for miniupnp-bridge's folder: --expect
# libminiupnp-bridge.so,libminiupnp-build.so,libnatpmp.so. With an APK, --expect adds to its
# list.
set -euo pipefail
export LC_ALL=C
expect=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --expect)
      [[ $# -ge 2 ]] || { echo "--expect needs a list of libraries" >&2; exit 2; }
      expect="$expect,$2"
      shift 2
      ;;
    --expect=*)
      expect="$expect,${1#--expect=}"
      shift
      ;;
    --)
      shift
      break
      ;;
    -*)
      echo "Unknown option: $1" >&2
      exit 2
      ;;
    *)
      break
      ;;
  esac
done
IFS=, read -ra expect_names <<< "$expect"
for name in "${expect_names[@]}"; do
  [[ -z $name || ($name == lib*.so && $name != */*) ]] \
    || { echo "--expect: $name is not a library file name" >&2; exit 2; }
done
if [[ $# -lt 1 ]]; then
  echo "Usage: $0 [--expect LIB[,LIB...]]... PATH..." >&2
  exit 2
fi
repo="$(cd "$(dirname "$0")/.." && pwd)"
gradle="$repo/build_common/version_common.gradle"
ndk_version="$(sed -n 's/.*ndkVersion "\([0-9.]*\)".*/\1/p' "$gradle")"
min_sdk="$(sed -n 's/.*minSdkVersion \([0-9]*\).*/\1/p' "$gradle")"
png_version="$(sed -n 's/^#define PNG_LIBPNG_VER_STRING "\(.*\)"$/\1/p' "$repo/ndkLibs/png/png.h")"
[[ -n "$ndk_version" && -n "$min_sdk" && -n "$png_version" ]] \
  || { echo "Cannot read the NDK version, minSdkVersion or libpng version" >&2; exit 2; }
sdl_net_version="$(sed -n \
  's/^#define SDL_NET_\(MAJOR_VERSION\|MINOR_VERSION\|PATCHLEVEL\) *\([0-9][0-9]*\)$/\2/p' \
  "$repo/ndkLibs/SDL_net/include/SDL_net.h" | paste -sd.)"
miniupnpc_version="$(sed -n 's/^#define MINIUPNPC_VERSION[[:space:]]*"\(.*\)"$/\1/p' \
  "$repo/ndkLibs/miniupnpc/include/miniupnpc.h")"
[[ $sdl_net_version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ && -n "$miniupnpc_version" ]] \
  || { echo "Cannot read the SDL_net or miniupnpc version" >&2; exit 2; }
sdk="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
if [[ -z "$sdk" && -f "$repo/local.properties" ]]; then
  sdk="$(sed -n 's/^sdk\.dir=//p' "$repo/local.properties")"
fi
ndk="$sdk/ndk/$ndk_version"
grep -qx "Pkg.Revision = $ndk_version" "$ndk/source.properties" 2>/dev/null \
  || { echo "NDK $ndk_version not found at $ndk (set ANDROID_HOME)" >&2; exit 2; }
bin=""
for candidate in "$ndk"/toolchains/llvm/prebuilt/*/bin; do
  [[ -x "$candidate/llvm-readelf" && -x "$candidate/llvm-nm" && -x "$candidate/llvm-objdump" ]] \
    && bin="$candidate"
done
[[ -n "$bin" ]] || { echo "llvm-readelf, llvm-nm and llvm-objdump not found under $ndk" >&2; exit 2; }
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

roots=()
labels=()
n=0
for path in "$@"; do
  if [[ -d "$path" ]]; then
    roots+=("$path")
    labels+=("dir")
  elif [[ -f "$path" ]]; then
    n=$((n + 1))
    unzip -q "$path" 'lib/*' -d "$work/apk$n" || { echo "No lib/ folder in $path" >&2; exit 2; }
    roots+=("$work/apk$n/lib")
    labels+=("apk $path")
  else
    echo "Not found: $path" >&2
    exit 2
  fi
done

BIN="$bin" NDK_VERSION="$ndk_version" MIN_SDK="$min_sdk" PNG_VERSION="$png_version" \
  SDL_NET_VERSION="$sdl_net_version" MINIUPNPC_VERSION="$miniupnpc_version" \
  EXPECT="$expect" ROOT_LABELS="$(printf '%s\n' "${labels[@]}")" \
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
    ("libfreetype.so", "libsoundtouch.so", "libsoundtouch_fp.so"), ("r23b", "7779620"))
# Built elsewhere (the NDK, and the Oboe and JNA packages): their notes are not checked, and
# JNA's library has no SONAME.
NOT_BUILT_HERE = {"libc++_shared.so", "liboboe.so", "libjnidispatch.so"}
PNG_LIBS = ("libmupen64plus-core.so", "libmupen64plus-video-rice.so")
NO_PNG_LIBS = ("libmupen64plus-video-glide64mk2.so", "libmupen64plus-video-glide64mk2-egl.so")
EGL_LOADER = "_ZN9EGLLoader16loadEGLFunctionsEv"
PNG_VERSION_RE = re.compile(rb"(?<![0-9.])1\.6\.[0-9]+(?=\x00)")
SDL_NET_VERSION = tuple(int(n) for n in os.environ["SDL_NET_VERSION"].split("."))
MINIUPNPC_VERSION = os.environ["MINIUPNPC_VERSION"]
# The miniupnpc and libnatpmp functions that miniupnp-bridge/src calls, by library.
BRIDGE_IMPORTS = {
    "libminiupnp-build.so": (
        "FreeUPNPUrls", "GetUPNPUrls", "UPNP_AddPortMapping", "UPNP_DeletePortMapping",
        "UPNP_GetConnectionTypeInfo", "UPNP_GetExternalIPAddress",
        "UPNP_GetGenericPortMappingEntry", "UPNP_GetValidIGD", "freeUPNPDevlist", "miniwget",
        "parserootdesc", "upnpDiscover"),
    "libnatpmp.so": (
        "closenatpmp", "initnatpmp", "readnatpmpresponseorretry", "sendnewportmappingrequest",
        "sendpublicaddressrequest"),
}
USER_AGENT_RE = re.compile(rb"User-Agent: ([ -~]*?)\r\n")
MINIUPNPC_RE = re.compile(rb"MiniUPnPc/([0-9][0-9.]*[0-9])")
# Every library that a check below names. An APK must hold each of them for each of its ABIs.
CHECKED_BY_NAME = sorted({"libae-bridge.so", *PNG_LIBS, *NO_PNG_LIBS, *PREBUILT_NOTE,
                          "libminiupnp-bridge.so", *BRIDGE_IMPORTS, "libSDL2_net.so"})
EXPECT = sorted({name for name in os.environ["EXPECT"].split(",") if name})
LABELS = os.environ["ROOT_LABELS"].split("\n")


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


IMM = r"(-?(?:0x[0-9a-f]+|[0-9]+))"
X86_REGS = {}
for wide, narrow in (("ax", "al"), ("bx", "bl"), ("cx", "cl"), ("dx", "dl"), ("si", "sil"),
                     ("di", "dil"), ("bp", "bpl")):
    X86_REGS.update(dict.fromkeys(("r" + wide, "e" + wide, wide, narrow), wide))
for number in range(8, 16):
    X86_REGS.update(dict.fromkeys([f"r{number}{s}" for s in ("", "d", "w", "b")], f"r{number}"))


def stored_bytes(lib, abi, symbol):
    """The bytes that the exported function `symbol` stores with constant values, by offset
    from the lowest one, as SDLNet_Linked_Version() fills its static version struct; read
    from the function's machine code up to its first return. None if a store's value or
    address is not a constant that this reads."""
    found = re.search(r"^\s*\d+:\s+([0-9a-f]+)\s+(\d+)\s+FUNC\s.*\s" + re.escape(symbol)
                      + r"(?:@\S*)?$", run(BIN / "llvm-readelf", "--dyn-syms", "--wide",
                                           lib.path), re.M)
    if not found:
        return None
    value, size = int(found.group(1), 16), int(found.group(2))
    start = value & ~1
    args = [BIN / "llvm-objdump", "-d", "--no-show-raw-insn", f"--start-address={start:#x}",
            f"--stop-address={start + size:#x}"]
    if abi == "armeabi-v7a" and value & 1:
        args.append("--triple=thumbv7-none-linux-androideabi")
    x86 = abi.startswith("x86")
    comment = {"arm64-v8a": "//", "armeabi-v7a": "@"}.get(abi, "#")
    regs, stores = {}, []

    def reg(name):
        name = name.lstrip("%")
        if name in ("wzr", "xzr"):
            return "zero"
        if x86:
            return X86_REGS.get(name, name)
        return "r" + name[1:] if re.fullmatch(r"[wx][0-9]+", name) else name

    def value_of(name):
        name = reg(name)
        return 0 if name == "zero" else regs.get(name)

    pending = None  # a RIP-relative store, whose address needs the next instruction's address
    for line in run(*args, lib.path).splitlines():
        insn = re.match(r"^\s*([0-9a-f]+):\s+(\S+)\s*(.*)$", line)
        if not insn:
            continue
        if pending is not None:
            base, displacement, width, data = stores[pending]
            stores[pending] = (base, int(insn.group(1), 16) + displacement, width, data)
            pending = None
        op = insn.group(2).split(".")[0]
        text = insn.group(3).partition(comment)[0].strip()
        if op in ("ret", "retq", "retl") or (op == "bx" and text == "lr") \
                or (op == "pop" and "pc" in text):
            break
        if x86:
            move = re.fullmatch(r"\$" + IMM + r",\s*%(\w+)", text)
            if op.startswith("mov") and move:
                regs[reg(move.group(2))] = int(move.group(1), 0)
                continue
            zeroed = re.fullmatch(r"%(\w+),\s*%(\w+)", text)
            if op.startswith("xor") and zeroed and reg(zeroed.group(1)) == reg(zeroed.group(2)):
                regs[reg(zeroed.group(1))] = 0
                continue
            store = re.fullmatch(r"(?:\$" + IMM + r"|%(\w+)),\s*" + IMM + r"?\(%(\w+)\)", text)
            if op in ("movb", "movw", "movl") and store:
                data = int(store.group(1), 0) if store.group(1) else value_of(store.group(2))
                base, offset = store.group(4), int(store.group(3) or "0", 0)
                if base == "rip":
                    # The address is the next instruction's plus the displacement. objdump's
                    # comment with the address is no help: after an "imm =" comment it moves
                    # to a line of its own.
                    pending = len(stores)
                stores.append((base, offset, {"b": 1, "w": 2, "l": 4}[op[-1]], data))
                continue
        else:
            move = re.fullmatch(r"(\w+),\s*#" + IMM, text)
            if op in ("mov", "movs", "movw", "movz") and move:
                regs[reg(move.group(1))] = int(move.group(2), 0)
                continue
            move = re.fullmatch(r"(\w+),\s*(wzr|xzr)(?:,\s*#" + IMM + ")?", text)
            if op in ("mov", "orr") and move:
                regs[reg(move.group(1))] = int(move.group(3), 0) if move.group(3) else 0
                continue
            store = re.fullmatch(r"(\w+),\s*\[(\w+)(?:,\s*#" + IMM + r")?\]", text)
            if op in ("strb", "strh", "str", "sturb", "sturh", "stur") and store:
                width = {"b": 1, "h": 2}.get(op[-1], 8 if store.group(1).startswith("x") else 4)
                stores.append((reg(store.group(2)), int(store.group(3) or "0", 0), width,
                               value_of(store.group(1))))
                continue
        target = re.match(r"%?(\w+)", text if not x86 else text.rpartition(",")[2].strip())
        if target:
            regs.pop(reg(target.group(1)), None)
    if pending is not None or not stores or len({s[0] for s in stores}) != 1 \
            or any(s[1] is None or s[3] is None for s in stores):
        return None
    memory = {}
    for _, offset, width, data in stores:
        for i in range(width):
            memory[offset + i] = (data >> (8 * i)) & 0xFF
    low = min(memory)
    return tuple(memory[low + i] for i in range(len(memory))) \
        if sorted(memory) == list(range(low, low + len(memory))) else None


fails, passes, notes = [], [], []
libs = collections.defaultdict(dict)
# Library names by ABI, for each APK and for all folders together ("dir").
found = collections.defaultdict(lambda: collections.defaultdict(set))
for root, label in zip(sys.argv[1:], LABELS):
    for path in sorted(Path(root).rglob("*.so")):
        abi = path.parent.name
        if abi not in ABIS or not path.is_file() or path.read_bytes()[:4] != b"\x7fELF":
            continue
        found[label][abi].add(path.name)
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
    bridge = libs[abi].get("libminiupnp-bridge.so")
    for name, functions in BRIDGE_IMPORTS.items():
        lib = libs[abi].get(name)
        if not bridge or not lib:
            continue
        imported = sorted(n for n in bridge.undefined if n in lib.defined)
        undefined = sorted(n for n in functions if lib.defined.get(n) != "T")
        if name in bridge.needed and imported == sorted(functions) and not undefined:
            passes.append(f"{abi}/libminiupnp-bridge.so: its {len(functions)} imports from "
                          f"{name} resolve there")
        else:
            fails.append(f"FAIL {abi}/libminiupnp-bridge.so: imports {imported or 'nothing'} "
                         f"from {name}, expected {sorted(functions)}; not functions there: "
                         f"{undefined or 'none'}")
    lib = libs[abi].get("libminiupnp-build.so")
    if lib:
        data = lib.path.read_bytes()
        agents = sorted({a.decode() for a in USER_AGENT_RE.findall(data)})
        versions = sorted({v.decode() for v in MINIUPNPC_RE.findall(data)})
        if agents and versions == [MINIUPNPC_VERSION] \
                and all(a.endswith(" MiniUPnPc/" + MINIUPNPC_VERSION) for a in agents):
            passes.append(f"{abi}/libminiupnp-build.so: User-Agent "
                          + ", ".join(f'"{a}"' for a in agents))
        else:
            fails.append(f"FAIL {abi}/libminiupnp-build.so: User-Agent "
                         f"{agents or 'not found'}, MiniUPnPc versions {versions or 'none'} "
                         f"(expected {MINIUPNPC_VERSION})")
    lib = libs[abi].get("libSDL2_net.so")
    if lib:
        stored = stored_bytes(lib, abi, "SDLNet_Linked_Version")
        shown = ".".join(map(str, stored)) if stored else "nothing this script can read"
        expected = ".".join(map(str, SDL_NET_VERSION))
        if stored == SDL_NET_VERSION:
            passes.append(f"{abi}/libSDL2_net.so: SDLNet_Linked_Version() stores {shown}")
        else:
            fails.append(f"FAIL {abi}/libSDL2_net.so: SDLNet_Linked_Version() stores {shown} "
                         f"(expected {expected})")

for label, abis in sorted(found.items()):
    where = "the folders" if label == "dir" else label[len("apk "):]
    required = sorted(set(EXPECT) | (set() if label == "dir" else set(CHECKED_BY_NAME)))
    if not required:
        notes.append("NOTE the folders: missing libraries not checked (no --expect)")
        continue
    for abi in sorted(abis):
        missing = [name for name in required if name not in abis[abi]]
        if missing:
            fails.append(f"FAIL {abi}: missing from {where}: {' '.join(missing)}")
        else:
            passes.append(f"{abi}: all {len(required)} expected libraries are in {where}")

for line in notes:
    print(line)
for line in passes:
    print("PASS " + line)
for line in fails:
    print(line)
print("RESULT: " + ("FAIL" if fails else "PASS"))
sys.exit(1 if fails else 0)
PY
