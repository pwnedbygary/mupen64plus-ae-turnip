#!/usr/bin/env bash
# Host checks of the native libraries in an APK or in native build output folders.
# Every library: its ELF machine matches its ABI folder, its SONAME is its file name, each
# NEEDED library is one of the checked libraries or an NDK system library for minSdkVersion,
# each undefined symbol (other than weak ones) is defined in its NEEDED tree, and its Android
# note names minSdkVersion and the pinned NDK. Then the libraries built from the sources of
# former prebuilts: libae-bridge.so
# exports EGLLoader's loader and its 173 g_gl* pointers; the core and Rice contain the libpng
# version of ndkLibs/png/png.h and no other libpng 1.6 version, and export no png_* symbol;
# the Glide64mk2 libraries contain no libpng; libminiupnp-bridge.so takes its 12 miniupnpc
# imports from libminiupnp-build.so and its 5 libnatpmp imports from libnatpmp.so; the
# User-Agent in libminiupnp-build.so names the version of
# ndkLibs/miniupnpc/include/miniupnpc.h; SDLNet_Linked_Version() in libSDL2_net.so stores the
# version of ndkLibs/SDL_net/include/SDL_net.h (read from its machine code).
# libsoundtouch.so exports putSamples for 16-bit integer samples and libsoundtouch_fp.so for
# float samples, and both hold the version of ndkLibs/soundtouch/include/SoundTouch.h (the value
# getVersionId() returns, and the version string). Unstripped, as the audio module's CMake build
# leaves them in mupen64plus-audio-android/build/intermediates/cxx/<Variant>/<hash>/obj, their
# line tables must show none of the code of ndkLibs/soundtouch/source/SoundTouch that only
# ST_SIMD_AVOID_UNALIGNED builds. libfreetype.so exports each FT_* and png_* function that
# libmupen64plus-video-GLideN64.so imports, and GLideN64 defines no png_* function itself;
# FT_New_Library() in libfreetype.so stores the version of
# ndkLibs/freetype/include/freetype/freetype.h (read from its machine code); libfreetype.so
# holds the libpng version of ndkLibs/png/png.h and no other, and the module classes that
# ndkLibs/config/freetype-android/ftmodule.h lists and none of the other classes of FreeType's
# own include/freetype/config/ftmodule.h.
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
soundtouch_h="$repo/ndkLibs/soundtouch/include/SoundTouch.h"
st_version="$(sed -n 's/^#define SOUNDTOUCH_VERSION[[:space:]]*"\([^"]*\)".*/\1/p' "$soundtouch_h")"
st_version_id="$(sed -n 's/^#define SOUNDTOUCH_VERSION_ID[[:space:]]*(\([0-9]*\)).*/\1/p' "$soundtouch_h")"
[[ -n "$st_version" && -n "$st_version_id" ]] \
  || { echo "Cannot read the SoundTouch version from $soundtouch_h" >&2; exit 2; }
export ST_VERSION="$st_version" ST_VERSION_ID="$st_version_id" \
  ST_SOURCE="$repo/ndkLibs/soundtouch/source/SoundTouch"
freetype_h="$repo/ndkLibs/freetype/include/freetype/freetype.h"
ft_version="$(sed -n 's/^#define FREETYPE_\(MAJOR\|MINOR\|PATCH\) *\([0-9][0-9]*\)$/\2/p' \
  "$freetype_h" | paste -sd.)"
[[ $ft_version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] \
  || { echo "Cannot read the FreeType version from $freetype_h" >&2; exit 2; }
export FT_VERSION="$ft_version" FT_SOURCE="$repo/ndkLibs/freetype" \
  FT_CONFIG="$repo/ndkLibs/config/freetype-android"
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
[[ -x "$bin/llvm-objdump" && -x "$bin/llvm-dwarfdump" ]] \
  || { echo "llvm-objdump and llvm-dwarfdump not found in $bin" >&2; exit 2; }
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
import struct
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
SOUNDTOUCH_LIBS = {"libsoundtouch.so": "short", "libsoundtouch_fp.so": "float"}
ST_PUT_SAMPLES = {"short": "_ZN10soundtouch10SoundTouch10putSamplesEPKsj",
                  "float": "_ZN10soundtouch10SoundTouch10putSamplesEPKfj"}
ST_GET_VERSION_ID = "_ZN10soundtouch10SoundTouch12getVersionIdEv"
ST_VERSION = os.environ["ST_VERSION"]
ST_VERSION_ID = int(os.environ["ST_VERSION_ID"])
ST_VERSION_RE = re.compile(rb"(?<![0-9A-Za-z.])" + re.escape(ST_VERSION.encode()) + rb"\x00")
ST_SIMD = "ST_SIMD_AVOID_UNALIGNED"
FREETYPE = "libfreetype.so"
GLIDEN64 = "libmupen64plus-video-GLideN64.so"
# The FreeType and libpng functions that GLideN64 imports from libfreetype.so.
GLIDEN64_IMPORTS = (
    "FT_Done_Face", "FT_Done_FreeType", "FT_Init_FreeType", "FT_Load_Char", "FT_New_Face",
    "FT_Set_Pixel_Sizes", "png_create_info_struct", "png_create_read_struct",
    "png_create_write_struct", "png_destroy_read_struct", "png_destroy_write_struct",
    "png_get_IHDR", "png_get_rowbytes", "png_get_valid", "png_init_io", "png_read_end",
    "png_read_info", "png_read_rows", "png_read_update_info", "png_set_expand_gray_1_2_4_to_8",
    "png_set_filler", "png_set_gray_to_rgb", "png_set_IHDR", "png_set_interlace_handling",
    "png_set_longjmp_fn", "png_set_palette_to_rgb", "png_set_sBIT", "png_set_sig_bytes",
    "png_set_strip_16", "png_set_tRNS_to_alpha", "png_sig_cmp", "png_write_end",
    "png_write_info", "png_write_row")
FT_VERSION = tuple(int(n) for n in os.environ["FT_VERSION"].split("."))


def module_classes(path):
    """The module classes that an ftmodule.h registers, in its order."""
    code = re.sub(r"/\*.*?\*/", "", Path(path).read_text(), flags=re.S)
    return re.findall(r"^\s*FT_USE_MODULE\(\s*\w+\s*,\s*(\w+)\s*\)", code, re.M)


FT_MODULES = module_classes(os.environ["FT_CONFIG"] + "/ftmodule.h")
FT_OTHER_MODULES = sorted(set(module_classes(
    os.environ["FT_SOURCE"] + "/include/freetype/config/ftmodule.h")) - set(FT_MODULES))
# The version check reads the three FT_Int fields after the FT_Memory pointer.
FT_LIBRARY_REC = re.search(r"typedef\s+struct\s+FT_LibraryRec_\s*\{(.*?)\}",
                           Path(os.environ["FT_SOURCE"]
                                + "/include/freetype/internal/ftobjs.h").read_text(), re.S)
FT_VERSION_READABLE = bool(FT_LIBRARY_REC) and re.findall(
    r"^\s*(\w+)\s+(\w+);", FT_LIBRARY_REC.group(1), re.M)[:4] == [
    ("FT_Memory", "memory"), ("FT_Int", "version_major"), ("FT_Int", "version_minor"),
    ("FT_Int", "version_patch")]
# Every library that a check below names. An APK must hold each of them for each of its ABIs.
CHECKED_BY_NAME = sorted({"libae-bridge.so", *PNG_LIBS, *NO_PNG_LIBS, FREETYPE, GLIDEN64,
                          "libminiupnp-bridge.so", *BRIDGE_IMPORTS, "libSDL2_net.so",
                          *SOUNDTOUCH_LIBS})
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


def strip_comments(text, in_comment):
    """A source line without its comments, and whether a /* comment is still open after it."""
    out = ""
    while text:
        if in_comment:
            end = text.find("*/")
            if end < 0:
                return out, True
            text, in_comment = text[end + 2:], False
            continue
        starts = [i for i in (text.find("//"), text.find("/*")) if i >= 0]
        if not starts:
            return out + text, False
        out += text[:min(starts)]
        if text.startswith("//", min(starts)):
            return out, False
        text, in_comment = text[min(starts) + 2:], True
    return out, in_comment


def soundtouch_simd_lines(source):
    """Per file name, the code lines of the SoundTouch sources that only ST_SIMD_AVOID_UNALIGNED
    builds ("on") and those it leaves out ("off"), and the conditionals not understood here."""
    on, off, unknown = collections.defaultdict(set), collections.defaultdict(set), []
    term = r"defined\s*\(?\s*\w+\s*\)?"
    any_of = re.compile(rf"{term}(\s*\|\|\s*{term})*")
    for path in sorted(Path(source).glob("*.cpp")):
        # One entry per open conditional: None, or [first branch needs the macro, this one does].
        stack, in_comment = [], False
        for number, raw in enumerate(path.read_text(errors="replace").splitlines(), 1):
            text, in_comment = strip_comments(raw, in_comment)
            text = text.strip()
            directive = re.match(r"#\s*(\w+)\s*(.*)", text)
            if not directive:
                states = {frame[1] for frame in stack if frame}
                if text and states == {True}:
                    on[path.name].add(number)
                elif text and states == {False}:
                    off[path.name].add(number)
                continue
            word, condition = directive.groups()
            if word in ("if", "ifdef", "ifndef", "elif") and ST_SIMD in condition:
                if word == "ifdef" or (word == "if" and any_of.fullmatch(condition)):
                    stack.append([True, True])
                    continue
                if word == "ifndef":
                    stack.append([False, False])
                    continue
                unknown.append(f"{path.name}:{number}")
            if word in ("if", "ifdef", "ifndef"):
                stack.append(None)
            elif word in ("elif", "else") and stack and stack[-1]:
                stack[-1][1] = not stack[-1][0]
            elif word == "endif" and stack:
                stack.pop()
    return on, off, unknown


def line_table(path):
    """The (file name, line) of every row of the DWARF line tables, or None without any."""
    rows, names, current = set(), {}, None
    for line in run(BIN / "llvm-dwarfdump", "--debug-line", path).splitlines():
        file_entry = re.match(r"file_names\[\s*(\d+)\]", line)
        name = re.match(r'\s+name:\s.*"([^"]*)"', line)
        row = re.match(r"0x[0-9a-f]+\s+(\d+)\s+\d+\s+(\d+)\s", line)
        if line.startswith("debug_line["):
            names, current = {}, None
        elif file_entry:
            current = int(file_entry.group(1))
        elif name and current is not None:
            names[current] = re.split(r"[\\/]", name.group(1))[-1]
        elif row:
            rows.add((names.get(int(row.group(2))), int(row.group(1))))
    return rows or None


def version_id_constants(path):
    """The immediate values in the code of soundtouch::SoundTouch::getVersionId()."""
    out = run(BIN / "llvm-objdump", "-d", "--no-show-raw-insn", "--print-imm-hex",
              f"--disassemble-symbols={ST_GET_VERSION_ID}", path)
    return sorted({int(value, 16) for line in out.splitlines() if re.match(r"\s+[0-9a-f]+:", line)
                   for value in re.findall(r"[#$](0x[0-9a-f]+)\b", line)})


def readonly_reader(path):
    """A function that reads a little-endian integer at an address of the library's read-only
    LOAD segments, or returns None."""
    data = path.read_bytes()
    wide = data[4] == 2
    phoff = struct.unpack_from("<Q" if wide else "<I", data, 0x20 if wide else 0x1C)[0]
    entsize, count = struct.unpack_from("<HH", data, 0x36 if wide else 0x2A)
    segments = []
    for i in range(count):
        at = phoff + i * entsize
        if wide:
            kind, flags, offset, vaddr, _, filesz = struct.unpack_from("<IIQQQQ", data, at)
        else:
            kind, offset, vaddr, _, filesz, _, flags = struct.unpack_from("<IIIIIII", data, at)
        if kind == 1 and not flags & 2:  # PT_LOAD without PF_W
            segments.append((vaddr, offset, filesz))

    def read(address, size):
        for vaddr, offset, filesz in segments:
            if address is not None and vaddr <= address and address + size <= vaddr + filesz:
                start = offset + address - vaddr
                return int.from_bytes(data[start:start + size], "little")
        return None
    return read


def object_stores(lib, abi, symbol):
    """The stores in all of the machine code of the exported function `symbol`: for each object
    that a register points to (followed through copies and added offsets), and for "abs"
    (addresses that the code computes), a map from offset to the stored byte, or to None where
    the value is not a constant that this reads; and the objects that an instruction not read
    here may store to. Constants come from immediates, the zero registers and loads from the
    library's read-only data. At branch targets and calls, what registers hold is forgotten.
    None if the library does not export `symbol`."""
    found = re.search(r"^\s*\d+:\s+([0-9a-f]+)\s+(\d+)\s+FUNC\s.*\s" + re.escape(symbol)
                      + r"(?:@\S*)?$", run(BIN / "llvm-readelf", "--dyn-syms", "--wide",
                                           lib.path), re.M)
    if not found:
        return None
    value, size = int(found.group(1), 16), int(found.group(2))
    start = value & ~1
    thumb = abi == "armeabi-v7a" and value & 1
    args = [BIN / "llvm-objdump", "-d", "--no-show-raw-insn", "--print-imm-hex",
            f"--start-address={start:#x}", f"--stop-address={start + size:#x}"]
    if thumb:
        args.append("--triple=thumbv7-none-linux-androideabi")
    lines = []
    for line in run(*args, lib.path).splitlines():
        insn = re.match(r"^\s*([0-9a-f]+):\s+(\S+)\s*(.*)$", line)
        if insn:
            text = insn.group(3).partition({"arm64-v8a": "//", "armeabi-v7a": "@"}
                                           .get(abi, "#"))[0].strip()
            lines.append((int(insn.group(1), 16), insn.group(2).split(".")[0], insn.group(2),
                          text))
    targets = {int(t, 16) for _, op, _, text in lines if op.startswith(("b", "cb", "tb", "j"))
               for t in re.findall(r"0x([0-9a-f]+) <", text)}
    read = readonly_reader(lib.path)
    arm64, arm32 = abi == "arm64-v8a", abi == "armeabi-v7a"
    keep = {"arm64-v8a": {f"r{n}" for n in range(19, 31)} | {"sp"},
            "armeabi-v7a": {f"r{n}" for n in range(4, 12)} | {"sp"},
            "x86": {"bx", "si", "di", "bp", "sp"},
            "x86_64": {"bx", "bp", "r12", "r13", "r14", "r15", "sp"}}[abi]
    vals, ptrs = {}, {}  # register: constant value; register: (object, offset)
    stores = collections.defaultdict(dict)
    unread = set()
    counter = iter(range(1, 1 << 30))
    pic = None  # x86: the register that holds the address of the GOT, and that address

    def reg(name):
        name = name.strip().lstrip("%").lower()
        if arm64:
            if name in ("wzr", "xzr"):
                return "zero"
            if re.fullmatch(r"[wx]\d+", name):
                return "r" + name[1:]
            vector = re.fullmatch(r"[bhsdqv](\d+)(?:\.\w+)?", name)
            return "v" + vector.group(1) if vector else name
        if arm32:
            return {"fp": "r11", "ip": "r12", "sb": "r9", "sl": "r10"}.get(name, name)
        return X86_REGS.get(name, "sp" if name in ("esp", "rsp", "spl") else name)

    def width_of(name):
        name = name.strip().lstrip("%").lower()
        if arm64:
            return {"w": 4, "x": 8, "b": 1, "h": 2, "s": 4, "d": 8, "q": 16}.get(name[0], 8)
        if arm32:
            return {"r": 4, "s": 4, "d": 8, "q": 16}.get(name[0], 4)
        if name.startswith("xmm"):
            return 16
        numbered = re.fullmatch(r"r\d+([dwb]?)", name)
        if numbered:
            return {"": 8, "d": 4, "w": 2, "b": 1}[numbered.group(1)]
        if name.startswith("r"):
            return 8
        if name.startswith("e"):
            return 4
        return 1 if name.endswith(("l", "h")) else 2

    def get(name):
        r = reg(name)
        return 0 if r == "zero" else vals.get(r)

    def forget(name):
        r = reg(name)
        vals.pop(r, None)
        ptrs[r] = (next(counter), 0)

    def pointer(r):
        if r not in ptrs:
            ptrs[r] = (next(counter), 0)
        return ptrs[r]

    def store(base, offset, width, data):
        """A store at base + offset; base is a register name or an address."""
        if isinstance(base, int):
            key, at = "abs", base + offset
        elif reg(base) in vals:
            key, at = "abs", vals[reg(base)] + offset
        else:
            obj, delta = pointer(reg(base))
            key, at = obj, delta + offset
        for i in range(width):
            stores[key][at + i] = None if data is None else (data >> (8 * i)) & 0xFF

    def mark_unread(base):
        if base is not None and reg(base) != "sp":
            unread.add(pointer(reg(base))[0])

    def call():
        for r in list(vals):
            if r not in keep:
                vals.pop(r)
        for r in list(ptrs):
            if r not in keep:
                ptrs[r] = (next(counter), 0)

    previous = None  # x86: the address that a call to the next instruction pushes
    pending = None  # x86_64: an access relative to the next instruction's address
    for index, (address, op, full, text) in enumerate(lines):
        if pending is not None:
            kind, details = pending
            pending = None
            if kind == "load":
                dest, disp, width = details
                vals.pop(dest, None)
                got = read(address + disp, width)
                if got is not None:
                    vals[dest] = got
            elif kind == "address":
                dest, disp = details
                vals[dest] = address + disp
            else:
                disp, width, data = details
                store(address + disp, 0, width, data)
        if address in targets:
            base = (pic[0], vals.get(pic[0])) if pic else None
            vals.clear()
            if base and base[1] is not None:
                vals[base[0]] = base[1]
        ops = [o.strip() for o in re.split(r",\s*(?![^\[\]{}()]*[\]})])", text)] if text else []
        if arm64:
            immediate = re.fullmatch(r"#" + IMM, ops[1]) if len(ops) >= 2 else None
            shift = re.fullmatch(r"lsl #(\d+)", ops[2]) if len(ops) == 3 else None
            if op in ("bl", "blr"):
                call()
            elif op in ("mov", "movz") and immediate and (len(ops) == 2 or shift):
                forget(ops[0])
                vals[reg(ops[0])] = int(immediate.group(1), 0) << int(shift.group(1) if shift
                                                                     else 0) \
                    & ((1 << 8 * width_of(ops[0])) - 1)
            elif op == "movk" and immediate and (len(ops) == 2 or shift):
                old = get(ops[0])
                at = int(shift.group(1)) if shift else 0
                forget(ops[0])
                if old is not None:
                    vals[reg(ops[0])] = old & ~(0xFFFF << at) | int(immediate.group(1), 0) << at
            elif op == "movi" and len(ops) == 2 and re.fullmatch(r"#(0x)?0+", ops[1]):
                forget(ops[0])
                vals[reg(ops[0])] = 0
            elif op in ("mov", "orr") and len(ops) in (2, 3) and reg(ops[1]) == "zero" \
                    and (len(ops) == 2 or re.fullmatch(r"#" + IMM, ops[2])):
                forget(ops[0])
                vals[reg(ops[0])] = int(ops[2][1:], 0) if len(ops) == 3 else 0
            elif op == "mov" and len(ops) == 2 and not ops[1].startswith("#"):
                source = reg(ops[1])
                if source in vals:
                    vals[reg(ops[0])] = vals[source]
                else:
                    vals.pop(reg(ops[0]), None)
                ptrs[reg(ops[0])] = pointer(source)
            elif op in ("adrp", "adr") and len(ops) == 2:
                absolute = re.match(r"0x([0-9a-f]+)", ops[1])
                forget(ops[0])
                if absolute:
                    vals[reg(ops[0])] = int(absolute.group(1), 16)
                elif immediate and op == "adr":
                    vals[reg(ops[0])] = address + int(immediate.group(1), 0)
            elif op == "add" and len(ops) == 3 and re.fullmatch(r"[wx]\d+", ops[2]):
                left, right = get(ops[1]), get(ops[2])
                forget(ops[0])
                if left is not None and right is not None:
                    vals[reg(ops[0])] = (left + right) & ((1 << 8 * width_of(ops[0])) - 1)
            elif op in ("add", "sub") and len(ops) == 3 and re.fullmatch(r"#" + IMM, ops[2]):
                imm = int(ops[2][1:], 0) * (1 if op == "add" else -1)
                source = reg(ops[1])
                if source in vals:
                    total = vals[source] + imm
                    forget(ops[0])
                    vals[reg(ops[0])] = total
                else:
                    obj, delta = pointer(source)
                    vals.pop(reg(ops[0]), None)
                    ptrs[reg(ops[0])] = (obj, delta + imm)
            elif op in ("ldr", "ldur") and len(ops) == 2:
                address_of = re.fullmatch(r"\[(\w+)(?:,\s*#" + IMM + r")?\]", ops[1])
                literal = re.match(r"0x([0-9a-f]+)", ops[1])
                got = None
                if address_of and reg(address_of.group(1)) in vals:
                    got = read(vals[reg(address_of.group(1))] + int(address_of.group(2) or "0", 0),
                               width_of(ops[0]))
                elif literal:
                    got = read(int(literal.group(1), 16), width_of(ops[0]))
                forget(ops[0])
                if got is not None:
                    vals[reg(ops[0])] = got
            elif op.startswith("st"):
                address_of = re.fullmatch(r"\[(\w+)(?:,\s*#" + IMM + r")?\](!?)", ops[-1]) \
                    if ops else None
                post = len(ops) >= 3 and re.fullmatch(r"#" + IMM, ops[-1]) \
                    and re.fullmatch(r"\[(\w+)\]", ops[-2])
                if post:
                    address_of, sources = re.fullmatch(r"\[(\w+)\]()()", ops[-2]), ops[:-2]
                else:
                    sources = ops[:-1]
                if op in ("str", "stur", "strb", "strh", "sturb", "sturh", "stp") \
                        and address_of and len(sources) == (2 if op == "stp" else 1):
                    width = {"b": 1, "h": 2}.get(op[-1]) or width_of(sources[0])
                    if reg(address_of.group(1)) != "sp":
                        for i, source in enumerate(sources):
                            store(address_of.group(1),
                                  int(address_of.group(2) or "0", 0) + i * width, width,
                                  get(source))
                    if address_of.group(3) or post:
                        forget(address_of.group(1))
                else:
                    base = re.search(r"\[(\w+)", text)
                    mark_unread(base.group(1) if base else None)
            elif ops and op not in ("b", "br", "cbz", "cbnz", "tbz", "tbnz", "ret", "cmp",
                                    "cmn", "tst", "ccmp", "ccmn", "fcmp", "fcmpe", "nop",
                                    "prfm", "dmb", "dsb", "isb", "hint"):
                pair = op in ("ldp", "ldnp", "ldpsw", "ldxp", "ldaxp")
                for dest in ops[:2 if pair else 1]:
                    forget(dest)
        elif arm32:
            pc = (address + 4) & ~3 if thumb else address + 8
            if op in ("bl", "blx"):
                call()
            elif op in ("mov", "movs", "movw", "mvn", "mvns") and len(ops) == 2 \
                    and re.fullmatch(r"#" + IMM, ops[1]):
                imm = int(ops[1][1:], 0)
                forget(ops[0])
                vals[reg(ops[0])] = (~imm if op.startswith("mvn") else imm) & 0xFFFFFFFF
            elif op == "movt" and len(ops) == 2 and re.fullmatch(r"#" + IMM, ops[1]):
                old = get(ops[0])
                forget(ops[0])
                if old is not None:
                    vals[reg(ops[0])] = old & 0xFFFF | int(ops[1][1:], 0) << 16
            elif op == "adr" and len(ops) == 2 and re.fullmatch(r"#" + IMM, ops[1]):
                forget(ops[0])
                vals[reg(ops[0])] = pc + int(ops[1][1:], 0)
            elif op == "vmov" and len(ops) == 2 and re.fullmatch(r"#(0x)?0+", ops[1]) \
                    and re.fullmatch(r"[dq]\d+", ops[0]):
                n = int(ops[0][1:])
                for d in ([2 * n, 2 * n + 1] if ops[0][0] == "q" else [n]):
                    vals[f"d{d}"] = 0
            elif op in ("mov", "movs") and len(ops) == 2 and re.fullmatch(r"\w+", ops[1]):
                source = reg(ops[1])
                if source in vals:
                    vals[reg(ops[0])] = vals[source]
                else:
                    vals.pop(reg(ops[0]), None)
                ptrs[reg(ops[0])] = pointer(source)
            elif op in ("add", "adds") and len(ops) in (2, 3) \
                    and all(re.fullmatch(r"\w+", o) for o in ops):
                left, right = [pc if reg(o) == "pc" else vals.get(reg(o)) for o in ops[-2:]]
                forget(ops[0])
                if left is not None and right is not None:
                    vals[reg(ops[0])] = (left + right) & 0xFFFFFFFF
            elif op in ("add", "adds", "sub", "subs") and len(ops) == 3 \
                    and re.fullmatch(r"#" + IMM, ops[2]):
                imm = int(ops[2][1:], 0) * (1 if op.startswith("add") else -1)
                source = reg(ops[1])
                if source == "pc" or source in vals:
                    total = ((pc if source == "pc" else vals[source]) + imm) & 0xFFFFFFFF
                    forget(ops[0])
                    vals[reg(ops[0])] = total
                else:
                    obj, delta = pointer(source)
                    vals.pop(reg(ops[0]), None)
                    ptrs[reg(ops[0])] = (obj, delta + imm)
            elif op in ("ldr", "vldr") and len(ops) == 2:
                address_of = re.fullmatch(r"\[(\w+)(?:,\s*#" + IMM + r")?\]", ops[1])
                got = None
                if address_of:
                    base = reg(address_of.group(1))
                    at = pc if base == "pc" else vals.get(base)
                    if at is not None:
                        got = read(at + int(address_of.group(2) or "0", 0), width_of(ops[0]))
                forget(ops[0])
                if got is not None:
                    vals[reg(ops[0])] = got
            elif op == "vld1" and len(ops) == 2:
                address_of = re.fullmatch(r"\[(\w+)(?::\d+)?\]", ops[1])
                at = vals.get(reg(address_of.group(1))) if address_of else None
                for i, d in enumerate(re.findall(r"d(\d+)", ops[0])):
                    forget(f"d{d}")
                    got = read(at + 8 * i, 8) if at is not None else None
                    if got is not None:
                        vals[f"d{d}"] = got
            elif op.startswith(("st", "vst")):
                address_of = re.fullmatch(r"\[(\w+)(?::\d+)?(?:,\s*#" + IMM + r")?\](!?)",
                                          ops[-1]) if ops else None
                if op in ("str", "strb", "strh", "strd", "vstr") and address_of:
                    width = 4 if op == "strd" else {"b": 1, "h": 2}.get(op[-1]) \
                        or width_of(ops[0])
                    if reg(address_of.group(1)) != "sp":
                        for i, source in enumerate(ops[:-1]):
                            store(address_of.group(1),
                                  int(address_of.group(2) or "0", 0) + i * width, width,
                                  get(source))
                    if address_of.group(3):
                        forget(address_of.group(1))
                elif op == "vst1" and address_of and not address_of.group(2) \
                        and not address_of.group(3):
                    if reg(address_of.group(1)) != "sp":
                        for i, d in enumerate(re.findall(r"d(\d+)", ops[0])):
                            store(address_of.group(1), 8 * i, 8, vals.get(f"d{d}"))
                elif op in ("stm", "stmia") and len(ops) == 2 and re.fullmatch(r"\w+", ops[0]):
                    if reg(ops[0]) != "sp":
                        for i, source in enumerate(re.findall(r"\w+", ops[1])):
                            store(ops[0], 4 * i, 4, get(source))
                else:
                    base = re.search(r"\[(\w+)", text)
                    mark_unread(base.group(1) if base else ops[0].rstrip("!") if ops else None)
            elif op in ("pop", "ldm", "ldmia", "vpop", "vldmia"):
                for r in re.findall(r"\b(r\d+|lr|pc|fp|ip|d\d+)\b", text):
                    forget(r)
            elif ops and not re.fullmatch(
                    r"b(l|lx|x)?(eq|ne|cs|hs|cc|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le|al)?|cbn?z"
                    r"|it[te]{0,3}|cmp|cmn|tst|teq|push|vpush|nop|dmb|dsb|isb|vcmp|vcmpe", op):
                pair = op in ("ldrd", "ldrexd", "umull", "smull", "umlal", "smlal") \
                    or (op == "vmov" and len(ops) == 3 and re.fullmatch(r"r\d+|lr|ip|fp", ops[1]))
                for dest in ops[:2 if pair else 1]:
                    forget(dest)
        else:
            if full.startswith("call"):
                target = re.match(r"0x([0-9a-f]+)(?: <([^>]*)>)?", text)
                after = lines[index + 1][0] if index + 1 < len(lines) else None
                if target and int(target.group(1), 16) == after:
                    previous = after
                    continue
                call()
                if target and target.group(2) and "get_pc_thunk." in target.group(2):
                    thunk = reg(target.group(2).rpartition(".")[2])
                    vals[thunk], pic = after, (thunk, after)
                previous = None
                continue
            if full.startswith("pop") and previous is not None and len(ops) == 1:
                vals[reg(ops[0])], pic = previous, (reg(ops[0]), previous)
                previous = None
                continue
            previous = None
            source = ops[0] if ops else ""
            dest = ops[-1] if ops else ""

            def memory(operand):
                """(displacement, base) for disp(%base), ("other", base) for other memory
                operands, None for registers and immediates."""
                simple = re.fullmatch(IMM + r"?\(%(\w+)\)", operand)
                if simple:
                    return int(simple.group(1) or "0", 0), simple.group(2)
                if "(" in operand:
                    base = re.search(r"\(%(\w+)", operand)
                    return "other", base.group(1) if base else None
                return None
            to_memory = memory(dest) if len(ops) == 2 else None
            from_memory = memory(source) if len(ops) == 2 else None
            move = re.fullmatch(r"mov(abs)?[bwlq]?|mov(s[sd]|aps|apd|ups|upd|dq[au]|d|lps|hps)",
                                full)
            width = {"movb": 1, "movw": 2, "movl": 4, "movq": 8, "movabsq": 8, "movss": 4,
                     "movd": 4, "movsd": 8, "movlps": 8, "movhps": 8}.get(full)
            if width is None and move and not re.fullmatch(r"mov(abs)?", full):
                width = 16
            skip = re.fullmatch(r"(cmp|test)[bwlq]?|push[lqw]?|j\w+|nop\w*|u?comis[sd]|bt[lqw]?",
                                full)
            if move and to_memory:
                if to_memory[0] == "other":
                    mark_unread(to_memory[1])
                    continue
                width = width or width_of(source)
                data = int(source[1:], 0) & ((1 << 8 * width) - 1) if source.startswith("$") \
                    else None if full == "movhps" else get(source)
                disp, base = to_memory
                if base == "rip":
                    pending = ("store", (disp, width, data))
                elif reg(base) != "sp":
                    store(base, disp, width, data)
            elif move and from_memory and dest.startswith("%"):
                width = width or width_of(dest)
                forget(dest)
                if from_memory[0] != "other":
                    disp, base = from_memory
                    if base == "rip":
                        pending = ("load", (reg(dest), disp, width))
                    elif reg(base) in vals:
                        got = read(vals[reg(base)] + disp, width)
                        if got is not None:
                            vals[reg(dest)] = got
            elif move and source.startswith("$") and dest.startswith("%"):
                forget(dest)
                if width_of(dest) >= 4:
                    vals[reg(dest)] = int(source[1:], 0) & ((1 << 8 * width_of(dest)) - 1)
            elif move and source.startswith("%") and dest.startswith("%"):
                origin = reg(source)
                if origin in vals and width_of(source) >= 4:
                    vals[reg(dest)] = vals[origin] & ((1 << 8 * width_of(dest)) - 1)
                else:
                    vals.pop(reg(dest), None)
                ptrs[reg(dest)] = pointer(origin)
            elif re.fullmatch(r"(add|sub)[lq]?", full) and len(ops) == 2 \
                    and source.startswith("$") and dest.startswith("%"):
                imm = int(source[1:], 0) * (1 if full.startswith("add") else -1)
                target = reg(dest)
                if target in vals:
                    vals[target] += imm
                    if pic and pic[0] == target:
                        pic = (target, vals[target])
                elif target != "sp":
                    obj, delta = pointer(target)
                    ptrs[target] = (obj, delta + imm)
            elif re.fullmatch(r"lea[lq]?", full) and len(ops) == 2 and from_memory \
                    and from_memory[0] != "other":
                disp, base = from_memory
                vals.pop(reg(dest), None)
                if base == "rip":
                    ptrs[reg(dest)] = (next(counter), 0)
                    pending = ("address", (reg(dest), disp))
                elif reg(base) in vals:
                    vals[reg(dest)] = vals[reg(base)] + disp
                    ptrs[reg(dest)] = (next(counter), 0)
                else:
                    obj, delta = pointer(reg(base))
                    ptrs[reg(dest)] = (obj, delta + disp)
            elif re.fullmatch(r"xor[lq]?|xorps|xorpd|pxor", full) and len(ops) == 2 \
                    and source.startswith("%") and reg(source) == reg(dest):
                forget(dest)
                vals[reg(dest)] = 0
            elif full.startswith(("rep", "stos")):
                mark_unread("di")
            elif to_memory and not skip:
                mark_unread(to_memory[1])
            elif ops and dest.startswith("%") and not skip:
                forget(dest)
                if full.startswith("xchg") and source.startswith("%"):
                    forget(source)
            if re.match(r"cltd|cqto|cltq|cwtl|mul|div|imul", full) and len(ops) <= 1:
                forget("dx")
                forget("ax")
    return {key: dict(sorted(data.items())) for key, data in stores.items()}, unread


def freetype_version(lib, abi):
    """The (major, minor, patch) that FT_New_Library() stores in the library object, in the three
    FT_Int fields after the FT_Memory pointer that FT_LibraryRec_ starts with. A field that it
    stores nothing in keeps the zero that FT_NEW fills the object with. None if no object has
    the major and minor fields stored as constants, or if the objects that do disagree."""
    read = object_stores(lib, abi, "FT_New_Library")
    if read is None:
        return None
    stores, unread = read
    first = 8 if abi in ("arm64-v8a", "x86_64") else 4
    candidates = set()
    for key, data in stores.items():
        if key == "abs" or key in unread:
            continue
        fields = []
        for n in range(3):
            raw = [data.get(first + 4 * n + i, "absent") for i in range(4)]
            if (n < 2 and "absent" in raw) or None in raw \
                    or ("absent" in raw and set(raw) != {"absent"}):
                break
            fields.append(0 if raw[0] == "absent" else int.from_bytes(bytes(raw), "little"))
        else:
            candidates.add(tuple(fields))
    return candidates.pop() if len(candidates) == 1 else None


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
SIMD_ON, SIMD_OFF, simd_unknown = soundtouch_simd_lines(os.environ["ST_SOURCE"])
if simd_unknown and any(name in libs[abi] for abi in libs for name in SOUNDTOUCH_LIBS):
    fails.append(f"FAIL ndkLibs/soundtouch: {ST_SIMD} conditionals not understood here: "
                 + " ".join(simd_unknown))

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
            rel, bld = NDK_NOTE
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
    for name, sample in SOUNDTOUCH_LIBS.items():
        lib = libs[abi].get(name)
        if not lib:
            continue
        other = "float" if sample == "short" else "short"
        put, wrong = lib.defined.get(ST_PUT_SAMPLES[sample]), lib.defined.get(ST_PUT_SAMPLES[other])
        if put == "T" and not wrong:
            passes.append(f"{abi}/{name}: putSamples takes {sample} samples")
        else:
            fails.append(f"FAIL {abi}/{name}: putSamples({sample}) {put or 'not exported'}, "
                         f"putSamples({other}) {wrong or 'not exported'} (expected T and not "
                         "exported)")
        constants = version_id_constants(lib.path)
        string_found = bool(ST_VERSION_RE.search(lib.path.read_bytes()))
        if ST_VERSION_ID in constants and string_found:
            passes.append(f"{abi}/{name}: SoundTouch {ST_VERSION}, getVersionId() returns "
                          f"{ST_VERSION_ID}")
        else:
            fails.append(f"FAIL {abi}/{name}: getVersionId() constants {constants or 'none'}, "
                         f"version string {ST_VERSION} {'found' if string_found else 'not found'} "
                         f"(expected {ST_VERSION_ID} and found)")
            notes.append(f"NOTE {abi}/{name}: not SoundTouch {ST_VERSION}, so its line table is "
                         "not compared with ndkLibs/soundtouch")
            continue
        rows = line_table(lib.path)
        if rows is None:
            notes.append(f"NOTE {abi}/{name}: stripped, {ST_SIMD} code not checked")
            continue
        built = sorted(f"{f}:{n}" for f, n in rows if n in SIMD_ON.get(f, ()))
        control = sorted(f"{f}:{n}" for f, n in rows if n in SIMD_OFF.get(f, ()))
        if built:
            fails.append(f"FAIL {abi}/{name}: code built with {ST_SIMD}: " + " ".join(built))
        elif not control:
            fails.append(f"FAIL {abi}/{name}: its line table shows neither the code built with "
                         f"{ST_SIMD} nor the code built without it, so it cannot be checked")
        else:
            passes.append(f"{abi}/{name}: no {ST_SIMD} code (its line table shows "
                          f"{' '.join(control)}, built without it)")
    gliden64, freetype = libs[abi].get(GLIDEN64), libs[abi].get(FREETYPE)
    if gliden64:
        own_png = sorted(n for n in set(gliden64.defined) | (gliden64.symtab() or set())
                         if n.startswith("png_"))
        if own_png:
            fails.append(f"FAIL {abi}/{GLIDEN64}: defines {len(own_png)} png_* symbols: "
                         + " ".join(own_png[:10]) + (" ..." if len(own_png) > 10 else ""))
        else:
            passes.append(f"{abi}/{GLIDEN64}: defines no png_* function"
                          + ("" if gliden64.symtab() else " in its dynamic symbols (stripped)"))
    if gliden64 and freetype:
        ft_imports = sorted(n for n in gliden64.undefined if n.startswith(("FT_", "png_")))
        unexpected = sorted(set(ft_imports) - set(GLIDEN64_IMPORTS))
        not_imported = sorted(set(GLIDEN64_IMPORTS) - set(ft_imports))
        not_exported = sorted(n for n in ft_imports if freetype.defined.get(n) != "T")
        if FREETYPE in gliden64.needed and not (unexpected or not_imported or not_exported):
            passes.append(f"{abi}/{FREETYPE}: exports the "
                          f"{sum(n.startswith('FT_') for n in ft_imports)} FT_* and "
                          f"{sum(n.startswith('png_') for n in ft_imports)} png_* functions that "
                          f"{GLIDEN64} imports")
        else:
            fails.append(f"FAIL {abi}/{GLIDEN64}: FT_* and png_* imports not expected: "
                         f"{unexpected or 'none'}; expected but not imported: "
                         f"{not_imported or 'none'}; not functions of {FREETYPE}: "
                         f"{not_exported or 'none'}; NEEDED {FREETYPE}: "
                         f"{'yes' if FREETYPE in gliden64.needed else 'no'}")
    if freetype:
        expected = ".".join(map(str, FT_VERSION))
        if not FT_VERSION_READABLE:
            fails.append(f"FAIL {abi}/{FREETYPE}: FT_LibraryRec_ in ndkLibs/freetype no longer "
                         "starts with memory and the three version fields, which this check reads")
        else:
            stored = freetype_version(freetype, abi)
            shown = ".".join(map(str, stored)) if stored else "nothing this script can read"
            if stored == FT_VERSION:
                passes.append(f"{abi}/{FREETYPE}: FreeType {expected}, FT_New_Library() stores "
                              f"{shown}")
            else:
                fails.append(f"FAIL {abi}/{FREETYPE}: FT_New_Library() stores {shown} "
                             f"(expected {expected})")
        ft_png = sorted({v.decode() for v in PNG_VERSION_RE.findall(freetype.path.read_bytes())})
        if ft_png == [PNG_VERSION]:
            passes.append(f"{abi}/{FREETYPE}: libpng {PNG_VERSION}")
        else:
            fails.append(f"FAIL {abi}/{FREETYPE}: libpng versions {ft_png or 'none'} (expected "
                         f"{PNG_VERSION})")
        classes = set(freetype.defined) | (freetype.symtab() or set())
        absent = [c for c in FT_MODULES if c not in classes]
        present = [c for c in FT_OTHER_MODULES if c in classes]
        if not absent and not present:
            passes.append(f"{abi}/{FREETYPE}: has the {len(FT_MODULES)} module classes of "
                          "ndkLibs/config/freetype-android/ftmodule.h and none of FreeType's "
                          "others")
        elif freetype.symtab() is None and not present:
            notes.append(f"NOTE {abi}/{FREETYPE}: stripped, and does not export "
                         f"{' '.join(absent)}: module classes not checked")
        else:
            fails.append(f"FAIL {abi}/{FREETYPE}: module classes missing: {absent or 'none'}; "
                         f"not in ftmodule.h but present: {present or 'none'}")

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
