# Vendored SoundTouch

Upstream: https://codeberg.org/soundtouch/soundtouch (home page https://www.surina.net/soundtouch,
which says that official releases are tagged in that repository)
Release: 2.4.1, March 29, 2026 (the date of the tag's commit)
Tag: `2.4.1`, commit `0047e0b1ecfceb041348579119bf79b73a322a3a` (Codeberg, 27 October 2026; the
archive's pax header names the same commit)
Archive: https://codeberg.org/soundtouch/soundtouch/archive/2.4.1.tar.gz, 608,032 bytes,
SHA-256 `35d404e6e8c2ebd12fb4000da6fadd75c99e37eed2126a04721828c11c0377ec`. Codeberg makes this
archive from the tag when asked, so a later download may differ in its bytes; two downloads on
27 October 2026 gave this hash.
License: `LGPL-2.1-or-later`. `COPYING.TXT` is the GNU LGPL version 2.1, and the header of each
source file says "version 2.1 of the License, or (at your option) any later version"
(`readme.md` says "released under LGPL v2.1"). `source/csharp-example/NAudio-license.txt`
(Microsoft Public License) is the license of `NAudio.dll`, which this folder leaves out.

Vendored contents: the archive without its top folder `soundtouch/` and without three of its
entries, which nothing here builds or uses (Gary's decision of October 27, 2026): the Windows
binaries `source/csharp-example/NAudio.dll` (NAudio 1.8.0.0) and
`source/csharp-example/SoundTouch.dll` (upstream's Windows build of SoundTouch 2.3.2.0, by its
version resource, so not of this release), and the symbolic link
`source/SoundTouchDLL/LazarusTest/libSoundTouchDll.so`, which points to
`../.libs/libSoundTouchDll.so`, a file that only a build of upstream's DLL would make. That
leaves 122 of the archive's 124 files. No file is changed. This file is the only addition. The
C# example's project, `source/csharp-example/csharp-example.csproj`, names both DLLs, so that
example does not build from this folder; nothing here builds it. The build does not use the
Gradle wrapper `source/Android-lib/gradle/wrapper/gradle-wrapper.jar` either.

Git and this folder (paths relative to it):
- Ignored files: the archive's own `.gitignore` applies inside this folder, as does the
  repository's. Five files match their rules, so they must be added with `git add -f`:
  `bin/run_test` and `bin/run_test.cmd` (`bin`), `include/soundtouch_config.h` (`*config*`),
  and `source/Android-lib/.classpath` and `.project` (the repository's `.classpath` and
  `.project`). Six files already tracked match a rule too, which does not affect them:
  `config/am_include.mk`, `config/README.TXT`, `configure.ac`, `include/soundtouch_config.h.in`
  and `source/csharp-example/App.config` (`*config*`), and `source/Android-lib/build.gradle`
  (`build*`).
- Line endings: 33 text files have CRLF line endings in the archive, and the repository's
  `.gitattributes` stores them with LF. None is built. Eight get CRLF again when checked out
  (`eol=crlf` for `*.sln` and `*.vcxproj`), so they come out as in the archive:
  `source/SoundStretch/soundstretch.sln` and `soundstretch.vcxproj`,
  `source/SoundTouch/SoundTouch.sln` and `SoundTouch.vcxproj`,
  `source/SoundTouchDLL/SoundTouchDLL.sln`, `SoundTouchDLL.vcxproj` and
  `DllTest/DllTest.vcxproj`, and `source/csharp-example/csharp-example.sln`. The other 25 come
  out with LF: `.gitignore`, `bin/run_test.cmd`, `config/README.TXT`, `config/am_include.mk`,
  `source/Android-lib/README-SoundTouch-Android.html`, `source/Android-lib/gradlew.bat`,
  `source/Android-lib/src/net/surina/soundtouch/SoundTouch.java`,
  `source/SoundTouchDLL/SoundTouchDLL.rc`, `source/SoundTouchDLL/LazarusTest/SoundTouchDLL.pas`,
  and 16 files in `source/csharp-example/`: `App.config`, `App.xaml`, `App.xaml.cs`,
  `csharp-example.csproj`, `MainWindow.xaml`, `MainWindow.xaml.cs`, `NAudio-license.txt`,
  `NAudio-readme.txt`, `README.txt`, `SoundProcessor.cs`, `SoundTouch.cs`, and in `Properties/`
  `AssemblyInfo.cs`, `Resources.Designer.cs`, `Resources.resx`, `Settings.Designer.cs` and
  `Settings.settings`.

Configuration: none. `include/soundtouch_config.h` is upstream's empty stand-in for the file
that its autotools build writes, and `include/STTypes.h` does not include it when `ANDROID` is
defined, as it is here.

Build: `mupen64plus-audio-android/CMakeLists.txt` builds two shared libraries from this folder,
one for each of the audio plugin's two libraries:
- `libsoundtouch.so` (target `soundtouch`), linked by `libmupen64plus-audio-android.so`: 16-bit
  integer samples, from `-D__SOFTFP__ -DANDROID` (`include/STTypes.h` turns that pair into
  `SOUNDTOUCH_INTEGER_SAMPLES`).
- `libsoundtouch_fp.so` (target `soundtouch_fp`), linked by `libmupen64plus-audio-android-fp.so`:
  float samples, from `-DSOUNDTOUCH_FLOAT_SAMPLES -DANDROID`.
- Sources, for both: `source/SoundTouch/AAFilter.cpp FIFOSampleBuffer.cpp FIRFilter.cpp
  cpu_detect_x86.cpp sse_optimized.cpp RateTransposer.cpp SoundTouch.cpp InterpolateCubic.cpp
  InterpolateLinear.cpp InterpolateShannon.cpp TDStretch.cpp BPMDetect.cpp PeakFinder.cpp`, plus
  `mmx_optimized.cpp` on `x86` and `x86_64`. On ARM, `sse_optimized.cpp` compiles to nothing,
  and `cpu_detect_x86.cpp` only to `disableExtensions()` and to `detectCPUextensions()`, which
  returns 0 there. Include path: `include/`.
- Flags: `-O2 -fno-fast-math -fexceptions -fno-rtti`, and ARM mode (`-marm`) on `armeabi-v7a`;
  the Release build type adds `-DNDEBUG`, so asserts are off. The libraries use `c++_shared`,
  link `liblog`, and keep default symbol visibility. These are the files and flags of the
  prebuilt SoundTouch 2.2 libraries that this build replaces, which were built in 2021 in
  https://github.com/mupen64plus-ae/mupen64plus-ae-deps (commit `1f92dbd6`) with a copy of that
  release's `source/Android-lib/jni/Android.mk`, where `-fno-rtti` came from ndk-build's
  defaults for C++; CMake leaves RTTI on, so the flag is set here. The other default flags come
  from the NDK's CMake toolchain file instead of from ndk-build. The target options come after
  the module's `-Oz` and the CMake file's `-O3 -ffast-math`, and override them. Like the
  prebuilt libraries, these export no typeinfo for SoundTouch's own classes. They export
  typeinfo for the same `std` exception classes as the prebuilt libraries, and also for
  `std::bad_array_new_length` and its base `std::bad_alloc`, which NDK 26.1's libc++ allocator
  throws.
- `SOUNDTOUCH_USE_NEON` is not defined, as in the prebuilt libraries (their recipe set it, then
  reset the flags before the libraries). Since 2.3.0 it would change nothing: the
  `ST_SIMD_AVOID_UNALIGNED` code in `source/SoundTouch/TDStretch.cpp` and `sse_optimized.cpp`,
  which it used to turn on, now also needs `SOUNDTOUCH_ALLOW_NONEXACT_SIMD_OPTIMIZATION`, which
  `include/STTypes.h` leaves commented out. So no ABI builds that code: ARM keeps the prebuilt
  libraries' code path, and `x86` and `x86_64`, where `__SSE__` turned it on in 2.2, lose that
  inexact shortcut, as upstream intends. `tools/check-native-libs.sh` checks this in the
  libraries' line tables. The audio plugin's own libraries still define `SOUNDTOUCH_USE_NEON`,
  which has no effect in the headers they include.
- Not used: upstream's `CMakeLists.txt` (one library named `SoundTouch`, built with
  `-O3 -ffast-math` and, on ARM, `SOUNDTOUCH_USE_NEON`), `source/Android-lib/` (a JNI library
  built with `-fvisibility=hidden -ffast-math`), `source/SoundStretch/`,
  `source/SoundTouchDLL/`, `source/csharp-example/`, and the autotools and Visual Studio files.

To update: download the new tag's archive from Codeberg, record its SHA-256 and the commit in
its pax header (`python3 -c "import tarfile; print(tarfile.open('ARCHIVE').pax_headers)"`),
replace this folder's content with the archive's, without the two DLLs and the symbolic link
named above (keep this file), compare the source list in the archive's `CMakeLists.txt` and the
sample-type and SIMD settings in `include/STTypes.h` with the build above, and update this file
and `LICENSES.md`. Add the new files by the archive's file list, not with `git add -A`, and use
`git add -f` for each one that `git check-ignore` reports.
