# Vendored libpng

Upstream: https://github.com/pnggroup/libpng (home page http://www.libpng.org/pub/png/libpng.html)
Release: 1.6.59, September 28, 2026 (`CHANGES`)
Tag: `v1.6.59`, tag object `a543b51f2de2904f3436830394b1013782f7f9e6` (GitHub, 27 October 2026)
Archive: https://download.sourceforge.net/libpng/libpng-1.6.59.tar.xz, 1,074,540 bytes,
SHA-256 `d80dd2a38a37f803cb9b6ac7b14bd6e74ddc3b654780a8380bdf93523fdb4389` (the value that
libpng.org publishes)
License: `libpng-2.0` (see `LICENSE`; `CMakeLists.txt` line 15 gives the SPDX identifier).
`LICENSE` also keeps the earlier license for libpng 0.5 through 1.6.35. `LICENSES/MIT.txt` and
`LICENSES/MIT-0.txt` cover only `ci/` and `.github/workflows/` (`REUSE.toml`), which the build
does not use.

Vendored contents: the whole archive, 614 files, without its top folder `libpng-1.6.59/`.
Nothing is left out, and no file is changed. This file is the only addition.

Line endings: the repository's `.gitattributes` changes the line endings of 21 Windows project
files, so they differ from the archive in their line endings only. None is built.
- Nine have CRLF line endings in the archive, and `.gitattributes` stores them with LF (by
  `* text=auto`, and by `*.rc text` for `VisualPng.rc`):
  `contrib/visupng/VisualPng.dsp`, `VisualPng.dsw`, `VisualPng.rc`;
  `projects/owatcom/libpng.tgt`, `libpng.wpj`, `pngconfig.mak`, `pngstest.tgt`, `pngtest.tgt`,
  `pngvalid.tgt`.
- Twelve have LF line endings in the archive and are stored so, but `eol=crlf` (for `*.sln`,
  `*.vcproj` and `*.vcxproj`) gives them CRLF when they are checked out:
  `projects/visualc71/libpng.sln`, `libpng.vcproj`, `pngtest.vcproj`, `zlib.vcproj`;
  `projects/vstudio/vstudio.sln`, and the `.vcxproj` file in each of its folders `libpng`,
  `pnglibconf`, `pngstest`, `pngtest`, `pngunknown`, `pngvalid` and `zlib`.

Configuration: `ndkLibs/config/png/pnglibconf.h`, outside this folder, is a copy of
`scripts/pnglibconf.h.prebuilt`, libpng's default configuration.

Build: the static module `png` in `build_common/native_common.mk`, which the ndk-build modules
that use libpng link (the core and Rice; Glide64mk2 links it too but calls none of it).
- Sources: the 15 library files that upstream's `CMakeLists.txt` lists (`png.c pngerror.c
  pngget.c pngmem.c pngpread.c pngread.c pngrio.c pngrtran.c pngrutil.c pngset.c pngtrans.c
  pngwio.c pngwrite.c pngwtran.c pngwutil.c`), plus, on `armeabi-v7a` and `arm64-v8a`, its ARM
  list: `arm/arm_init.c arm/filter_neon_intrinsics.c arm/palette_neon_intrinsics.c`.
- Not built: `pngtest.c` (a test program); `arm/filter_neon.S`, which assembles to nothing when
  the intrinsics are used; `intel/`, so SSE2 stays off on `x86` and `x86_64` (`pngpriv.h` turns
  it on only when `PNG_INTEL_SSE` is defined); the other processors' folders; `contrib/`.
- Flags: `-O3`; on `armeabi-v7a` also `-mfpu=neon -DPNG_ARM_NEON_OPT=2
  -DPNG_ARM_NEON_API_SUPPORTED`, in Thumb mode. These are the flags of the prebuilt `libpng.a`
  (libpng 1.6.34) that this build replaces. On `arm64-v8a`, `pngpriv.h` turns NEON on itself.
  On `armeabi-v7a`, `PNG_ARM_NEON_API_SUPPORTED` leaves the NEON row filters off unless a
  program turns them on with `png_set_option` (`arm/arm_init.c`), which neither the core nor
  Rice does, as with the prebuilt. No run-time option turns off the NEON palette code
  (`pngrtran.c`), which is built in on both ARM ABIs. The prebuilt's `x86` flag
  `-DPNG_INTEL_SSE_IMPLEMENTATION` is left out with the `intel/` files. In the prebuilt, that
  flag compiled libpng 1.6.34's SSE2 filters into the `libpng.a` for `x86`, but without
  `PNG_INTEL_SSE`, that version's `pngpriv.h` does not define `PNG_FILTER_OPTIMIZATIONS`, so
  nothing called them: the core and Rice used no SSE2 filters then, and use none now.

GLideN64 does not use this tree yet: it calls the libpng 1.6.34 inside the prebuilt
`ndkLibs/libs/<variant>/<abi>/libfreetype.so`, and compiles against its own
`mupen64plus-video-gliden64/upstream/src/GLideNHQ/inc/png.h` (1.6.32).

To update: check the new archive's SHA-256 against libpng.org, replace this folder's content
with the archive's (keep this file), copy `scripts/pnglibconf.h.prebuilt` to
`ndkLibs/config/png/pnglibconf.h`, compare the source lists in `CMakeLists.txt` with
`native_common.mk`, and update this file and `LICENSES.md`. The archive's own `.gitignore`
applies inside this folder: add the new files by the archive's file list, not with
`git add -A`, and check with `git check-ignore` that none of them is ignored.
