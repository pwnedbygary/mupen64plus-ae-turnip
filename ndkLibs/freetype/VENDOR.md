# Vendored FreeType

Upstream: https://gitlab.freedesktop.org/freetype/freetype (home page https://freetype.org)
Release: 2.15.0, October 12, 2026 (`docs/CHANGES`)
Tag: `VER-2-15-0`, tag object `631ee12ce7693c9ba9ea33f0235da99218045420`, commit
`37515cf56ba3349656049edbed89ffc6f6466f2d` (freedesktop.org's GitLab, read on October 28, 2026)
Archive: https://download.savannah.gnu.org/releases/freetype/freetype-2.15.0.tar.xz, 2,716,928
bytes, SHA-256 `93cd47eb719c325107f2d3cf087d06299e944c3b48a40b55537302304dea1dd5`. Savannah
publishes a signature (`.sig`) and no hash; the signature was not checked. Downloads on October
27 and 28, 2026 gave this hash.
License: `FTL OR GPL-2.0-or-later`. `LICENSE.TXT` offers the FreeType License (`docs/FTL.TXT`)
or the GNU GPL version 2 (`docs/GPLv2.TXT`), "any later version can be used also". Two parts
that the build compiles have notices of their own: `src/base/fthash.c` (a license like the X
Window System's), which `src/base/ftbase.c` includes, and FreeType's copy of zlib 1.3.1 in
`src/gzip` (the zlib license, in `src/gzip/zlib.h`). `LICENSES.md` reproduces the FTL and these
two notices.

Vendored contents: the archive without its top folder `freetype-2.15.0/` and without two of its
folders, which the build does not use (Gary's decision of October 27, 2026): `docs/reference/`
(111 files, the HTML API reference that FreeType generates from its headers) and `tests/` (19
files, unit and regression tests that only FreeType's Meson build builds, with their fonts and
scripts). That leaves 756 of the archive's 886 files. No file is changed. This file is the only
addition.

Git and this folder (paths relative to it):
- Ignored files: none. The archive has no `.gitignore`, and no file matches the repository's.
- Line endings: 17 text files have CRLF line endings in the archive (`ChangeLog` in 157 of its
  lines, the others in all of them), and the repository's `.gitattributes` stores them with LF.
  None is built. Ten get CRLF again when checked out (`eol=crlf` for `*.sln`, `*.vcproj`,
  `*.vcxproj` and `*.vcxproj.filters`), so they come out as in the archive: `MSBuild.sln`,
  `builds/wince/vc2005-ce/freetype.sln` and `freetype.vcproj`,
  `builds/wince/vc2008-ce/freetype.sln` and `freetype.vcproj`,
  `builds/windows/vc2010/freetype.vcxproj` and `freetype.vcxproj.filters`,
  `builds/windows/visualc/freetype.sln` and `freetype.vcproj`, and
  `builds/windows/visualce/freetype.vcproj`. The other seven come out with LF: `ChangeLog`,
  `MSBuild.rsp`, `builds/windows/vc2010/freetype.user.props`, and `freetype.dsp` and
  `freetype.dsw` in both `builds/windows/visualc/` and `builds/windows/visualce/`.

Configuration: the build replaces `include/freetype/config/ftoption.h` and `ftmodule.h` with
the two files in `ndkLibs/config/freetype-android/`, outside this folder, through
`-DFT_CONFIG_OPTIONS_H=<freetype-android/ftoption.h>` and
`-DFT_CONFIG_MODULES_H=<freetype-android/ftmodule.h>`, the mechanism that Android's own FreeType
build uses. They are copies of this release's two files with these differences, and no others:
- `ftoption.h`: `FT_CONFIG_OPTION_USE_PNG` is defined (line 276; upstream comments it out), so
  FreeType can load color bitmap glyphs stored as PNG, with the libpng linked into the library;
  `FT_CONFIG_OPTION_INCREMENTAL` is commented out (line 437; upstream defines it); and so is
  `FT_CONFIG_OPTION_SVG` (line 584; upstream defines it). The first two are the changes that
  Android's FreeType (https://android.googlesource.com/platform/external/freetype, commit
  `9c745321`, the source of the prebuilt `libfreetype.so` that this build replaces) made to its
  `ftoption.h`. The third goes with leaving out the SVG module (see `ftmodule.h` below): with the
  option on and the module left out, FreeType can load a glyph of a font that has an SVG table
  as an SVG document that nothing can render, and `ft_glyphslot_preset_bitmap()`
  (`src/base/ftobjs.c`) uses what `FT_Get_Module()` returns for the missing module without
  checking it for null. Android's FreeType predates OpenType SVG support, which came with
  FreeType 2.12.0, so the prebuilt library had none either. Everything else is upstream's
  default, among it zlib support with FreeType's own copy of zlib (`FT_CONFIG_OPTION_USE_ZLIB`,
  without `FT_CONFIG_OPTION_SYSTEM_ZLIB`), no bzip2, Brotli or HarfBuzz, and
  `TT_CONFIG_OPTION_SUBPIXEL_HINTING` defined, so that the TrueType bytecode interpreter is
  version 40 (Gary's choice of October 27, 2026). The prebuilt library had only version 35, so
  GLideN64's on-screen text is hinted differently.
- `ftmodule.h`: upstream's list without `t1_driver_class`, `t1cid_driver_class`,
  `hvf_driver_class`, `pfr_driver_class`, `t42_driver_class`, `winfnt_driver_class`,
  `pcf_driver_class`, `bdf_driver_class`, `ft_sdf_renderer_class`,
  `ft_bitmap_sdf_renderer_class` and `ft_svg_renderer_class`. That leaves nine, in upstream's
  order: `autofit_module_class`, `tt_driver_class`, `cff_driver_class`, `psaux_module_class`,
  `psnames_module_class`, `pshinter_module_class`, `sfnt_module_class`,
  `ft_smooth_renderer_class` and `ft_raster1_renderer_class`. These are the modules of the
  prebuilt library, plus psaux, whose interface the CFF driver now gets
  (`src/cff/cffobjs.c`), and without its two LCD renderers, which no longer exist (the smooth
  renderer does their work).

Build: `mupen64plus-video-gliden64/freetype-png.cmake`, which GLideN64's
`upstream/src/CMakeLists.txt` includes, builds `libfreetype.so` (target `freetype`) for
GLideN64's on-screen text.
- Sources: `src/base/ftsystem.c ftinit.c ftdebug.c ftbase.c ftbbox.c ftbitmap.c ftfstype.c
  ftgasp.c ftglyph.c ftmm.c ftstroke.c fttype1.c`, `src/autofit/autofit.c`,
  `src/truetype/truetype.c`, `src/cff/cff.c`, `src/psaux/psaux.c`, `src/psnames/psnames.c`,
  `src/pshinter/pshinter.c`, `src/sfnt/sfnt.c`, `src/smooth/smooth.c`, `src/raster/raster.c`,
  `src/gzip/ftgzip.c`. These are the 21 files of the prebuilt library with four changes:
  `src/base/ftlcdfil.c` and `ftxf86.c` (now `ftfntfmt.c`) are left out, because
  `src/base/ftbase.c` includes them now; `src/base/ftdebug.c` is added, as upstream's
  `CMakeLists.txt` builds it on systems other than Windows (without FreeType's debug options it
  defines the four functions of `include/freetype/ftlogging.h` as stubs); `src/psaux/psaux.c` is
  added for the CFF driver; and `src/base/ftmm.c` is added, because this release's TrueType and
  CFF drivers call functions that it defines: `FT_Set_Named_Instance`, and, in the TrueType
  driver's code for variable composite glyphs, `FT_Get_MM_Var`, `FT_Done_MM_Var`,
  `FT_Get_Var_Blend_Coordinates` and `FT_Set_Var_Blend_Coordinates`.
- Include folders, ahead of GLideN64's own, which the target also gets: `ndkLibs/config` (for
  the two configuration files), `include/`, and libpng's `ndkLibs/png` and
  `ndkLibs/config/png`, for FreeType's PNG code.
- Definitions: `PIC DARWIN_NO_CARBON FT2_BUILD_LIBRARY` and the two configuration files above.
  Flags: `-W -Wall -O2 -fno-fast-math`, and ARM mode (`-marm`) on `armeabi-v7a`. These are the
  definitions and flags of the prebuilt library (the recipe that built it in 2021, in
  https://github.com/mupen64plus-ae/mupen64plus-ae-deps, commit `1f92dbd6`, differs from this
  repository's former `app/src/main/jni/freetype.mk` only in how it finds libpng's headers and
  links libpng and zlib); `PIC` and `DARWIN_NO_CARBON` change
  nothing in this release. The target options come after GLideN64's `-O3 -ffast-math` and
  override them; GLideN64's other flags and the NDK's CMake toolchain defaults apply too, and the
  Release build type adds `-DNDEBUG`. GLideN64's directory definitions (`MUPENPLUSAPI`,
  `OS_ANDROID`, `EGL` and others) reach these targets too; no file of FreeType or libpng tests
  them.
- Links: `png_static`, libpng from `ndkLibs/png`, built in the same file with the files and
  flags of the `png` module of `build_common/native_common.mk` (see `ndkLibs/png/VENDOR.md`),
  with its include folders private, so that GLideN64's `GLideNHQ` keeps compiling against its
  own `inc/png.h`; and the system zlib (`libz.so`), which libpng uses. FreeType's gzip code
  uses its own copy of zlib instead, compiled into `src/gzip/ftgzip.c` with internal functions.
- Exports: FreeType's public functions (`FT_EXPORT` gives them default visibility) and the libpng
  functions of the libpng objects that FreeType's PNG code pulls in. These include every libpng
  function that GLideN64 calls, its write functions too, because libpng's `png.c` refers to
  `png_destroy_write_struct()` (in `png_image_free_function()`, which `png_image_free()`
  calls). GLideN64 links `libfreetype.so` before
  `png_static`, so its
  `png_*` calls bind to `libfreetype.so`, as they did to the prebuilt library;
  `tools/check-native-libs.sh` checks this. Besides its `FT_*` and `png_*` functions, the
  prebuilt library exported 24 internal functions and 125 internal tables. This release hides
  the functions that it declares with `FT_LOCAL` or `FT_BASE` and the tables that it declares
  with `FT_LOCAL_ARRAY` (`include/freetype/internal/compiler-macros.h`), but not all of its
  internals. It still exports `TT_New_Context()` and `TT_RunIns()` (declared with `FT_EXPORT`),
  `ps_property_get()` and `ps_property_set()` (with `FT_BASE_CALLBACK`), `ps_hints_apply()` and
  the `af_shaper_*` and `ft_hash_*` functions (with no visibility macro), and tables such as the
  module classes (declared with `FT_CALLBACK_TABLE`) and the autofitter's `af_*_uniranges`
  arrays (declared with no visibility macro). No library imports any of them.
- Not built: the rest of this folder, among it `builds/`, the other modules in `src/`,
  `src/tools/`, `devel/`, and upstream's `CMakeLists.txt`, `meson.build` and `configure`.

The tree that this folder held before was Android's copy of FreeType (commit `9c745321`, which
is FreeType git `e1394d56`, between versions 2.5.3 and 2.5.4). Its files that this release does
not have went with it, among them Android's `Android.mk`, `CleanSpec.mk`,
`MODULE_LICENSE_BSD_LIKE` and `NOTICE` (its FTL text, now `docs/FTL.TXT`).

To update: download the new release's archive from Savannah, record its SHA-256, replace this
folder's content with the archive's without `docs/reference/` and `tests/` (keep this file),
copy the new `include/freetype/config/ftoption.h` and `ftmodule.h` to
`ndkLibs/config/freetype-android/` and make the changes listed above again, compare the source
list of upstream's `CMakeLists.txt` and the includes of `src/base/ftbase.c` with
`freetype-png.cmake`, and update this file and `LICENSES.md` (the FreeType row and its three
notices). `tools/check-native-libs.sh` reads the version from `include/freetype/freetype.h` and
the module lists from both `ftmodule.h` files. Add the new files by the archive's file list, not
with `git add -A`, and use `git add -f` for each one that `git check-ignore` reports.
