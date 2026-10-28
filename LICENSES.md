# Licenses

**Mupen64Plus-AE Turnip** (shown on your device as "M64P Turnip")

Last updated: 28 October 2026

This page lists the third-party software, data and art found in the app, with their licenses
and where their texts are. The section [Full license notices](#full-license-notices) gives, in
full, every notice that these licenses ask to be kept or reproduced. The page describes the
current code, from which the next release and the test builds are made. The section
[Release V339](#release-v339) lists how V339 differs.

Paths are relative to the root of this repository. A path that starts with `assets/` is in
the app too, under `app/src/main/assets/` in the repository.

## The app's license

The app is licensed under the GNU General Public License, version 3 (`GPL-3.0`). Most of the
app's own Java files say "version 3 of the License, or (at your option) any later version"
(`GPL-3.0-or-later`); seven say version 2 or any later version (`GPL-2.0-or-later`). The
license text is in [`gpl-license`](gpl-license) and, in the app, in
`assets/mupen64plus_data/doc/doc/gpl-license-3`.

The app is a fork of [Mupen64Plus-AE](https://github.com/mupen64plus-ae/mupen64plus-ae), by
way of [Francisco Zurita's fork](https://github.com/fzurita/mupen64plus-ae).

### Source code of each release

Each release on the [releases page](https://github.com/pwnedbygary/mupen64plus-ae-turnip/releases)
has a tag with the same name in this repository, for example `V339` (commit `d9779061b`).
The source code of a release is this repository at that tag. Each release page also has
"Source code" downloads of the tag. The tag contains the source of every native library that
the build compiles, the copies of the upstream projects, and the build scripts. The build
compiles every native library in the app except `libc++_shared.so`, `liboboe.so` and
`libjnidispatch.so`, which come from the Android NDK and from the Oboe and JNA packages.
V339 also contains libraries that were built in 2021 outside this repository: see
[Release V339](#release-v339).

## Parts that are not open source

- **angrylion-rdp-plus**, one of the video plugins. Its license is the MAME license, which
  says: "Redistributions may not be sold, nor may they be used in a commercial product or
  activity." The app is free and has no paid features. The full license is in
  [Full license notices](#full-license-notices).
- **Google Play services** libraries, used to sign in for Google Drive backup. They are
  under the Android Software Development Kit License, Google's own terms.

The terms of some art and data are unknown: see [Data, art and fonts](#data-art-and-fonts).

## Native libraries

The app contains 31 native libraries for `arm64-v8a`, and 27 for each of `armeabi-v7a`,
`x86` and `x86_64`. The four `libadrenotools` hook libraries are for `arm64-v8a` only. This
table lists each component in them. The full text of each notice in the last column is in
[Full license notices](#full-license-notices).

| Component | In library | License (SPDX) | Upstream | License text in this repository |
|---|---|---|---|---|
| Mupen64Plus-AE input plugin | `libmupen64plus-input-android.so` | `GPL-3.0-or-later` | This repository | Header of `mupen64plus-input-android/src/plugin.cpp` |
| The app's own native files without a license header (ae-bridge, EGLLoader, the audio handler, the NAT-PMP port manager) | `libae-bridge.so`, `libmupen64plus-video-glide64mk2-egl.so`, `libmupen64plus-audio-android.so`, `libmupen64plus-audio-android-fp.so`, `libminiupnp-bridge.so` | `GPL-3.0` (the app's license) | This repository | [`gpl-license`](gpl-license) |
| Mupen64Plus core, from the mupen64plus-ae fork, with later upstream commits and this app's changes | `libmupen64plus-core.so` | `GPL-2.0-or-later` | <https://github.com/mupen64plus/mupen64plus-core>; fork <https://github.com/mupen64plus-ae/mupen64plus-core> at commit `3c4182f7`, plus commits from upstream that this repository took on 23 and 24 October 2026 | `mupen64plus-core/upstream/LICENSES`, `mupen64plus-core/upstream/doc/gpl-license` |
| rcheevos 12.5.0 | `libmupen64plus-core.so` | `MIT` | <https://github.com/RetroAchievements/rcheevos>, tag `v12.5.0` | `mupen64plus-core/rcheevos/LICENSE` |
| tiny-AES-c, inside rcheevos | `libmupen64plus-core.so` | `Unlicense` | <https://github.com/kokke/tiny-AES-c> | Header of `mupen64plus-core/rcheevos/src/rhash/aes.c`; the Unlicense text is not in this repository and is reproduced below from tiny-AES-c at commit `f06ac37f` |
| minizip, from zlib 1.3.1.2 (its file headers still say version 1.1) | `libmupen64plus-core.so` | `Zlib` | <http://www.winimage.com/zLibDll/minizip.html>; zlib <https://github.com/madler/zlib>, tag `v1.3.1.2` | Header of `mupen64plus-core/upstream/subprojects/minizip/unzip.h`; Mark Adler's notices in `skipset.h` and `ints.h` in the same folder |
| MD5 code by L. Peter Deutsch | `libmupen64plus-core.so` | `Zlib` | Not recorded | Header of `mupen64plus-core/upstream/subprojects/md5/md5.c` |
| xxHash (0.8.3 in the core, 0.8.0 in GLideN64 and Glide64mk2) | `libmupen64plus-core.so`, `libmupen64plus-video-GLideN64.so`, `libmupen64plus-video-glide64mk2*.so` (ARM only) | `BSD-2-Clause` | <https://github.com/Cyan4973/xxHash> | Headers of `mupen64plus-core/upstream/subprojects/xxhash/xxhash.h`, `mupen64plus-video-gliden64/upstream/src/xxHash/xxhash.h`, `mupen64plus-video-glide64mk2/upstream/src/Neon/xxhash.h` |
| CIC-NUS-6105 code by X-Scale | `libmupen64plus-core.so` | `BSD-2-Clause` | Not recorded | Header of `mupen64plus-core/upstream/src/device/pif/n64_cic_nus_6105.c` |
| libpng 1.6.59, built from source as a static library (GLideN64 uses the copy in `libfreetype.so`) | `libmupen64plus-core.so`, `libmupen64plus-video-rice.so`, `libfreetype.so` | `libpng-2.0` | <https://github.com/pnggroup/libpng>, tag `v1.6.59` | `ndkLibs/png/LICENSE` |
| SDL_net 2.4.0, built from source; a header of this app's build, `ndkLibs/config/SDL_net/SDL_net_libc.h`, maps six SDL functions to the C library | `libSDL2_net.so` | `Zlib` | <https://github.com/libsdl-org/SDL_net>, tag `release-2.4.0` | `ndkLibs/SDL_net/LICENSE.txt`; header of `ndkLibs/SDL_net/include/SDL_net.h` |
| libadrenotools | `libae-bridge.so`; hook libraries `libfile_redirect_hook.so`, `libgsl_alloc_hook.so`, `libhook_impl.so`, `libmain_hook.so` | `BSD-2-Clause` | <https://github.com/bylaws/libadrenotools> | `adrenotools/LICENSE` |
| liblinkernsbypass | `libae-bridge.so`, `libhook_impl.so` | `BSD-2-Clause` | <https://github.com/bylaws/liblinkernsbypass> | `adrenotools/lib/linkernsbypass/LICENSE` |
| Linux kernel header `kgsl.h` | `libhook_impl.so` | `GPL-2.0-only WITH Linux-syscall-note` | Not recorded | First line of `adrenotools/src/hook/kgsl.h` |
| Khronos OpenGL headers `glcorearb.h` and `glext.h`, used by EGLLoader, and GLideN64's own copies of both | `libae-bridge.so`, `libmupen64plus-video-glide64mk2-egl.so`, `libmupen64plus-video-GLideN64.so` | Khronos license in MIT style (no SPDX id) | <https://www.khronos.org/> | Headers of `ndkLibs/GL/GL/glcorearb.h`, `ndkLibs/GL/GL/glext.h`, `mupen64plus-video-gliden64/upstream/src/inc/GL/glcorearb.h` and `mupen64plus-video-gliden64/upstream/src/inc/GL/glext.h` |
| Mesa's `gl.h` header, used by EGLLoader | `libae-bridge.so`, `libmupen64plus-video-glide64mk2-egl.so` | `MIT` | Not recorded | Header of `ndkLibs/GL/GL/gl.h` |
| LLVM libc++, libc++abi and libunwind, from Android NDK 26.1.10909125, whose toolchain is based on LLVM commit `c4c5e79d` | `libc++_shared.so`. libc++ and libc++abi are also linked into `libmupen64plus-video-GLideN64.so`, `libmupen64plus-video-parallel.so`, `libmupen64plus-rsp-parallel.so` and `libmupen64plus-video-angrylion-plus.so`. libunwind is also linked into those four and into `libae-bridge.so`, `libhook_impl.so`, `libminiupnp-bridge.so`, `libmupen64plus-audio-android*.so`, `libmupen64plus-video-glide64mk2*.so`, `libmupen64plus-video-rice.so` and `libsoundtouch*.so`; for `armeabi-v7a`, only into those four and `liboboe.so` | `Apache-2.0 WITH LLVM-exception`; the older code is also under the University of Illinois/NCSA and MIT licenses | <https://github.com/llvm/llvm-project> | Not in this repository; reproduced below from llvm-project at commit `c4c5e79d`, except the Apache-2.0 part, which is in the app as `assets/mupen64plus_data/doc/doc/apache-license` |
| JNA 5.13.0, native part | `libjnidispatch.so` | `Apache-2.0 OR LGPL-2.1-or-later` | <https://github.com/java-native-access/jna> | Not in this repository (see [Files removed from the app](#license-and-notice-files-removed-from-the-app)); in the app: `assets/mupen64plus_data/doc/doc/apache-license`, `assets/mupen64plus_data/doc/doc/lgpl-license` |
| libffi, built into JNA's native library | `libjnidispatch.so` | `MIT` | <https://github.com/libffi/libffi>; JNA's copy at tag `5.13.0` | Not in this repository; reproduced below from JNA at tag `5.13.0` |
| GCC's unwinder from libgcc, built into JNA's native library for `armeabi-v7a` | `libjnidispatch.so` (`armeabi-v7a`) | `GPL-3.0-or-later WITH GCC-exception-3.1` | <https://gcc.gnu.org/>; the exception: <https://www.gnu.org/licenses/gcc-exception-3.1.html> | Not in this repository. The GCC Runtime Library Exception lets a program compiled with GCC carry this code under the program's own terms |
| Oboe 1.6.1 | `liboboe.so` | `Apache-2.0` | <https://github.com/google/oboe>, tag `1.6.1` | Not in this repository; in the app: `assets/mupen64plus_data/doc/doc/apache-license` |
| Mupen64Plus-AE audio plugin (plugin and dynamic-library code) | `libmupen64plus-audio-android.so`, `libmupen64plus-audio-android-fp.so` | `GPL-2.0-or-later` | This repository | Header of `mupen64plus-audio-android/src/plugin.cpp` |
| SoundTouch 2.4.1, built from source | `libsoundtouch.so`, `libsoundtouch_fp.so` | `LGPL-2.1-or-later` | <https://www.surina.net/soundtouch>; <https://codeberg.org/soundtouch/soundtouch>, tag `2.4.1` | `ndkLibs/soundtouch/COPYING.TXT`; header of `ndkLibs/soundtouch/source/SoundTouch/SoundTouch.cpp` |
| readerwriterqueue and atomicops, by Cameron Desrochers | `libmupen64plus-audio-android*.so`, `libmupen64plus-video-GLideN64.so` | `BSD-2-Clause`; the semaphore code in `atomicops.h` is `Zlib` | <https://github.com/cameron314/readerwriterqueue> | Headers of `mupen64plus-audio-android/src/readerwriterqueue.h` and `atomicops.h`; `mupen64plus-video-gliden64/upstream/licenses/readerwriterqueue/LICENSE.md` |
| PortManager, from PPSSPP | `libminiupnp-bridge.so` | `GPL-2.0-or-later` | <https://github.com/hrydgard/ppsspp> | Header of `miniupnp-bridge/src/PortManager.cpp` |
| miniupnpc 2.3.3, built from source | `libminiupnp-build.so` | `BSD-3-Clause` | <http://miniupnp.free.fr/>; <https://github.com/miniupnp/miniupnp>, tag `miniupnpc_2_3_3` | `ndkLibs/miniupnpc/LICENSE` |
| libnatpmp at commit `134fc89e` (April 2025, after release 20230423), built from source | `libnatpmp.so` | `BSD-3-Clause` | <http://miniupnp.free.fr/libnatpmp.html>; <https://github.com/miniupnp/libnatpmp>, commit `134fc89e` | `ndkLibs/libnatpmp/LICENSE` |
| mupen64plus-input-raphnetraw | `libmupen64plus-input-raphnet.so` | `GPL-2.0-or-later` | <https://github.com/raphnet/mupen64plus-input-raphnetraw>; fork <https://github.com/mupen64plus-ae/mupen64plus-input-raphnetraw> at commit `5715b943` | `mupen64plus-input-raphnet/upstream/LICENSES`, `mupen64plus-input-raphnet/upstream/COPYING` |
| libusb 1.0.19, with saki's Android back end | `libusb1.0.so` | `LGPL-2.1-or-later` | <https://github.com/libusb/libusb>; <https://github.com/saki4510t/UVCCamera> | `mupen64plus-input-raphnet/libusb/COPYING` |
| cxd4 RSP | `libmupen64plus-rsp-cxd4.so` | `CC0-1.0` | <https://github.com/mupen64plus/mupen64plus-rsp-cxd4>; fork <https://github.com/mupen64plus-ae/mupen64plus-rsp-cxd4> at commit `b252fc1e` | `mupen64plus-rsp-cxd4/upstream/COPYING` |
| SSE2NEON | `libmupen64plus-rsp-cxd4.so`, `libmupen64plus-rsp-parallel.so` | `MIT` | <https://github.com/jratcliff63367/sse2neon> | Headers of `mupen64plus-rsp-cxd4/upstream/sse2neon/SSE2NEON.h` and `mupen64plus-rsp-parallel/upstream/arch/simd/rsp/SSE2NEON.h` |
| mupen64plus-rsp-hle | `libmupen64plus-rsp-hle.so` | `GPL-2.0-or-later` | <https://github.com/mupen64plus/mupen64plus-rsp-hle> at commit `f22dc143` | `mupen64plus-rsp-hle/upstream/LICENSES` |
| parallel-rsp | `libmupen64plus-rsp-parallel.so` | `MIT OR LGPL-3.0-only` | <https://github.com/Themaister/parallel-rsp>; fork <https://github.com/mupen64plus-ae/parallel-rsp> at commit `a6566dad` | `mupen64plus-rsp-parallel/upstream/LICENSE`, `LICENSE.MIT`, `LICENSE.LESSER` |
| GNU Lightning 2.1.3, linked statically | `libmupen64plus-rsp-parallel.so` | `LGPL-3.0-or-later` | <https://www.gnu.org/software/lightning/>, commit `b0b8eb5e` | `mupen64plus-rsp-parallel/upstream/lightning/COPYING.LESSER` |
| Code from CEN64 | `libmupen64plus-rsp-parallel.so` | `BSD-3-Clause` | <https://github.com/n64dev/cen64> | Not in this repository; reproduced below from CEN64 at commit `8b89df2f` |
| GLideN64 | `libmupen64plus-video-GLideN64.so` | `GPL-2.0-only` | <https://github.com/gonetz/GLideN64> | `mupen64plus-video-gliden64/upstream/LICENSE` |
| GLideNHQ (from GlideHQ by Hiroshi Morii) | `libmupen64plus-video-GLideN64.so` | `GPL-2.0-or-later` | Not recorded | `mupen64plus-video-gliden64/upstream/licenses/GlideHQ/gpl-2.0.txt` |
| xBRZ, by Zenju | `libmupen64plus-video-GLideN64.so` | `GPL-3.0`, with an exception for linking with MAME | <https://sourceforge.net/projects/xbrz/> | Header of `mupen64plus-video-gliden64/upstream/src/GLideNHQ/TextureFilters_xbrz.cpp` |
| `GraphicBuffer.cpp`, from the Imagine engine | `libmupen64plus-video-GLideN64.so` | `GPL-3.0-or-later` | <https://github.com/Rakashazi/emu-ex-plus-alpha> | Header of `mupen64plus-video-gliden64/upstream/src/Graphics/OpenGLContext/GraphicBuffer/PrivateApi/GraphicBuffer.cpp` |
| Three files based on Chromium (`android_hardware_buffer_compat.cpp`, `GraphicBufferWrapper.cpp`, `GraphicBufferWrapper.h`) | `libmupen64plus-video-GLideN64.so` | Not stated in the files | <https://github.com/chromium/chromium> | None in the files; Chromium's own license is reproduced below from commit `afc4ac10` |
| GLSL-FXAA, by Matt DesLauriers, with code by Armin Ronacher | `libmupen64plus-video-GLideN64.so` | `MIT`; Armin Ronacher's part is `BSD-3-Clause` | <https://github.com/mattdesl/glsl-fxaa> | `mupen64plus-video-gliden64/upstream/licenses/GLSL-FXAA/LICENSE.md` |
| Glow, Nutty Software (a license file is present; whether its code is built is unknown) | `libmupen64plus-video-GLideN64.so` | MIT-style license with an attribution clause (no SPDX id) | Not recorded | `mupen64plus-video-gliden64/upstream/licenses/Glow/LICENSE` |
| gles2n64, ported by yongzh | `libmupen64plus-video-gln64.so`; parts in `libmupen64plus-video-GLideN64.so` | `LGPL-3.0` (the files do not say "or later"); three files in gln64 are `GPL-2.0-or-later` | <https://code.google.com/archive/p/gles2n64/> | `mupen64plus-video-gln64/src/COPYING`; `mupen64plus-video-gliden64/upstream/licenses/gles2n64/LICENSE` |
| Glide64mk2 | `libmupen64plus-video-glide64mk2.so`, `libmupen64plus-video-glide64mk2-egl.so` | `GPL-2.0-or-later` | <https://github.com/mupen64plus/mupen64plus-video-glide64mk2>; fork <https://github.com/mupen64plus-ae/mupen64plus-video-glide64mk2> at commit `e54bfef0` | `mupen64plus-video-glide64mk2/upstream/LICENSES` |
| Rice Video | `libmupen64plus-video-rice.so` | `GPL-2.0-or-later` | <https://github.com/mupen64plus/mupen64plus-video-rice> at commit `677257a3` | `mupen64plus-video-rice/upstream/LICENSES` |
| BMG image code, by M. Scott Heiman | `libmupen64plus-video-rice.so` | Own permissive license (no SPDX id) | Not recorded | Headers of `BMGImage.c`, `BMGUtils.c`, `bmp.c` and `pngrw.c` in `mupen64plus-video-rice/upstream/src/liblinux/` |
| angrylion-rdp-plus | `libmupen64plus-video-angrylion-plus.so` | MAME license, non-commercial (no SPDX id). Not open source. | <https://github.com/ata4/angrylion-rdp-plus> at commit `3744ec76` | `mupen64plus-video-angrylion-plus/upstream/MAME License.txt`, `mupen64plus-video-angrylion-plus/upstream/CREDITS.txt` |
| Mupen64Plus plugin code (`gfx_m64p.c`) | `libmupen64plus-video-angrylion-plus.so`, `libmupen64plus-video-parallel.so` | `GPL-2.0-or-later` | Not recorded | Headers of `mupen64plus-video-angrylion-plus/upstream/src/plugin/mupen64plus/gfx_m64p.c` and `mupen64plus-video-parallel/upstream/gfx_m64p.c` |
| paraLLEl-RDP | `libmupen64plus-video-parallel.so` | `MIT` | <https://github.com/Themaister/parallel-rdp>; fork <https://github.com/mupen64plus-ae/parallel-rdp-standalone> at commit `20e39fd1` | `mupen64plus-video-parallel/upstream/LICENSE`; header of `mupen64plus-video-parallel/upstream/parallel-rdp/rdp_dump_write.cpp` |
| Granite Vulkan back end, by Hans-Kristian Arntzen | `libmupen64plus-video-parallel.so` | `MIT` | <https://github.com/Themaister/Granite> | Headers of the compiled files in `mupen64plus-video-parallel/upstream/vulkan/` and `util/` |
| volk | `libmupen64plus-video-parallel.so` | `MIT` | <https://github.com/zeux/volk> | End of `mupen64plus-video-parallel/upstream/volk/volk.h` |
| Vulkan headers, version 187 | `libmupen64plus-video-parallel.so` | `Apache-2.0` | <https://github.com/KhronosGroup/Vulkan-Headers> | `mupen64plus-video-parallel/upstream/vulkan-headers/include/vulkan/vulkan_core.h` |
| FreeType 2.15.0, built from source, with its own copy of zlib 1.3.1 for its gzip support (`libfreetype.so` also holds libpng 1.6.59, in the libpng row) | `libfreetype.so` | `FTL OR GPL-2.0-or-later`; its `src/base/fthash.c`: a license in MIT style (no SPDX id); its copy of zlib: `Zlib` | <https://freetype.org>; <https://gitlab.freedesktop.org/freetype/freetype>, tag `VER-2-15-0` | `ndkLibs/freetype/docs/FTL.TXT`, `ndkLibs/freetype/docs/GPLv2.TXT`, `ndkLibs/freetype/LICENSE.TXT`; header of `ndkLibs/freetype/src/base/fthash.c`; `ndkLibs/freetype/src/gzip/zlib.h` |

The app does not contain the Android system libraries that these libraries use, such as
zlib's `libz.so`, libEGL, libGLESv2 and liblog: Android provides them. `libfreetype.so` holds
FreeType's own copy of zlib (see the FreeType row).

## Java and Kotlin libraries

The release build shrinks this code with R8, so some of these libraries may be only partly
in the app.

| Library | License (SPDX) | Upstream | License text |
|---|---|---|---|
| AndroidX, 63 libraries (list below) | `Apache-2.0` | <https://developer.android.com/jetpack/androidx> | In the app: `assets/mupen64plus_data/doc/doc/apache-license` |
| Material Components for Android 1.10.0 | `Apache-2.0` | <https://github.com/material-components/material-components-android> | As above |
| Kotlin standard library 1.9.10 (`kotlin-stdlib`, `-common`, `-jdk7`, `-jdk8`) | `Apache-2.0` | <https://github.com/JetBrains/kotlin> | As above |
| kotlinx.coroutines 1.6.4 (`core-jvm`, `android`) | `Apache-2.0` | <https://github.com/Kotlin/kotlinx.coroutines> | As above |
| JetBrains annotations 13.0 | `Apache-2.0` | <https://github.com/JetBrains/java-annotations> | As above |
| Google Play services: `auth` 20.7.0, `auth-api-phone` 18.0.1, `auth-base` 18.0.4, `base` 18.0.1, `basement` 18.2.0, `drive` 17.0.0, `fido` 20.0.1, `tasks` 18.0.1 | Android Software Development Kit License (no SPDX id). Not open source. | <https://developers.google.com/android/guides/overview> | <https://developer.android.com/studio/terms> |
| Google API Client Library for Java (`google-api-client` 1.25.0, `google-api-client-android` 2.2.0) | `Apache-2.0` | <https://github.com/googleapis/google-api-java-client> | In the app: `assets/mupen64plus_data/doc/doc/apache-license` |
| Google Drive API v3 client (`google-api-services-drive` v3-rev197-1.25.0) | `Apache-2.0` | <https://github.com/googleapis/google-api-java-client-services> | As above |
| Google HTTP Client Library for Java (`google-http-client` 1.43.3, `-gson` 1.43.3, `-jackson2` 1.25.0) | `Apache-2.0` | <https://github.com/googleapis/google-http-java-client> | As above |
| Google OAuth Client Library for Java 1.25.0 | `Apache-2.0` | <https://github.com/googleapis/google-oauth-java-client> | As above |
| Gson 2.10.1 | `Apache-2.0` | <https://github.com/google/gson> | As above |
| Guava 30.1.1-android, with `failureaccess` 1.0.1 and the empty `listenablefuture` | `Apache-2.0` | <https://github.com/google/guava> | As above |
| Jackson core 2.9.6 | `Apache-2.0` | <https://github.com/FasterXML/jackson-core> | As above; its notice is reproduced below |
| Apache Commons Lang 3.13.0, Text 1.11.0, IO 1.3.2, Compress 1.20, Codec 1.11, Logging 1.2 | `Apache-2.0` | <https://commons.apache.org/> | As above; their notices are reproduced below |
| Apache HttpClient 4.5.14 and HttpCore 4.4.16 | `Apache-2.0` | <https://hc.apache.org/> | As above; their notices are reproduced below |
| OpenCensus API and HTTP utilities 0.31.1 | `Apache-2.0` | <https://github.com/census-instrumentation/opencensus-java> | As above |
| gRPC Context 1.27.2 | `Apache-2.0` | <https://github.com/grpc/grpc-java> | As above |
| JSR 305 annotations 3.0.2 | `Apache-2.0`, as its POM file says | <https://code.google.com/archive/p/jsr-305/> | As above |
| Error Prone annotations 2.18.0 | `Apache-2.0` | <https://errorprone.info> | As above |
| J2ObjC annotations 2.8 | `Apache-2.0` | <https://github.com/google/j2objc/> | As above |
| Checker Framework `checker-compat-qual` 2.5.5 | Its POM file lists two licenses: `GPL-2.0-only WITH Classpath-exception-2.0` and `MIT` | <https://checkerframework.org> | Not in this repository or in its jar; the MIT text is reproduced below from the Checker Framework at tag `checker-framework-2.5.5` |
| Reactive Streams 1.0.0 | `CC0-1.0` | <https://github.com/reactive-streams/reactive-streams-jvm> | None needed |
| XZ for Java 1.9 | Public domain (no SPDX id) | <https://tukaani.org/xz/java.html> | None |
| JNA and JNA Platform 5.13.0 | `Apache-2.0 OR LGPL-2.1-or-later` | <https://github.com/java-native-access/jna> | In the app: `assets/mupen64plus_data/doc/doc/apache-license`, `assets/mupen64plus_data/doc/doc/lgpl-license` |
| ColorPickerView 2.3.0 | `Apache-2.0` | <https://github.com/skydoves/colorpickerview> | In the app: `assets/mupen64plus_data/doc/doc/apache-license` |
| `desugar_jdk_libs_nio` 2.0.4 (a part of OpenJDK that the build adds for older Android versions) | `GPL-2.0-only WITH Classpath-exception-2.0` | <https://github.com/google/desugar_jdk_libs> | In the app: `assets/mupen64plus_data/doc/doc/gpl-license` (GPL-2.0) |
| Two of the app's Java files, © 2018 Google LLC (`DriveServiceHelper.java`, `GoogleDriveFileHolder.java`) | `Apache-2.0` | Not recorded | Headers of the two files in `app/src/main/java/paulscode/android/mupen64plusae/util/`; in the app: `assets/mupen64plus_data/doc/doc/apache-license` |

<details>
<summary>The 63 AndroidX libraries</summary>

`activity` 1.8.0, `activity-ktx` 1.8.0, `annotation` 1.3.0, `annotation-experimental` 1.3.0,
`appcompat` 1.6.1, `appcompat-resources` 1.6.1, `arch.core:core-common` 2.2.0,
`arch.core:core-runtime` 2.2.0, `cardview` 1.0.0, `collection` 1.1.0, `collection-ktx` 1.1.0,
`concurrent-futures` 1.1.0, `constraintlayout` 2.1.4, `constraintlayout-core` 1.0.4,
`coordinatorlayout` 1.1.0, `core` 1.9.0, `core-ktx` 1.9.0, `cursoradapter` 1.0.0,
`customview` 1.1.0, `databinding:viewbinding` 8.2.2, `documentfile` 1.0.1, `drawerlayout` 1.1.1,
`dynamicanimation` 1.0.0, `emoji2` 1.2.0, `emoji2-views-helper` 1.2.0, `fragment` 1.3.6,
`fragment-ktx` 1.3.6, `interpolator` 1.0.0, `legacy-support-core-utils` 1.0.0,
`lifecycle-common` 2.6.2, `lifecycle-common-java8` 2.6.2, `lifecycle-livedata` 2.6.2,
`lifecycle-livedata-core` 2.6.2, `lifecycle-livedata-core-ktx` 2.6.2, `lifecycle-process` 2.6.2,
`lifecycle-reactivestreams` 2.6.2, `lifecycle-runtime` 2.6.2, `lifecycle-runtime-ktx` 2.6.2,
`lifecycle-service` 2.6.2, `lifecycle-viewmodel` 2.6.2, `lifecycle-viewmodel-ktx` 2.6.2,
`lifecycle-viewmodel-savedstate` 2.6.2, `loader` 1.0.0, `localbroadcastmanager` 1.0.0,
`multidex` 2.0.1, `preference` 1.2.1, `print` 1.0.0, `profileinstaller` 1.3.0,
`recyclerview` 1.1.0, `resourceinspection-annotation` 1.0.1, `savedstate` 1.2.1,
`savedstate-ktx` 1.2.1, `slidingpanelayout` 1.2.0, `startup-runtime` 1.1.1, `tracing` 1.0.0,
`transition` 1.4.1, `tvprovider` 1.0.0, `vectordrawable` 1.1.0, `vectordrawable-animated` 1.1.0,
`versionedparcelable` 1.1.1, `viewpager` 1.0.0, `viewpager2` 1.0.0, `window` 1.0.0.

</details>

## License and notice files removed from the app

Line 146 of `app/build.gradle` removes these files from the app when it is built. The
libraries that they belong to are still in the app, under the licenses below. The text of
each removed notice file is reproduced in [Full license notices](#full-license-notices).

| File removed | Libraries that contain it | License |
|---|---|---|
| `META-INF/LICENSE` | JNA 5.13.0, JNA Platform 5.13.0 | `Apache-2.0 OR LGPL-2.1-or-later` |
| `META-INF/LICENSE` | Jackson core 2.9.6, Apache HttpClient 4.5.14, Apache HttpCore 4.4.16 | `Apache-2.0` |
| `META-INF/LGPL2.1`, `META-INF/AL2.0` | JNA 5.13.0, JNA Platform 5.13.0 | `Apache-2.0 OR LGPL-2.1-or-later` |
| `META-INF/NOTICE` | Jackson core 2.9.6, Apache HttpClient 4.5.14, Apache HttpCore 4.4.16 | `Apache-2.0` |
| `META-INF/LICENSE.txt`, `META-INF/NOTICE.txt` | Apache Commons Codec 1.11, IO 1.3.2, Logging 1.2, Compress 1.20, Lang 3.13.0, Text 1.11.0 | `Apache-2.0` |
| `META-INF/DEPENDENCIES` (a list of dependencies) | Apache HttpClient 4.5.14, Apache HttpCore 4.4.16 | `Apache-2.0` |
| `META-INF/license.txt`, `META-INF/notice.txt`, `META-INF/ASL2.0`, `META-INF/beans.xml` | None of the libraries in the app | Not applicable |

## Data, art and fonts

Paths in this table are under `assets/mupen64plus_data/` unless they say otherwise.

| Item | Origin | License (SPDX) | License text |
|---|---|---|---|
| `font.ttf` | Bitstream Vera Sans, © 2003 Bitstream, Inc. | `Bitstream-Vera` | `doc/doc/font-license` |
| `doc/LICENSES`, `doc/CREDITS`, `doc/INSTALL`, `doc/README`, `doc/RELEASE`, `doc/doc/emuwiki-api-doc/` (13 files), `doc/doc/new_dynarec.txt`, `mupen64plus.ini` | Mupen64Plus core documents and game database | `GPL-2.0` | `doc/doc/gpl-license` |
| `mupencheat.default` | Cheat database, from the Mupen64Plus core | `GPL-2.0-or-later` | Its header; `doc/doc/gpl-license` |
| `GLideN64.custom.ini` | GLideN64 settings | `GPL-2.0-only` | `mupen64plus-video-gliden64/upstream/LICENSE` |
| `Glide64mk2.ini` | Glide64mk2 settings | `GPL-2.0` | `mupen64plus-video-glide64mk2/upstream/LICENSES` |
| `RiceVideoLinux.ini` | Rice Video settings | `GPL-2.0` | `mupen64plus-video-rice/upstream/LICENSES` |
| `gln64.conf`, `gln64rom.conf` | gln64 settings ("by Orkin / glN64 developers and Adventus") | Not stated (unknown) | None |
| `m64p_test_rom.v64` | Mupen64Plus test ROM, by Marshallh | `GPL-2.0`, according to [mupen64plus-rom](https://github.com/mupen64plus/mupen64plus-rom); this repository does not state it | `doc/doc/gpl-license` |
| `shaders/crt-geom.glsl` | CRT shader by cgwg, Themaister and DOLLS | `GPL-2.0-or-later` | Its header |
| `shaders/fxaa.glsl` | From [glsl-fxaa](https://github.com/mattdesl/glsl-fxaa) (`MIT`), based on code by Armin Ronacher | `BSD-3-Clause`, as the file says | Its header; reproduced below |
| `shaders/scalefx-pass0.glsl` to `scalefx-pass4.glsl` | ScaleFX, by Sp00kyFox | `MIT` | Their headers; reproduced below |
| `shaders/scanlines-sine-abs.glsl` | By RiskyJumps | Public domain, as the file says | Its header |
| `shaders/blur9x9.glsl` | Ported from [gizmo98/common-shaders](https://github.com/gizmo98/common-shaders) | Not stated (unknown) | None |
| `shaders/n64-dither.glsl` | gizmo98's N64 dither | Not stated (unknown) | None |
| `skins/touchscreen/Hires/` | Its number textures come from Nerrel's Majora's Mask N64HD texture pack, and its L, R and Z buttons from Federelli's Ocarina of Time retexture pack. The skin was ported by a contributor who is not named. | Unknown: the terms of the two texture packs are unknown | `skins/touchscreen/CREDITS.txt` (credits only) |
| `skins/touchscreen/` GameCube (KRAID FFF), JoshaGibs (JoshaGibs), 3DSXL, Flat, Newd, Switch and WiiU (Juan Jesus Tovar), Colored, Black and White (Henry Mora) | Named in `CREDITS.txt` | Not stated (unknown) | `skins/touchscreen/CREDITS.txt` (credits only) |
| `skins/touchscreen/Outline/`, `skins/touchscreen/Shaded/` | Authors unknown ("Can't find contributors") | Unknown | None |
| Three icons in `app/src/main/res/drawable/`: `ic_trophy.xml`, `ic_drawer_open.xml` and `ic_fab_refresh_roms.xml` | Material Icons "emoji_events", "menu" and "add", from [google/material-design-icons](https://github.com/google/material-design-icons) | `Apache-2.0` | `doc/doc/apache-license` |
| Six contributor pictures in the credits screen (`app/src/main/res/drawable/credit_avatar_*.png`) | Not recorded | Not stated (unknown) | None |
| `mozilla/public-suffix-list.txt`, outside `assets/` (it comes inside Apache HttpClient 4.5.14) | Public Suffix List, from Mozilla | `MPL-2.0` | Its header, reproduced below; the MPL 2.0 text is at <https://mozilla.org/MPL/2.0/> |
| `profiles/`, `shaders/default.glsl`, `shaders/test-pattern.glsl` | The app's own | `GPL-3.0` (the app's license) | [`gpl-license`](gpl-license) |

## License texts in the app

The app contains these license texts, in `assets/mupen64plus_data/doc/`:

- `doc/gpl-license` (GPL-2.0), `doc/gpl-license-3` (GPL-3.0), `doc/lgpl-license`
  (LGPL-2.1), `doc/apache-license` (Apache-2.0) and `doc/font-license` (Bitstream Vera);
- `LICENSES` and `CREDITS`, which are the Mupen64Plus core's files and describe only the
  core.

The other license texts and notices are not in the app. The next section reproduces them in
full. Two things remain unknown: the license of the three GLideN64 files based on Chromium,
which state none (the next section gives Chromium's own license), and the terms of the art
marked "unknown" above.

## Full license notices

This section gives, in full, the notice of every component in the app whose license asks for
its copyright or permission notice to be kept or reproduced. It leaves out the license texts
that the app already contains (GPL-2.0, GPL-3.0, LGPL-2.1, Apache-2.0 and the Bitstream Vera
license), and the CC0 and public-domain items.

Each block is copied from the file named above it: a file of this repository at commit
`9bcca07a6`, or a file of another project at the commit or release named. Where several
components carry the same text, one block names all of them. Three things differ from a
plain copy: `MAME License.txt` and FreeType's `NOTICE` are in ISO-8859-1 and appear here in
UTF-8; the `NOTICE.txt` files of Apache Commons Codec and Commons IO have Windows line
endings, which appear here as Unix line endings; and Glow's `LICENSE`, whose last line has no
line ending, appears here with one.

<!-- notice: rcheevos -->
rcheevos 12.5.0, in `libmupen64plus-core.so`. Source: `mupen64plus-core/rcheevos/LICENSE`, lines 1-21, at `9bcca07a6`.
```text
MIT License

Copyright (c) 2018 RetroAchievements.org

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

<!-- notice: tiny-aes-c-header -->
tiny-AES-c, inside rcheevos in `libmupen64plus-core.so`: the notice in the file. Source: `mupen64plus-core/rcheevos/src/rhash/aes.c`, lines 1-3, at `9bcca07a6`.
```text
/* This file is sourced from https://github.com/kokke/tiny-AES-c, with unused code excised.
 * This code is licensed under the Unlicense license, effectively public domain.
 * https://github.com/kokke/tiny-AES-c/blob/f06ac37/unlicense.txt
```

<!-- notice: tiny-aes-c-unlicense -->
tiny-AES-c: the Unlicense text that the notice above names. Source: <https://raw.githubusercontent.com/kokke/tiny-AES-c/f06ac37fc31dfdaca2e0d9bec83f90d5663c319b/unlicense.txt>, lines 1-24, commit `f06ac37fc31dfdaca2e0d9bec83f90d5663c319b`.
```text
This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org/>
```

<!-- notice: minizip -->
minizip from zlib 1.3.1.2 (`ioapi.c`, `unzip.c` and `zip.c`, whose headers say version 1.1), in `libmupen64plus-core.so`. Source: `mupen64plus-core/upstream/subprojects/minizip/unzip.h`, lines 5-33, at `9bcca07a6`.
```text
         Copyright (C) 1998-2010 Gilles Vollant (minizip) ( http://www.winimage.com/zLibDll/minizip.html )

         Modifications of Unzip for Zip64
         Copyright (C) 2007-2008 Even Rouault

         Modifications for Zip64 support on both zip and unzip
         Copyright (C) 2009-2010 Mathias Svensson ( http://result42.com )

         For more info read MiniZip_info.txt

         ---------------------------------------------------------------------------------

        Condition of use and distribution are the same than zlib :

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
```

<!-- notice: minizip-skipset -->
minizip's `skipset.h` (zlib 1.3.1.2), compiled into `zip.c` in `libmupen64plus-core.so`; the `MiniZip_info.txt` it names is not in this repository, and minizip's license is in the block above. Source: `mupen64plus-core/upstream/subprojects/minizip/skipset.h`, lines 2-3, at `9bcca07a6`.
```text
// Copyright (C) 2024 Mark Adler
// See MiniZip_info.txt for the license.
```

<!-- notice: minizip-ints -->
minizip's `ints.h` (zlib 1.3.1.2), which `ioapi.h` includes, in `libmupen64plus-core.so`; the copyright notice in `zlib.h` that it points to is the zlib license, with the same terms as the minizip block above. Source: `mupen64plus-core/upstream/subprojects/minizip/ints.h`, lines 2-3, at `9bcca07a6`.
```text
 * Copyright (C) 2024 Mark Adler
 * For conditions of distribution and use, see the copyright notice in zlib.h
```

<!-- notice: md5 -->
MD5 code by L. Peter Deutsch, in `libmupen64plus-core.so`. Source: `mupen64plus-core/upstream/subprojects/md5/md5.c`, lines 2-18, at `9bcca07a6`.
```text
  Copyright (C) 1999, 2000, 2002 Aladdin Enterprises.  All rights reserved.

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
```

<!-- notice: xxhash-0-8-3 -->
xxHash 0.8.3, in `libmupen64plus-core.so`. Source: `mupen64plus-core/upstream/subprojects/xxhash/xxhash.h`, lines 4-29, at `9bcca07a6`.
```text
 * Copyright (C) 2012-2023 Yann Collet
 *
 * BSD 2-Clause License (https://www.opensource.org/licenses/bsd-license.php)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following disclaimer
 *      in the documentation and/or other materials provided with the
 *      distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

<!-- notice: xxhash-0-8-0 -->
xxHash 0.8.0, in `libmupen64plus-video-GLideN64.so` and, on ARM, in both Glide64mk2 libraries. Source: `mupen64plus-video-gliden64/upstream/src/xxHash/xxhash.h`, lines 4-29, at `9bcca07a6`; the same text is in `mupen64plus-video-glide64mk2/upstream/src/Neon/xxhash.h`, lines 4-29.
```text
 * Copyright (C) 2012-2020 Yann Collet
 *
 * BSD 2-Clause License (https://www.opensource.org/licenses/bsd-license.php)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following disclaimer
 *      in the documentation and/or other materials provided with the
 *      distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

<!-- notice: cic-nus-6105 -->
CIC-NUS-6105 code by X-Scale, in `libmupen64plus-core.so`. Source: `mupen64plus-core/upstream/src/device/pif/n64_cic_nus_6105.c`, lines 2-26, at `9bcca07a6`.
```text
 * Copyright 2011 X-Scale. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met:
 *
 *    1. Redistributions of source code must retain the above copyright notice,
 *       this list of conditions and the following disclaimer.
 *    2. Redistributions in binary form must reproduce the above copyright 
 *       notice, this list of conditions and the following disclaimer in the 
 *       documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY X-Scale ``AS IS'' AND ANY EXPRESS OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO 
 * EVENT SHALL X-Scale OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, 
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF 
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING 
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are 
 * those of the authors and should not be interpreted as representing official 
 * policies, either expressed or implied, of X-Scale.
```

<!-- notice: sdl-net-2-4-0 -->
SDL_net 2.4.0, `libSDL2_net.so`. Source: `SDL2_net-2.4.0/include/SDL_net.h` in <https://github.com/libsdl-org/SDL_net/releases/download/release-2.4.0/SDL2_net-2.4.0.tar.gz> (archive SHA-256 `9cbca2527feb3f1a622d48ba65cc7dee9b1e3f2c55ceafb7d7720bb058aafb30`), lines 3-20; the same file is `ndkLibs/SDL_net/include/SDL_net.h` in this repository. Its `LICENSE.txt` has the same terms, with only the first copyright line.
```text
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>
  Copyright (C) 2012 Simeon Maxein <smaxein@googlemail.com>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
```

<!-- notice: sdl-net -->
SDL_net of upstream commit `01285e4a` (2020, after version 2.0.1), the prebuilt `libSDL2_net.so` in V339. Source: `ndkLibs/SDL_net/include/SDL_net.h`, lines 3-20, at `9bcca07a6`.
```text
  Copyright (C) 1997-2020 Sam Lantinga <slouken@libsdl.org>
  Copyright (C) 2012 Simeon Maxein <smaxein@googlemail.com>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
```

<!-- notice: libadrenotools -->
libadrenotools and liblinkernsbypass, in `libae-bridge.so` and the four hook libraries. Source: `adrenotools/LICENSE`, lines 1-24, at `9bcca07a6`; the same text is in `adrenotools/lib/linkernsbypass/LICENSE`, lines 1-24.
```text
BSD 2-Clause License

Copyright (c) 2021, Billy Laws

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

<!-- notice: khronos-gl-2016 -->
Khronos OpenGL header `glcorearb.h`, used by EGLLoader in `libae-bridge.so` and `libmupen64plus-video-glide64mk2-egl.so`; GLideN64's own `glcorearb.h` and `glext.h`, in `libmupen64plus-video-GLideN64.so`. Source: `ndkLibs/GL/GL/glcorearb.h`, lines 9-28, at `9bcca07a6`; the same text is in `mupen64plus-video-gliden64/upstream/src/inc/GL/glcorearb.h` and `mupen64plus-video-gliden64/upstream/src/inc/GL/glext.h`, lines 9-28.
```text
** Copyright (c) 2013-2016 The Khronos Group Inc.
**
** Permission is hereby granted, free of charge, to any person obtaining a
** copy of this software and/or associated documentation files (the
** "Materials"), to deal in the Materials without restriction, including
** without limitation the rights to use, copy, modify, merge, publish,
** distribute, sublicense, and/or sell copies of the Materials, and to
** permit persons to whom the Materials are furnished to do so, subject to
** the following conditions:
**
** The above copyright notice and this permission notice shall be included
** in all copies or substantial portions of the Materials.
**
** THE MATERIALS ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
** EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
** MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
** IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
** CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
** TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
** MATERIALS OR THE USE OR OTHER DEALINGS IN THE MATERIALS.
```

<!-- notice: khronos-glext-2014 -->
Khronos OpenGL header `glext.h`, used by EGLLoader in `libae-bridge.so` and `libmupen64plus-video-glide64mk2-egl.so`. Source: `ndkLibs/GL/GL/glext.h`, lines 9-28, at `9bcca07a6`.
```text
** Copyright (c) 2013-2014 The Khronos Group Inc.
**
** Permission is hereby granted, free of charge, to any person obtaining a
** copy of this software and/or associated documentation files (the
** "Materials"), to deal in the Materials without restriction, including
** without limitation the rights to use, copy, modify, merge, publish,
** distribute, sublicense, and/or sell copies of the Materials, and to
** permit persons to whom the Materials are furnished to do so, subject to
** the following conditions:
**
** The above copyright notice and this permission notice shall be included
** in all copies or substantial portions of the Materials.
**
** THE MATERIALS ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
** EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
** MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
** IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
** CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
** TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
** MATERIALS OR THE USE OR OTHER DEALINGS IN THE MATERIALS.
```

<!-- notice: mesa-gl-h -->
Mesa's `gl.h` header, used by EGLLoader in `libae-bridge.so` and `libmupen64plus-video-glide64mk2-egl.so`. Source: `ndkLibs/GL/GL/gl.h`, lines 4-23, at `9bcca07a6`.
```text
 * Copyright (C) 1999-2006  Brian Paul   All Rights Reserved.
 * Copyright (C) 2009  VMware, Inc.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
```

<!-- notice: libpng-1-6-59 -->
libpng 1.6.59, a static library inside `libmupen64plus-core.so`, `libmupen64plus-video-rice.so` and `libfreetype.so`. Source: `libpng-1.6.59/LICENSE` in <https://download.sourceforge.net/libpng/libpng-1.6.59.tar.xz> (archive SHA-256 `d80dd2a38a37f803cb9b6ac7b14bd6e74ddc3b654780a8380bdf93523fdb4389`), lines 1-134; the same file is `ndkLibs/png/LICENSE` in this repository.
```text
COPYRIGHT NOTICE, DISCLAIMER, and LICENSE
=========================================

PNG Reference Library License version 2
---------------------------------------

 * Copyright (c) 1995-2026 The PNG Reference Library Authors.
 * Copyright (c) 2018-2026 Cosmin Truta.
 * Copyright (c) 2000-2002, 2004, 2006-2018 Glenn Randers-Pehrson.
 * Copyright (c) 1996-1997 Andreas Dilger.
 * Copyright (c) 1995-1996 Guy Eric Schalnat, Group 42, Inc.

The software is supplied "as is", without warranty of any kind,
express or implied, including, without limitation, the warranties
of merchantability, fitness for a particular purpose, title, and
non-infringement.  In no event shall the Copyright owners, or
anyone distributing the software, be liable for any damages or
other liability, whether in contract, tort or otherwise, arising
from, out of, or in connection with the software, or the use or
other dealings in the software, even if advised of the possibility
of such damage.

Permission is hereby granted to use, copy, modify, and distribute
this software, or portions hereof, for any purpose, without fee,
subject to the following restrictions:

 1. The origin of this software must not be misrepresented; you
    must not claim that you wrote the original software.  If you
    use this software in a product, an acknowledgment in the product
    documentation would be appreciated, but is not required.

 2. Altered source versions must be plainly marked as such, and must
    not be misrepresented as being the original software.

 3. This Copyright notice may not be removed or altered from any
    source or altered source distribution.


PNG Reference Library License version 1 (for libpng 0.5 through 1.6.35)
-----------------------------------------------------------------------

libpng versions 1.0.7, July 1, 2000, through 1.6.35, July 15, 2018 are
Copyright (c) 2000-2002, 2004, 2006-2018 Glenn Randers-Pehrson, are
derived from libpng-1.0.6, and are distributed according to the same
disclaimer and license as libpng-1.0.6 with the following individuals
added to the list of Contributing Authors:

    Simon-Pierre Cadieux
    Eric S. Raymond
    Mans Rullgard
    Cosmin Truta
    Gilles Vollant
    James Yu
    Mandar Sahastrabuddhe
    Google Inc.
    Vadim Barkov

and with the following additions to the disclaimer:

    There is no warranty against interference with your enjoyment of
    the library or against infringement.  There is no warranty that our
    efforts or the library will fulfill any of your particular purposes
    or needs.  This library is provided with all faults, and the entire
    risk of satisfactory quality, performance, accuracy, and effort is
    with the user.

Some files in the "contrib" directory and some configure-generated
files that are distributed with libpng have other copyright owners, and
are released under other open source licenses.

libpng versions 0.97, January 1998, through 1.0.6, March 20, 2000, are
Copyright (c) 1998-2000 Glenn Randers-Pehrson, are derived from
libpng-0.96, and are distributed according to the same disclaimer and
license as libpng-0.96, with the following individuals added to the
list of Contributing Authors:

    Tom Lane
    Glenn Randers-Pehrson
    Willem van Schaik

libpng versions 0.89, June 1996, through 0.96, May 1997, are
Copyright (c) 1996-1997 Andreas Dilger, are derived from libpng-0.88,
and are distributed according to the same disclaimer and license as
libpng-0.88, with the following individuals added to the list of
Contributing Authors:

    John Bowler
    Kevin Bracey
    Sam Bushell
    Magnus Holmgren
    Greg Roelofs
    Tom Tanner

Some files in the "scripts" directory have other copyright owners,
but are released under this license.

libpng versions 0.5, May 1995, through 0.88, January 1996, are
Copyright (c) 1995-1996 Guy Eric Schalnat, Group 42, Inc.

For the purposes of this copyright and license, "Contributing Authors"
is defined as the following set of individuals:

    Andreas Dilger
    Dave Martindale
    Guy Eric Schalnat
    Paul Schmidt
    Tim Wegner

The PNG Reference Library is supplied "AS IS".  The Contributing
Authors and Group 42, Inc. disclaim all warranties, expressed or
implied, including, without limitation, the warranties of
merchantability and of fitness for any purpose.  The Contributing
Authors and Group 42, Inc. assume no liability for direct, indirect,
incidental, special, exemplary, or consequential damages, which may
result from the use of the PNG Reference Library, even if advised of
the possibility of such damage.

Permission is hereby granted to use, copy, modify, and distribute this
source code, or portions hereof, for any purpose, without fee, subject
to the following restrictions:

 1. The origin of this source code must not be misrepresented.

 2. Altered versions must be plainly marked as such and must not
    be misrepresented as being the original source.

 3. This Copyright notice may not be removed or altered from any
    source or altered source distribution.

The Contributing Authors and Group 42, Inc. specifically permit,
without fee, and encourage the use of this source code as a component
to supporting the PNG file format in commercial products.  If you use
this source code in a product, acknowledgment is not required but would
be appreciated.
```

<!-- notice: libpng -->
libpng 1.6.34, in V339's core, Rice and prebuilt `libfreetype.so` (see [Release V339](#release-v339)). Source: `ndkLibs/png/LICENSE`, lines 1-133, at `9bcca07a6`.
```text

This copy of the libpng notices is provided for your convenience.  In case of
any discrepancy between this copy and the notices in the file png.h that is
included in the libpng distribution, the latter shall prevail.

COPYRIGHT NOTICE, DISCLAIMER, and LICENSE:

If you modify libpng you may insert additional notices immediately following
this sentence.

This code is released under the libpng license.

libpng versions 1.0.7, July 1, 2000 through 1.6.34, September 29, 2017 are
Copyright (c) 2000-2002, 2004, 2006-2017 Glenn Randers-Pehrson, are
derived from libpng-1.0.6, and are distributed according to the same
disclaimer and license as libpng-1.0.6 with the following individuals
added to the list of Contributing Authors:

   Simon-Pierre Cadieux
   Eric S. Raymond
   Mans Rullgard
   Cosmin Truta
   Gilles Vollant
   James Yu
   Mandar Sahastrabuddhe
   Google Inc.
   Vadim Barkov

and with the following additions to the disclaimer:

   There is no warranty against interference with your enjoyment of the
   library or against infringement.  There is no warranty that our
   efforts or the library will fulfill any of your particular purposes
   or needs.  This library is provided with all faults, and the entire
   risk of satisfactory quality, performance, accuracy, and effort is with
   the user.

Some files in the "contrib" directory and some configure-generated
files that are distributed with libpng have other copyright owners and
are released under other open source licenses.

libpng versions 0.97, January 1998, through 1.0.6, March 20, 2000, are
Copyright (c) 1998-2000 Glenn Randers-Pehrson, are derived from
libpng-0.96, and are distributed according to the same disclaimer and
license as libpng-0.96, with the following individuals added to the list
of Contributing Authors:

   Tom Lane
   Glenn Randers-Pehrson
   Willem van Schaik

libpng versions 0.89, June 1996, through 0.96, May 1997, are
Copyright (c) 1996-1997 Andreas Dilger, are derived from libpng-0.88,
and are distributed according to the same disclaimer and license as
libpng-0.88, with the following individuals added to the list of
Contributing Authors:

   John Bowler
   Kevin Bracey
   Sam Bushell
   Magnus Holmgren
   Greg Roelofs
   Tom Tanner

Some files in the "scripts" directory have other copyright owners
but are released under this license.

libpng versions 0.5, May 1995, through 0.88, January 1996, are
Copyright (c) 1995-1996 Guy Eric Schalnat, Group 42, Inc.

For the purposes of this copyright and license, "Contributing Authors"
is defined as the following set of individuals:

   Andreas Dilger
   Dave Martindale
   Guy Eric Schalnat
   Paul Schmidt
   Tim Wegner

The PNG Reference Library is supplied "AS IS".  The Contributing Authors
and Group 42, Inc. disclaim all warranties, expressed or implied,
including, without limitation, the warranties of merchantability and of
fitness for any purpose.  The Contributing Authors and Group 42, Inc.
assume no liability for direct, indirect, incidental, special, exemplary,
or consequential damages, which may result from the use of the PNG
Reference Library, even if advised of the possibility of such damage.

Permission is hereby granted to use, copy, modify, and distribute this
source code, or portions hereof, for any purpose, without fee, subject
to the following restrictions:

  1. The origin of this source code must not be misrepresented.

  2. Altered versions must be plainly marked as such and must not
     be misrepresented as being the original source.

  3. This Copyright notice may not be removed or altered from any
     source or altered source distribution.

The Contributing Authors and Group 42, Inc. specifically permit, without
fee, and encourage the use of this source code as a component to
supporting the PNG file format in commercial products.  If you use this
source code in a product, acknowledgment is not required but would be
appreciated.

END OF COPYRIGHT NOTICE, DISCLAIMER, and LICENSE.

TRADEMARK:

The name "libpng" has not been registered by the Copyright owner
as a trademark in any jurisdiction.  However, because libpng has
been distributed and maintained world-wide, continually since 1995,
the Copyright owner claims "common-law trademark protection" in any
jurisdiction where common-law trademark is recognized.

OSI CERTIFICATION:

Libpng is OSI Certified Open Source Software.  OSI Certified Open Source is
a certification mark of the Open Source Initiative. OSI has not addressed
the additional disclaimers inserted at version 1.0.7.

EXPORT CONTROL:

The Copyright owner believes that the Export Control Classification
Number (ECCN) for libpng is EAR99, which means not subject to export
controls or International Traffic in Arms Regulations (ITAR) because
it is open source, publicly available software, that does not contain
any encryption software.  See the EAR, paragraphs 734.3(b)(3) and
734.7(b).

Glenn Randers-Pehrson
glennrp at users.sourceforge.net
September 29, 2017
```

<!-- notice: llvm-exceptions -->
LLVM libc++, libc++abi and libunwind, in `libc++_shared.so` and linked into the other libraries that the LLVM row of [Native libraries](#native-libraries) names: the LLVM exceptions to the Apache License 2.0 (whose text is in the app) and the notes on third-party and legacy code. Source: <https://raw.githubusercontent.com/llvm/llvm-project/c4c5e79dd4b4c78eee7cffd9b0d7394b5bedcf12/libcxx/LICENSE.TXT>, lines 208-238, commit `c4c5e79dd4b4c78eee7cffd9b0d7394b5bedcf12`; the same lines are in `libcxxabi/LICENSE.TXT` and `libunwind/LICENSE.TXT` at that commit.
```text
---- LLVM Exceptions to the Apache 2.0 License ----

As an exception, if, as a result of your compiling your source code, portions
of this Software are embedded into an Object form of such source code, you
may redistribute such embedded portions in such Object form without complying
with the conditions of Sections 4(a), 4(b) and 4(d) of the License.

In addition, if you combine or link compiled forms of this Software with
software that is licensed under the GPLv2 ("Combined Software") and if a
court of competent jurisdiction determines that the patent provision (Section
3), the indemnity provision (Section 9) or other Section of the License
conflicts with the conditions of the GPLv2, you may retroactively and
prospectively choose to deem waived or otherwise exclude such Section(s) of
the License, but only in their entirety and only with respect to the Combined
Software.

==============================================================================
Software from third parties included in the LLVM Project:
==============================================================================
The LLVM Project contains third party software which is under different license
terms. All such code will be identified clearly using at least one of two
mechanisms:
1) It will be in a separate directory tree with its own `LICENSE.txt` or
   `LICENSE` file at the top containing the specific license and restrictions
   which apply to that software, or
2) It will contain specific license and restriction terms at the top of every
   file.

==============================================================================
Legacy LLVM License (https://llvm.org/docs/DeveloperPolicy.html#legacy):
==============================================================================
```

<!-- notice: llvm-legacy-libcxx -->
LLVM libc++, in `libc++_shared.so`, and linked into `libmupen64plus-video-GLideN64.so`, `libmupen64plus-video-parallel.so`, `libmupen64plus-rsp-parallel.so` and `libmupen64plus-video-angrylion-plus.so`. Source: <https://raw.githubusercontent.com/llvm/llvm-project/c4c5e79dd4b4c78eee7cffd9b0d7394b5bedcf12/libcxx/LICENSE.TXT>, lines 240-243, commit `c4c5e79dd4b4c78eee7cffd9b0d7394b5bedcf12`.
```text
The libc++ library is dual licensed under both the University of Illinois
"BSD-Like" license and the MIT license.  As a user of this code you may choose
to use it under either license.  As a contributor, you agree to allow your code
to be used under both.
```

<!-- notice: llvm-legacy-libcxxabi -->
LLVM libc++abi, in `libc++_shared.so`, and linked into `libmupen64plus-video-GLideN64.so`, `libmupen64plus-video-parallel.so`, `libmupen64plus-rsp-parallel.so` and `libmupen64plus-video-angrylion-plus.so`. Source: <https://raw.githubusercontent.com/llvm/llvm-project/c4c5e79dd4b4c78eee7cffd9b0d7394b5bedcf12/libcxxabi/LICENSE.TXT>, lines 240-243, commit `c4c5e79dd4b4c78eee7cffd9b0d7394b5bedcf12`.
```text
The libc++abi library is dual licensed under both the University of Illinois
"BSD-Like" license and the MIT license.  As a user of this code you may choose
to use it under either license.  As a contributor, you agree to allow your code
to be used under both.
```

<!-- notice: llvm-legacy-libunwind -->
LLVM libunwind, in `libc++_shared.so`, and linked into 14 other libraries for `arm64-v8a`, 13 for `x86` and `x86_64`, and 5 for `armeabi-v7a`, which the LLVM row of [Native libraries](#native-libraries) names. Source: <https://raw.githubusercontent.com/llvm/llvm-project/c4c5e79dd4b4c78eee7cffd9b0d7394b5bedcf12/libunwind/LICENSE.TXT>, lines 240-243, commit `c4c5e79dd4b4c78eee7cffd9b0d7394b5bedcf12`.
```text
The libunwind library is dual licensed under both the University of Illinois
"BSD-Like" license and the MIT license.  As a user of this code you may choose
to use it under either license.  As a contributor, you agree to allow your code
to be used under both.
```

<!-- notice: llvm-legacy-licenses -->
LLVM libc++, libc++abi and libunwind, in the libraries named in the three blocks above: the two legacy licenses. Source: <https://raw.githubusercontent.com/llvm/llvm-project/c4c5e79dd4b4c78eee7cffd9b0d7394b5bedcf12/libcxx/LICENSE.TXT>, lines 245-311, commit `c4c5e79dd4b4c78eee7cffd9b0d7394b5bedcf12`; the same lines are in `libcxxabi/LICENSE.TXT` and `libunwind/LICENSE.TXT` at that commit.
```text
Full text of the relevant licenses is included below.

==============================================================================

University of Illinois/NCSA
Open Source License

Copyright (c) 2009-2019 by the contributors listed in CREDITS.TXT

All rights reserved.

Developed by:

    LLVM Team

    University of Illinois at Urbana-Champaign

    http://llvm.org

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal with
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimers.

    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimers in the
      documentation and/or other materials provided with the distribution.

    * Neither the names of the LLVM Team, University of Illinois at
      Urbana-Champaign, nor the names of its contributors may be used to
      endorse or promote products derived from this Software without specific
      prior written permission.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS WITH THE
SOFTWARE.

==============================================================================

Copyright (c) 2009-2014 by the contributors listed in CREDITS.TXT

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

<!-- notice: libffi -->
libffi, built into JNA 5.13.0's `libjnidispatch.so`. Source: <https://raw.githubusercontent.com/java-native-access/jna/4962fd7758493b7395e86578705d8a32f6238872/native/libffi/LICENSE>, lines 1-21, commit `4962fd7758493b7395e86578705d8a32f6238872` (tag `5.13.0`).
```text
libffi - Copyright (c) 1996-2022  Anthony Green, Red Hat, Inc and others.
See source files for details.

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
``Software''), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED ``AS IS'', WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

<!-- notice: soundtouch -->
SoundTouch 2.4.1, built from source as `libsoundtouch.so` and `libsoundtouch_fp.so`: the notice in its files, which refers to the LGPL-2.1 text that the app contains. Source: `soundtouch/source/SoundTouch/SoundTouch.cpp` in <https://codeberg.org/soundtouch/soundtouch/archive/2.4.1.tar.gz> (archive SHA-256 `35d404e6e8c2ebd12fb4000da6fadd75c99e37eed2126a04721828c11c0377ec`), lines 46-61; the same file is `ndkLibs/soundtouch/source/SoundTouch/SoundTouch.cpp` in this repository, and every other SoundTouch file that the build compiles or includes has the same lines in its header.
```text
//  SoundTouch audio processing library
//  Copyright (c) Olli Parviainen
//
//  This library is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2.1 of the License, or (at your option) any later version.
//
//  This library is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  Lesser General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public
//  License along with this library; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
```

<!-- notice: moodycamel-header -->
readerwriterqueue and atomicops, by Cameron Desrochers, in both audio plugin libraries and `libmupen64plus-video-GLideN64.so`: the notice in the files. Source: `mupen64plus-audio-android/src/readerwriterqueue.h`, lines 1-3, at `9bcca07a6`; the same lines are in GLideN64's copy, `mupen64plus-video-gliden64/upstream/src/Graphics/OpenGLContext/ThreadedOpenGl/readerwriterqueue.h`, and, after a byte-order mark, in lines 1-3 of both copies of `atomicops.h`.
```text
// ©2013-2016 Cameron Desrochers.
// Distributed under the simplified BSD license (see the license file that
// should have come with this header).
```

<!-- notice: readerwriterqueue -->
readerwriterqueue and atomicops, by Cameron Desrochers: the license file that the notice above refers to. Source: `mupen64plus-video-gliden64/upstream/licenses/readerwriterqueue/LICENSE.md`, lines 1-28, at `9bcca07a6`.
```text
This license applies to all the code in this repository except that written by third
parties, namely the files in benchmarks/ext, which have their own licenses, and Jeff
Preshing's semaphore implementation (used in the blocking queue) which has a zlib
license (embedded in atomicops.h).

Simplified BSD License:

Copyright (c) 2013-2015, Cameron Desrochers  
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

- Redistributions of source code must retain the above copyright notice, this list of
conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright notice, this list of
conditions and the following disclaimer in the documentation and/or other materials
provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

<!-- notice: preshing-semaphore -->
Jeff Preshing's semaphore, embedded in `atomicops.h`, in both audio plugin libraries and `libmupen64plus-video-GLideN64.so`. Source: `mupen64plus-audio-android/src/atomicops.h`, lines 362-378, at `9bcca07a6`; the same lines are in GLideN64's copy, `mupen64plus-video-gliden64/upstream/src/Graphics/OpenGLContext/ThreadedOpenGl/atomicops.h`.
```text
	// Copyright (c) 2015 Jeff Preshing
	//
	// This software is provided 'as-is', without any express or implied
	// warranty. In no event will the authors be held liable for any damages
	// arising from the use of this software.
	//
	// Permission is granted to anyone to use this software for any purpose,
	// including commercial applications, and to alter it and redistribute it
	// freely, subject to the following restrictions:
	//
	// 1. The origin of this software must not be misrepresented; you must not
	//    claim that you wrote the original software. If you use this software
	//    in a product, an acknowledgement in the product documentation would be
	//    appreciated but is not required.
	// 2. Altered source versions must be plainly marked as such, and must not be
	//    misrepresented as being the original software.
	// 3. This notice may not be removed or altered from any source distribution.
```

<!-- notice: miniupnpc-2-3-3 -->
miniupnpc 2.3.3, `libminiupnp-build.so`. Source: `miniupnpc-2.3.3/LICENSE` in <https://miniupnp.tuxfamily.org/files/miniupnpc-2.3.3.tar.gz> (archive SHA-256 `d52a0afa614ad6c088cc9ddff1ae7d29c8c595ac5fdd321170a05f41e634bd1a`), lines 1-29; the same file is `ndkLibs/miniupnpc/LICENSE` in this repository.
```text
BSD 3-Clause License

Copyright (c) 2005-2025, Thomas BERNARD
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

<!-- notice: miniupnpc -->
miniupnpc of upstream commit `3a87be33` (2021, after version 2.2.2), the prebuilt `libminiupnp-build.so` in V339. Source: `ndkLibs/miniupnp/miniupnpc/LICENSE`, lines 1-27, at `9bcca07a6`.
```text
MiniUPnPc
Copyright (c) 2005-2020, Thomas BERNARD
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.
    * The name of the author may not be used to endorse or promote products
	  derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

```

<!-- notice: libnatpmp-134fc89e -->
libnatpmp at commit `134fc89e`, `libnatpmp.so`. Source: `LICENSE` in <https://codeload.github.com/miniupnp/libnatpmp/tar.gz/134fc89e2781e154e40042641f4d8bcbe42579f1>, the archive that GitHub makes of that commit (SHA-256 `be1864ed4e54a122590fd2de3824b085386b6e4aa9a91738c277c280ab8bf188` on 27 October 2026), lines 1-29; the same file is `ndkLibs/libnatpmp/LICENSE` in this repository.
```text
BSD 3-Clause License

Copyright (c) 2007-2022, Thomas BERNARD
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

<!-- notice: libnatpmp -->
libnatpmp of upstream commit `4536032a` (2020), the prebuilt `libnatpmp.so` in V339. Source: `ndkLibs/libnatpmp/LICENSE`, lines 1-26, at `9bcca07a6`.
```text
Copyright (c) 2007-2011, Thomas BERNARD
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.
    * The name of the author may not be used to endorse or promote products
	  derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

```

<!-- notice: sse2neon-cxd4 -->
SSE2NEON, the copy in the cxd4 RSP, `libmupen64plus-rsp-cxd4.so`. The file has no copyright line; its notice is its list of contributors and the MIT text, which has "MEMALLOC_FREE of charge" where the MIT license says "free of charge". Source: `mupen64plus-rsp-cxd4/upstream/sse2neon/SSE2NEON.h`, lines 27-53, at `9bcca07a6`.
```text
// Contributors to this project are:
//
// John W. Ratcliff : jratcliffscarab@gmail.com
// Brandon Rowlett : browlett@nvidia.com
// Ken Fast : kfast@gdeb.com
// yomoma : iterentiev@nvidia.com
//
//
/*
** The MIT license:
**
** Permission is hereby granted, MEMALLOC_FREE of charge, to any person obtaining a copy
** of this software and associated documentation files (the "Software"), to deal
** in the Software without restriction, including without limitation the rights
** to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
** copies of the Software, and to permit persons to whom the Software is furnished
** to do so, subject to the following conditions:
**
** The above copyright notice and this permission notice shall be included in all
** copies or substantial portions of the Software.

** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
** AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
** WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
** CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

<!-- notice: sse2neon-parallel-rsp -->
SSE2NEON, the copy in parallel-rsp, `libmupen64plus-rsp-parallel.so`. The file has no copyright line; its notice is its list of contributors and the MIT text. Source: `mupen64plus-rsp-parallel/upstream/arch/simd/rsp/SSE2NEON.h`, lines 9-39, at `9bcca07a6`.
```text
// Contributors to this work are:
//   John W. Ratcliff <jratcliffscarab@gmail.com>
//   Brandon Rowlett <browlett@nvidia.com>
//   Ken Fast <kfast@gdeb.com>
//   Eric van Beurden <evanbeurden@nvidia.com>
//   Alexander Potylitsin <apotylitsin@nvidia.com>
//   Hasindu Gamaarachchi <hasindu2008@gmail.com>
//   Jim Huang <jserv@biilabs.io>
//   Mark Cheng <marktwtn@biilabs.io>
//   Malcolm James MacLeod <malcolm@gulden.com>

/*
 * The MIT license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
```

<!-- notice: parallel-rsp -->
parallel-rsp, `libmupen64plus-rsp-parallel.so`, under the MIT option of its dual license. Source: `mupen64plus-rsp-parallel/upstream/LICENSE.MIT`, lines 1-20, at `9bcca07a6`.
```text
Copyright (c) 2020 Hans-Kristian Arntzen

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

<!-- notice: gles2n64-note -->
gles2n64, ported by yongzh, in `libmupen64plus-video-gln64.so`: the note at the top of its license file, which the LGPL-3.0 text below follows. Source: `mupen64plus-video-gln64/src/COPYING`, lines 1-7, at `9bcca07a6`.
```text
This directory contains the source code of gln64 ported to Android
by yongzh (freeman.yong@gmail.com). The original source code is available at:

http://code.google.com/p/gles2n64/



```

<!-- notice: lgpl-3-0 -->
GNU Lesser General Public License, version 3: the license of gles2n64 (in `libmupen64plus-video-gln64.so` and, in parts, `libmupen64plus-video-GLideN64.so`) and of GNU Lightning (linked into `libmupen64plus-rsp-parallel.so`). The app contains the GPL-3.0 text that this license builds on. Source: `mupen64plus-video-gliden64/upstream/licenses/gles2n64/LICENSE`, lines 1-165, at `9bcca07a6`; the same text is in `mupen64plus-video-gln64/src/COPYING`, lines 8-172. GNU Lightning's copy, `mupen64plus-rsp-parallel/upstream/lightning/COPYING.LESSER` (also `mupen64plus-rsp-parallel/upstream/LICENSE.LESSER`), differs only by a space at the end of lines 13 and 114.
```text
		   GNU LESSER GENERAL PUBLIC LICENSE
                       Version 3, 29 June 2007

 Copyright (C) 2007 Free Software Foundation, Inc. <http://fsf.org/>
 Everyone is permitted to copy and distribute verbatim copies
 of this license document, but changing it is not allowed.


  This version of the GNU Lesser General Public License incorporates
the terms and conditions of version 3 of the GNU General Public
License, supplemented by the additional permissions listed below.

  0. Additional Definitions.

  As used herein, "this License" refers to version 3 of the GNU Lesser
General Public License, and the "GNU GPL" refers to version 3 of the GNU
General Public License.

  "The Library" refers to a covered work governed by this License,
other than an Application or a Combined Work as defined below.

  An "Application" is any work that makes use of an interface provided
by the Library, but which is not otherwise based on the Library.
Defining a subclass of a class defined by the Library is deemed a mode
of using an interface provided by the Library.

  A "Combined Work" is a work produced by combining or linking an
Application with the Library.  The particular version of the Library
with which the Combined Work was made is also called the "Linked
Version".

  The "Minimal Corresponding Source" for a Combined Work means the
Corresponding Source for the Combined Work, excluding any source code
for portions of the Combined Work that, considered in isolation, are
based on the Application, and not on the Linked Version.

  The "Corresponding Application Code" for a Combined Work means the
object code and/or source code for the Application, including any data
and utility programs needed for reproducing the Combined Work from the
Application, but excluding the System Libraries of the Combined Work.

  1. Exception to Section 3 of the GNU GPL.

  You may convey a covered work under sections 3 and 4 of this License
without being bound by section 3 of the GNU GPL.

  2. Conveying Modified Versions.

  If you modify a copy of the Library, and, in your modifications, a
facility refers to a function or data to be supplied by an Application
that uses the facility (other than as an argument passed when the
facility is invoked), then you may convey a copy of the modified
version:

   a) under this License, provided that you make a good faith effort to
   ensure that, in the event an Application does not supply the
   function or data, the facility still operates, and performs
   whatever part of its purpose remains meaningful, or

   b) under the GNU GPL, with none of the additional permissions of
   this License applicable to that copy.

  3. Object Code Incorporating Material from Library Header Files.

  The object code form of an Application may incorporate material from
a header file that is part of the Library.  You may convey such object
code under terms of your choice, provided that, if the incorporated
material is not limited to numerical parameters, data structure
layouts and accessors, or small macros, inline functions and templates
(ten or fewer lines in length), you do both of the following:

   a) Give prominent notice with each copy of the object code that the
   Library is used in it and that the Library and its use are
   covered by this License.

   b) Accompany the object code with a copy of the GNU GPL and this license
   document.

  4. Combined Works.

  You may convey a Combined Work under terms of your choice that,
taken together, effectively do not restrict modification of the
portions of the Library contained in the Combined Work and reverse
engineering for debugging such modifications, if you also do each of
the following:

   a) Give prominent notice with each copy of the Combined Work that
   the Library is used in it and that the Library and its use are
   covered by this License.

   b) Accompany the Combined Work with a copy of the GNU GPL and this license
   document.

   c) For a Combined Work that displays copyright notices during
   execution, include the copyright notice for the Library among
   these notices, as well as a reference directing the user to the
   copies of the GNU GPL and this license document.

   d) Do one of the following:

       0) Convey the Minimal Corresponding Source under the terms of this
       License, and the Corresponding Application Code in a form
       suitable for, and under terms that permit, the user to
       recombine or relink the Application with a modified version of
       the Linked Version to produce a modified Combined Work, in the
       manner specified by section 6 of the GNU GPL for conveying
       Corresponding Source.

       1) Use a suitable shared library mechanism for linking with the
       Library.  A suitable mechanism is one that (a) uses at run time
       a copy of the Library already present on the user's computer
       system, and (b) will operate properly with a modified version
       of the Library that is interface-compatible with the Linked
       Version.

   e) Provide Installation Information, but only if you would otherwise
   be required to provide such information under section 6 of the
   GNU GPL, and only to the extent that such information is
   necessary to install and execute a modified version of the
   Combined Work produced by recombining or relinking the
   Application with a modified version of the Linked Version. (If
   you use option 4d0, the Installation Information must accompany
   the Minimal Corresponding Source and Corresponding Application
   Code. If you use option 4d1, you must provide the Installation
   Information in the manner specified by section 6 of the GNU GPL
   for conveying Corresponding Source.)

  5. Combined Libraries.

  You may place library facilities that are a work based on the
Library side by side in a single library together with other library
facilities that are not Applications and are not covered by this
License, and convey such a combined library under terms of your
choice, if you do both of the following:

   a) Accompany the combined library with a copy of the same work based
   on the Library, uncombined with any other library facilities,
   conveyed under the terms of this License.

   b) Give prominent notice with the combined library that part of it
   is a work based on the Library, and explaining where to find the
   accompanying uncombined form of the same work.

  6. Revised Versions of the GNU Lesser General Public License.

  The Free Software Foundation may publish revised and/or new versions
of the GNU Lesser General Public License from time to time. Such new
versions will be similar in spirit to the present version, but may
differ in detail to address new problems or concerns.

  Each version is given a distinguishing version number. If the
Library as you received it specifies that a certain numbered version
of the GNU Lesser General Public License "or any later version"
applies to it, you have the option of following the terms and
conditions either of that published version or of any later version
published by the Free Software Foundation. If the Library as you
received it does not specify a version number of the GNU Lesser
General Public License, you may choose any version of the GNU Lesser
General Public License ever published by the Free Software Foundation.

  If the Library as you received it specifies that a proxy can decide
whether future versions of the GNU Lesser General Public License shall
apply, that proxy's public statement of acceptance of any version is
permanent authorization for you to choose that version for the
Library.
```

<!-- notice: cen64 -->
Code from CEN64, in `libmupen64plus-rsp-parallel.so`. Its files refer to a `LICENSE` file that is not in this repository; this is CEN64's. Source: <https://raw.githubusercontent.com/n64dev/cen64/8b89df2fdca20eec2d735e2b3db80b0b8199879f/LICENSE>, lines 1-25, commit `8b89df2fdca20eec2d735e2b3db80b0b8199879f` (the last commit that changed it).
```text
Copyright (c) 2015, Tyler J. Stachecki
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the Tyler J. Stachecki nor the names of its
      contributors may be used to endorse or promote products derived from
      this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL TYLER J. STACHECKI BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

```

<!-- notice: glsl-fxaa -->
GLSL-FXAA, by Matt DesLauriers, in `libmupen64plus-video-GLideN64.so`; the `shaders/fxaa.glsl` shader also comes from it. Source: `mupen64plus-video-gliden64/upstream/licenses/GLSL-FXAA/LICENSE.md`, lines 1-21, at `9bcca07a6`.
```text
The MIT License (MIT)
Copyright (c) 2014 Matt DesLauriers

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
OR OTHER DEALINGS IN THE SOFTWARE.

```

<!-- notice: glow -->
Glow, by Nutty Software, whose license file is in GLideN64 (`libmupen64plus-video-GLideN64.so`); whether its code is compiled is unknown. Source: `mupen64plus-video-gliden64/upstream/licenses/Glow/LICENSE`, lines 1-25, at `9bcca07a6`.
```text
Nutty Software Open WebGL Framework

Copyright (C) 2012 Nathaniel Meyer
Nutty Software, http://www.nutty.ca
All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:
    1. The above copyright notice and this permission notice shall be included in all
       copies or substantial portions of the Software.
    2. Redistributions in binary or minimized form must reproduce the above copyright
       notice and this list of conditions in the documentation and/or other materials
       provided with the distribution.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

<!-- notice: chromium -->
Chromium. Three files in GLideN64 (`libmupen64plus-video-GLideN64.so`) say they are based on Chromium's `android_hardware_buffer_compat.h` but carry no license notice; this is Chromium's own license, as an assumption, at a recent commit; the Chromium version that the files came from is unknown. Source: <https://raw.githubusercontent.com/chromium/chromium/afc4ac1053d29b81b8359233b17fda9aef49e2de/LICENSE>, lines 1-27, commit `afc4ac1053d29b81b8359233b17fda9aef49e2de`.
```text
// Copyright 2015 The Chromium Authors
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//    * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//    * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//    * Neither the name of Google LLC nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

<!-- notice: bmg-image -->
BMG image code, by Michael S. (M. Scott) Heiman, in `libmupen64plus-video-rice.so`: the notice in `BMGImage.c`. Source: `mupen64plus-video-rice/upstream/src/liblinux/BMGImage.c`, lines 4-24, at `9bcca07a6`.
```text
// Copyright (C) 2001 Michael S. Heiman
//
// You may use the software for any purpose you see fit. You may modify
// it, incorporate it in a commercial application, use it for school,
// even turn it in as homework. You must keep the Copyright in the
// header and source files. This software is not in the "Public Domain".
// You may use this software at your own risk. I have made a reasonable
// effort to verify that this software works in the manner I expect it to;
// however,...
//
// THE MATERIAL EMBODIED ON THIS SOFTWARE IS PROVIDED TO YOU "AS-IS" AND
// WITHOUT WARRANTY OF ANY KIND, EXPRESS, IMPLIED OR OTHERWISE, INCLUDING
// WITHOUT LIMITATION, ANY WARRANTY OF MERCHANTABILITY OR FITNESS FOR A
// PARTICULAR PURPOSE. IN NO EVENT SHALL MICHAEL S. HEIMAN BE LIABLE TO
// YOU OR ANYONE ELSE FOR ANY DIRECT, SPECIAL, INCIDENTAL, INDIRECT OR
// CONSEQUENTIAL DAMAGES OF ANY KIND, OR ANY DAMAGES WHATSOEVER, INCLUDING
// WITHOUT LIMITATION, LOSS OF PROFIT, LOSS OF USE, SAVINGS OR REVENUE,
// OR THE CLAIMS OF THIRD PARTIES, WHETHER OR NOT MICHAEL S. HEIMAN HAS
// BEEN ADVISED OF THE POSSIBILITY OF SUCH LOSS, HOWEVER CAUSED AND ON
// ANY THEORY OF LIABILITY, ARISING OUT OF OR IN CONNECTION WITH THE
// POSSESSION, USE OR PERFORMANCE OF THIS SOFTWARE.
```

<!-- notice: bmg-utils -->
BMG image code, by M. Scott Heiman, in `libmupen64plus-video-rice.so`: the notice in `BMGUtils.c`, `bmp.c` and `pngrw.c`. Source: `mupen64plus-video-rice/upstream/src/liblinux/BMGUtils.c`, lines 4-25, at `9bcca07a6`; the same lines are in `bmp.c` and `pngrw.c` in that folder.
```text
//  Copyright (C) 2001 M. Scott Heiman
//  All Rights Reserved
//
// You may use the software for any purpose you see fit. You may modify
// it, incorporate it in a commercial application, use it for school,
// even turn it in as homework. You must keep the Copyright in the
// header and source files. This software is not in the "Public Domain".
// You may use this software at your own risk. I have made a reasonable
// effort to verify that this software works in the manner I expect it to;
// however,...
//
// THE MATERIAL EMBODIED ON THIS SOFTWARE IS PROVIDED TO YOU "AS-IS" AND
// WITHOUT WARRANTY OF ANY KIND, EXPRESS, IMPLIED OR OTHERWISE, INCLUDING
// WITHOUT LIMITATION, ANY WARRANTY OF MERCHANTABILITY OR FITNESS FOR A
// PARTICULAR PURPOSE. IN NO EVENT SHALL MICHAEL S. HEIMAN BE LIABLE TO
// YOU OR ANYONE ELSE FOR ANY DIRECT, SPECIAL, INCIDENTAL, INDIRECT OR
// CONSEQUENTIAL DAMAGES OF ANY KIND, OR ANY DAMAGES WHATSOEVER, INCLUDING
// WITHOUT LIMITATION, LOSS OF PROFIT, LOSS OF USE, SAVINGS OR REVENUE,
// OR THE CLAIMS OF THIRD PARTIES, WHETHER OR NOT MICHAEL S. HEIMAN HAS
// BEEN ADVISED OF THE POSSIBILITY OF SUCH LOSS, HOWEVER CAUSED AND ON
// ANY THEORY OF LIABILITY, ARISING OUT OF OR IN CONNECTION WITH THE
// POSSESSION, USE OR PERFORMANCE OF THIS SOFTWARE.
```

<!-- notice: mame -->
angrylion-rdp-plus, `libmupen64plus-video-angrylion-plus.so`: the MAME license file, which its `CREDITS.txt` names ("The code comes under MAME license."). Source: `mupen64plus-video-angrylion-plus/upstream/MAME License.txt`, lines 1-53, at `9bcca07a6`, converted from ISO-8859-1 to UTF-8 (the "®" in line 13).
```text
MAME Legal Information
License

Redistribution and use of the MAME code or any derivative works are permitted provided that the following conditions are met:

    * Redistributions may not be sold, nor may they be used in a commercial product or activity.
    * Redistributions that are modified from the original source must include the complete source code, including the source code for all components used by a binary built from the modified sources. However, as a special exception, the source code distributed need not include anything that is normally distributed (in either source or binary form) with the major components (compiler, kernel, and so on) of the operating system on which the executable runs, unless that component itself accompanies the executable.
    * Redistributions must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
Trademark

MAME® is a registered trademark of Nicola Salmoria. The "MAME" name and MAME logo may not be used without first obtaining permission of the trademark holder.
Copyright

The code in MAME is the work of hundreds of developers, each of whom owns the copyright to the code they wrote. There is no central copyright authority you can license the code from. The proper way to use the MAME source code is to examine it, using it to understand how the games worked, and then write your own emulation code. Sorry, there is no free lunch here.
Common Questions

Q. Can I include MAME with my product?
A. No. MAME is not licensed for commercial use. Using MAME as a "freebie" or including it at "no cost" with your product still constitutes commerical usage and is forbidden by the license.

Q. Can I sell my product with the MAME logo on it?
A. No. Putting the logo on your product makes it appear that the product is something officially endorsed by Nicola Salmoria, and constitutes trademark infringement.

Q. Can I use the MAME logo to advertise my product?
A. No. Using the logo in your advertising makes it appear that the product is something officially endorsed by Nicola Salmoria, and constitutes trademark infringement.

Q. Can I use the term "MAME" in the name of my software?
A. Generally, no, especially if it is something that is sold. However, if you are producing a free MAME-related piece of software, it is common that permission is granted. Send a query to double- check first, please.

Q. Can I put an arcade cabinet running MAME in a public location?
A. No. This this a commercial use of MAME and is prohibited by the license. Even if you don't charge money, putting a machine in a public location is "operating" an arcade machine and falls under commercial rules in most locations.

Q. Can my non-profit use MAME or an arcade cabinet running MAME to help raise money?
A. No, sorry. Even for the most worthwhile cause, this still is a commercial use of MAME and is prohibited by the license.

Q. How do I obtain a license to the MAME source code?
A. You can't. See the Copyright section above.

Q. Is it legal to download ROMs for a game when I own the PCB?
A. This is unclear and depends on where you live. In most cases you would need to obtain permission from the original manufacturer to do so.

Q. What about the free ROMs on the MAME site? Can I use those with my product?
A. Almost all of the free ROMs on the MAME site are licensed only for non- commercial use, and only for distribution from the MAME site. Just because they are available for "free" here does not grant further redistribution rights, nor does it allow you to treat them as "freebies" for commercial use.

Q. If I obtain a license from an original manufacturer to distribute the ROMs can I use MAME to run them?
A. Generally, no, because it constitutes a commercial use of MAME. However, we have in the past made a couple of exceptions for this particular case. We will not consider making any further exceptions without proof that such a license has already been obtained.

Q. Can I use a PC running MAME to replace a real arcade PCB?
A. In order to do this you would have to use a copy of the original ROMs, which would require obtaining permission from the original manufacturer. Once you had permission from them, if it was used for non-commercial purposes, then you would not technically be violating the MAME license. However we still do not explicitly give permission to use MAME in this way because of the possibility of the game being sold sometime later, which would constitute commercial use of MAME. If you sell your game later you must sell it without MAME included.

Q. Can I ask for donations for the work I did on my port of MAME to platform X?
A. No. You would be earning money from the MAME trademark and copyrights, and that would be a commercial use, which is prohibited by the license. It is our wish that MAME remain free.
```

<!-- notice: parallel-rdp -->
paraLLEl-RDP, `libmupen64plus-video-parallel.so`. Source: `mupen64plus-video-parallel/upstream/LICENSE`, lines 1-20, at `9bcca07a6`.
```text
Copyright (c) 2020 Themaister

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

<!-- notice: parallel-rdp-2021 -->
paraLLEl-RDP, `libmupen64plus-video-parallel.so`: the one compiled file whose notice has a later year. Source: `mupen64plus-video-parallel/upstream/parallel-rdp/rdp_dump_write.cpp`, lines 1-20, at `9bcca07a6`.
```text
/* Copyright (c) 2021 Themaister
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

<!-- notice: granite -->
Granite Vulkan back end, by Hans-Kristian Arntzen, in `libmupen64plus-video-parallel.so`. Source: `mupen64plus-video-parallel/upstream/vulkan/buffer.cpp`, lines 1-20, at `9bcca07a6`; the same lines head the 25 other files in `vulkan/` and `util/` that the plugin compiles.
```text
/* Copyright (c) 2017-2020 Hans-Kristian Arntzen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

<!-- notice: volk -->
volk, by Arseny Kapoulkine, in `libmupen64plus-video-parallel.so`. The file's header points to this notice at its end. Source: `mupen64plus-video-parallel/upstream/volk/volk.h`, lines 1443-1461, at `9bcca07a6`.
```text
 * Copyright (c) 2018-2019 Arseny Kapoulkine
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
```

<!-- notice: freetype-2-15-0 -->
FreeType 2.15.0, built from source as `libfreetype.so`: the FreeType License. This software is based in part on the work of the FreeType Team. Source: `freetype-2.15.0/docs/FTL.TXT` in <https://download.savannah.gnu.org/releases/freetype/freetype-2.15.0.tar.xz> (archive SHA-256 `93cd47eb719c325107f2d3cf087d06299e944c3b48a40b55537302304dea1dd5`), lines 1-169; the same file is `ndkLibs/freetype/docs/FTL.TXT` in this repository.
```text
                    The FreeType Project LICENSE
                    ----------------------------

                            2006-Jan-27

                    Copyright 1996-2002, 2006 by
          David Turner, Robert Wilhelm, and Werner Lemberg



Introduction
============

  The FreeType  Project is distributed in  several archive packages;
  some of them may contain, in addition to the FreeType font engine,
  various tools and  contributions which rely on, or  relate to, the
  FreeType Project.

  This  license applies  to all  files found  in such  packages, and
  which do not  fall under their own explicit  license.  The license
  affects  thus  the  FreeType   font  engine,  the  test  programs,
  documentation and makefiles, at the very least.

  This  license   was  inspired  by  the  BSD,   Artistic,  and  IJG
  (Independent JPEG  Group) licenses, which  all encourage inclusion
  and  use of  free  software in  commercial  and freeware  products
  alike.  As a consequence, its main points are that:

    o We don't promise that this software works. However, we will be
      interested in any kind of bug reports. (`as is' distribution)

    o You can  use this software for whatever you  want, in parts or
      full form, without having to pay us. (`royalty-free' usage)

    o You may not pretend that  you wrote this software.  If you use
      it, or  only parts of it,  in a program,  you must acknowledge
      somewhere  in  your  documentation  that  you  have  used  the
      FreeType code. (`credits')

  We  specifically  permit  and  encourage  the  inclusion  of  this
  software, with  or without modifications,  in commercial products.
  We  disclaim  all warranties  covering  The  FreeType Project  and
  assume no liability related to The FreeType Project.


  Finally,  many  people  asked  us  for  a  preferred  form  for  a
  credit/disclaimer to use in compliance with this license.  We thus
  encourage you to use the following text:

   """
    Portions of this software are copyright © <year> The FreeType
    Project (https://freetype.org).  All rights reserved.
   """

  Please replace <year> with the value from the FreeType version you
  actually use.


Legal Terms
===========

0. Definitions
--------------

  Throughout this license,  the terms `package', `FreeType Project',
  and  `FreeType  archive' refer  to  the  set  of files  originally
  distributed  by the  authors  (David Turner,  Robert Wilhelm,  and
  Werner Lemberg) as the `FreeType Project', be they named as alpha,
  beta or final release.

  `You' refers to  the licensee, or person using  the project, where
  `using' is a generic term including compiling the project's source
  code as  well as linking it  to form a  `program' or `executable'.
  This  program is  referred to  as  `a program  using the  FreeType
  engine'.

  This  license applies  to all  files distributed  in  the original
  FreeType  Project,   including  all  source   code,  binaries  and
  documentation,  unless  otherwise  stated   in  the  file  in  its
  original, unmodified form as  distributed in the original archive.
  If you are  unsure whether or not a particular  file is covered by
  this license, you must contact us to verify this.

  The FreeType  Project is copyright (C) 1996-2000  by David Turner,
  Robert Wilhelm, and Werner Lemberg.  All rights reserved except as
  specified below.

1. No Warranty
--------------

  THE FREETYPE PROJECT  IS PROVIDED `AS IS' WITHOUT  WARRANTY OF ANY
  KIND, EITHER  EXPRESS OR IMPLIED,  INCLUDING, BUT NOT  LIMITED TO,
  WARRANTIES  OF  MERCHANTABILITY   AND  FITNESS  FOR  A  PARTICULAR
  PURPOSE.  IN NO EVENT WILL ANY OF THE AUTHORS OR COPYRIGHT HOLDERS
  BE LIABLE  FOR ANY DAMAGES CAUSED  BY THE USE OR  THE INABILITY TO
  USE, OF THE FREETYPE PROJECT.

2. Redistribution
-----------------

  This  license  grants  a  worldwide, royalty-free,  perpetual  and
  irrevocable right  and license to use,  execute, perform, compile,
  display,  copy,   create  derivative  works   of,  distribute  and
  sublicense the  FreeType Project (in  both source and  object code
  forms)  and  derivative works  thereof  for  any  purpose; and  to
  authorize others  to exercise  some or all  of the  rights granted
  herein, subject to the following conditions:

    o Redistribution of  source code  must retain this  license file
      (`FTL.TXT') unaltered; any  additions, deletions or changes to
      the original  files must be clearly  indicated in accompanying
      documentation.   The  copyright   notices  of  the  unaltered,
      original  files must  be  preserved in  all  copies of  source
      files.

    o Redistribution in binary form must provide a  disclaimer  that
      states  that  the software is based in part of the work of the
      FreeType Team,  in  the  distribution  documentation.  We also
      encourage you to put an URL to the FreeType web page  in  your
      documentation, though this isn't mandatory.

  These conditions  apply to any  software derived from or  based on
  the FreeType Project,  not just the unmodified files.   If you use
  our work, you  must acknowledge us.  However, no  fee need be paid
  to us.

3. Advertising
--------------

  Neither the  FreeType authors and  contributors nor you  shall use
  the name of the  other for commercial, advertising, or promotional
  purposes without specific prior written permission.

  We suggest,  but do not require, that  you use one or  more of the
  following phrases to refer  to this software in your documentation
  or advertising  materials: `FreeType Project',  `FreeType Engine',
  `FreeType library', or `FreeType Distribution'.

  As  you have  not signed  this license,  you are  not  required to
  accept  it.   However,  as  the FreeType  Project  is  copyrighted
  material, only  this license, or  another one contracted  with the
  authors, grants you  the right to use, distribute,  and modify it.
  Therefore,  by  using,  distributing,  or modifying  the  FreeType
  Project, you indicate that you understand and accept all the terms
  of this license.

4. Contacts
-----------

  There are two mailing lists related to FreeType:

    o freetype@nongnu.org

      Discusses general use and applications of FreeType, as well as
      future and  wanted additions to the  library and distribution.
      If  you are looking  for support,  start in  this list  if you
      haven't found anything to help you in the documentation.

    o freetype-devel@nongnu.org

      Discusses bugs,  as well  as engine internals,  design issues,
      specific licenses, porting, etc.

  Our home page can be found at

    https://freetype.org


--- end of FTL.TXT ---
```

<!-- notice: freetype-fthash -->
FreeType 2.15.0's `src/base/fthash.c`, which the build compiles into `libfreetype.so` through `src/base/ftbase.c`. Source: `freetype-2.15.0/src/base/fthash.c` in <https://download.savannah.gnu.org/releases/freetype/freetype-2.15.0.tar.xz> (archive SHA-256 `93cd47eb719c325107f2d3cf087d06299e944c3b48a40b55537302304dea1dd5`), lines 10-30; the same file is `ndkLibs/freetype/src/base/fthash.c` in this repository.
```text
 * Copyright 2000 Computing Research Labs, New Mexico State University
 * Copyright 2001-2015
 *   Francesco Zappa Nardelli
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COMPUTING RESEARCH LAB OR NEW MEXICO STATE UNIVERSITY BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
 * OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

<!-- notice: freetype-zlib -->
zlib 1.3.1, FreeType's copy in `src/gzip`, which the build compiles into `libfreetype.so` for FreeType's gzip support. Source: `freetype-2.15.0/src/gzip/zlib.h` in <https://download.savannah.gnu.org/releases/freetype/freetype-2.15.0.tar.xz> (archive SHA-256 `93cd47eb719c325107f2d3cf087d06299e944c3b48a40b55537302304dea1dd5`), lines 4-23; the same file is `ndkLibs/freetype/src/gzip/zlib.h` in this repository.
```text
  Copyright (C) 1995-2024 Jean-loup Gailly and Mark Adler

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

  Jean-loup Gailly        Mark Adler
  jloup@gzip.org          madler@alumni.caltech.edu
```

<!-- notice: freetype -->
FreeType in V339's prebuilt `libfreetype.so`, Android's copy of FreeType 2.5.3 (see [Release V339](#release-v339)). Source: `ndkLibs/freetype/NOTICE`, lines 1-173, at `9bcca07a6`, converted from ISO-8859-1 to UTF-8 (the "©" in line 55).
```text
This software is based in part on the work of the FreeType Team.

----------------------

                    The FreeType Project LICENSE
                    ----------------------------

                            2006-Jan-27

                    Copyright 1996-2002, 2006 by
          David Turner, Robert Wilhelm, and Werner Lemberg



Introduction
============

  The FreeType  Project is distributed in  several archive packages;
  some of them may contain, in addition to the FreeType font engine,
  various tools and  contributions which rely on, or  relate to, the
  FreeType Project.

  This  license applies  to all  files found  in such  packages, and
  which do not  fall under their own explicit  license.  The license
  affects  thus  the  FreeType   font  engine,  the  test  programs,
  documentation and makefiles, at the very least.

  This  license   was  inspired  by  the  BSD,   Artistic,  and  IJG
  (Independent JPEG  Group) licenses, which  all encourage inclusion
  and  use of  free  software in  commercial  and freeware  products
  alike.  As a consequence, its main points are that:

    o We don't promise that this software works. However, we will be
      interested in any kind of bug reports. (`as is' distribution)

    o You can  use this software for whatever you  want, in parts or
      full form, without having to pay us. (`royalty-free' usage)

    o You may not pretend that  you wrote this software.  If you use
      it, or  only parts of it,  in a program,  you must acknowledge
      somewhere  in  your  documentation  that  you  have  used  the
      FreeType code. (`credits')

  We  specifically  permit  and  encourage  the  inclusion  of  this
  software, with  or without modifications,  in commercial products.
  We  disclaim  all warranties  covering  The  FreeType Project  and
  assume no liability related to The FreeType Project.


  Finally,  many  people  asked  us  for  a  preferred  form  for  a
  credit/disclaimer to use in compliance with this license.  We thus
  encourage you to use the following text:

   """  
    Portions of this software are copyright © <year> The FreeType
    Project (www.freetype.org).  All rights reserved.
   """

  Please replace <year> with the value from the FreeType version you
  actually use.


Legal Terms
===========

0. Definitions
--------------

  Throughout this license,  the terms `package', `FreeType Project',
  and  `FreeType  archive' refer  to  the  set  of files  originally
  distributed  by the  authors  (David Turner,  Robert Wilhelm,  and
  Werner Lemberg) as the `FreeType Project', be they named as alpha,
  beta or final release.

  `You' refers to  the licensee, or person using  the project, where
  `using' is a generic term including compiling the project's source
  code as  well as linking it  to form a  `program' or `executable'.
  This  program is  referred to  as  `a program  using the  FreeType
  engine'.

  This  license applies  to all  files distributed  in  the original
  FreeType  Project,   including  all  source   code,  binaries  and
  documentation,  unless  otherwise  stated   in  the  file  in  its
  original, unmodified form as  distributed in the original archive.
  If you are  unsure whether or not a particular  file is covered by
  this license, you must contact us to verify this.

  The FreeType  Project is copyright (C) 1996-2000  by David Turner,
  Robert Wilhelm, and Werner Lemberg.  All rights reserved except as
  specified below.

1. No Warranty
--------------

  THE FREETYPE PROJECT  IS PROVIDED `AS IS' WITHOUT  WARRANTY OF ANY
  KIND, EITHER  EXPRESS OR IMPLIED,  INCLUDING, BUT NOT  LIMITED TO,
  WARRANTIES  OF  MERCHANTABILITY   AND  FITNESS  FOR  A  PARTICULAR
  PURPOSE.  IN NO EVENT WILL ANY OF THE AUTHORS OR COPYRIGHT HOLDERS
  BE LIABLE  FOR ANY DAMAGES CAUSED  BY THE USE OR  THE INABILITY TO
  USE, OF THE FREETYPE PROJECT.

2. Redistribution
-----------------

  This  license  grants  a  worldwide, royalty-free,  perpetual  and
  irrevocable right  and license to use,  execute, perform, compile,
  display,  copy,   create  derivative  works   of,  distribute  and
  sublicense the  FreeType Project (in  both source and  object code
  forms)  and  derivative works  thereof  for  any  purpose; and  to
  authorize others  to exercise  some or all  of the  rights granted
  herein, subject to the following conditions:

    o Redistribution of  source code  must retain this  license file
      (`FTL.TXT') unaltered; any  additions, deletions or changes to
      the original  files must be clearly  indicated in accompanying
      documentation.   The  copyright   notices  of  the  unaltered,
      original  files must  be  preserved in  all  copies of  source
      files.

    o Redistribution in binary form must provide a  disclaimer  that
      states  that  the software is based in part of the work of the
      FreeType Team,  in  the  distribution  documentation.  We also
      encourage you to put an URL to the FreeType web page  in  your
      documentation, though this isn't mandatory.

  These conditions  apply to any  software derived from or  based on
  the FreeType Project,  not just the unmodified files.   If you use
  our work, you  must acknowledge us.  However, no  fee need be paid
  to us.

3. Advertising
--------------

  Neither the  FreeType authors and  contributors nor you  shall use
  the name of the  other for commercial, advertising, or promotional
  purposes without specific prior written permission.

  We suggest,  but do not require, that  you use one or  more of the
  following phrases to refer  to this software in your documentation
  or advertising  materials: `FreeType Project',  `FreeType Engine',
  `FreeType library', or `FreeType Distribution'.

  As  you have  not signed  this license,  you are  not  required to
  accept  it.   However,  as  the FreeType  Project  is  copyrighted
  material, only  this license, or  another one contracted  with the
  authors, grants you  the right to use, distribute,  and modify it.
  Therefore,  by  using,  distributing,  or modifying  the  FreeType
  Project, you indicate that you understand and accept all the terms
  of this license.

4. Contacts
-----------

  There are two mailing lists related to FreeType:

    o freetype@nongnu.org

      Discusses general use and applications of FreeType, as well as
      future and  wanted additions to the  library and distribution.
      If  you are looking  for support,  start in  this list  if you
      haven't found anything to help you in the documentation.

    o freetype-devel@nongnu.org

      Discusses bugs,  as well  as engine internals,  design issues,
      specific licenses, porting, etc.

  Our home page can be found at

    http://www.freetype.org


--- end of FTL.TXT ---
```

<!-- notice: fxaa-shader -->
The `shaders/fxaa.glsl` shader: Armin Ronacher's code that it is based on. Source: `app/src/main/assets/mupen64plus_data/shaders/fxaa.glsl`, lines 19-49, at `9bcca07a6`.
```text
Copyright (c) 2011 by Armin Ronacher.

Some rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

* Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above
copyright notice, this list of conditions and the following
disclaimer in the documentation and/or other materials provided
with the distribution.

* The names of the contributors may not be used to endorse or
promote products derived from this software without specific
prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

<!-- notice: scalefx-pass-0 -->
ScaleFX, by Sp00kyFox: the `shaders/scalefx-pass0.glsl` shader. Source: `app/src/main/assets/mupen64plus_data/shaders/scalefx-pass0.glsl`, lines 15-30, at `9bcca07a6`.
```text
Copyright (c) 2016 Sp00kyFox - ScaleFX@web.de
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

<!-- notice: scalefx-passes-1-4 -->
ScaleFX, by Sp00kyFox: the `shaders/scalefx-pass1.glsl` to `scalefx-pass4.glsl` shaders. Source: `app/src/main/assets/mupen64plus_data/shaders/scalefx-pass1.glsl`, lines 21-39, at `9bcca07a6`; the same lines are in `scalefx-pass2.glsl` (lines 21-39), `scalefx-pass3.glsl` (lines 22-40) and `scalefx-pass4.glsl` (lines 20-38).
```text
Copyright (c) 2016 Sp00kyFox - ScaleFX@web.de

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

<!-- notice: checker-framework-mit -->
Checker Framework `checker-compat-qual` 2.5.5. Its jar holds no license text; the Checker Framework's license file says that its annotations are under the MIT License and gives this text, with no copyright line. Source: <https://raw.githubusercontent.com/typetools/checker-framework/62602db32dbc0dd64b5aecc8c84671252a50e08b/LICENSE.txt>, lines 389-407, commit `62602db32dbc0dd64b5aecc8c84671252a50e08b` (tag `checker-framework-2.5.5`).
```text
MIT License:

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

<!-- notice: notice-jackson-core -->
Jackson core 2.9.6: its `NOTICE` file, which the build removes from the app. Source: `META-INF/NOTICE` in <https://repo1.maven.org/maven2/com/fasterxml/jackson/core/jackson-core/2.9.6/jackson-core-2.9.6.jar> (jar SHA-1 `4e393793c37c77e042ccc7be5a914ae39251b365`), lines 1-20.
```text
# Jackson JSON processor

Jackson is a high-performance, Free/Open Source JSON processing library.
It was originally written by Tatu Saloranta (tatu.saloranta@iki.fi), and has
been in development since 2007.
It is currently developed by a community of developers, as well as supported
commercially by FasterXML.com.

## Licensing

Jackson core and extension components may licensed under different licenses.
To find the details that apply to this artifact see the accompanying LICENSE file.
For more information, including possible other licensing options, contact
FasterXML.com (http://fasterxml.com).

## Credits

A list of contributors may be found from CREDITS file, which is included
in some artifacts (usually source distributions); but is always available
from the source code management (SCM) system project uses.
```

<!-- notice: notice-commons-codec -->
Apache Commons Codec 1.11: its `NOTICE.txt` file, which the build removes from the app; its Windows line endings appear here as Unix line endings. Source: `META-INF/NOTICE.txt` in <https://repo1.maven.org/maven2/commons-codec/commons-codec/1.11/commons-codec-1.11.jar> (jar SHA-1 `3acb4705652e16236558f0f4f2192cc33c3bd189`), lines 1-17.
```text
Apache Commons Codec
Copyright 2002-2017 The Apache Software Foundation

This product includes software developed at
The Apache Software Foundation (http://www.apache.org/).

src/test/org/apache/commons/codec/language/DoubleMetaphoneTest.java
contains test data from http://aspell.net/test/orig/batch0.tab.
Copyright (C) 2002 Kevin Atkinson (kevina@gnu.org)

===============================================================================

The content of package org.apache.commons.codec.language.bm has been translated
from the original php source code available at http://stevemorse.org/phoneticinfo.htm
with permission from the original authors.
Original source copyright:
Copyright (c) 2008 Alexander Beider & Stephen P. Morse.
```

<!-- notice: notice-commons-io -->
Apache Commons IO 1.3.2: its `NOTICE.txt` file, which the build removes from the app; its Windows line endings appear here as Unix line endings. Source: `META-INF/NOTICE.txt` in <https://repo1.maven.org/maven2/commons-io/commons-io/1.3.2/commons-io-1.3.2.jar> (jar SHA-1 `b6dde38349ba9bb5e6ea6320531eae969985dae5`), lines 1-6.
```text
Apache Jakarta Commons IO
Copyright 2001-2007 The Apache Software Foundation

This product includes software developed by
The Apache Software Foundation (http://www.apache.org/).

```

<!-- notice: notice-commons-logging -->
Apache Commons Logging 1.2: its `NOTICE.txt` file, which the build removes from the app. Source: `META-INF/NOTICE.txt` in <https://repo1.maven.org/maven2/commons-logging/commons-logging/1.2/commons-logging-1.2.jar> (jar SHA-1 `4bfc12adfe4842bf07b657f0369c4cb522955686`), lines 1-6.
```text
Apache Commons Logging
Copyright 2003-2014 The Apache Software Foundation

This product includes software developed at
The Apache Software Foundation (http://www.apache.org/).

```

<!-- notice: notice-commons-compress -->
Apache Commons Compress 1.20: its `NOTICE.txt` file, which the build removes from the app. Source: `META-INF/NOTICE.txt` in <https://repo1.maven.org/maven2/org/apache/commons/commons-compress/1.20/commons-compress-1.20.jar> (jar SHA-1 `b8df472b31e1f17c232d2ad78ceb1c84e00c641b`), lines 1-55.
```text
Apache Commons Compress
Copyright 2002-2020 The Apache Software Foundation

This product includes software developed at
The Apache Software Foundation (https://www.apache.org/).

---

The files in the package org.apache.commons.compress.archivers.sevenz
were derived from the LZMA SDK, version 9.20 (C/ and CPP/7zip/),
which has been placed in the public domain:

"LZMA SDK is placed in the public domain." (http://www.7-zip.org/sdk.html)

---

The test file lbzip2_32767.bz2 has been copied from libbzip2's source
repository:

This program, "bzip2", the associated library "libbzip2", and all
documentation, are copyright (C) 1996-2019 Julian R Seward.  All
rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

2. The origin of this software must not be misrepresented; you must 
   not claim that you wrote the original software.  If you use this 
   software in a product, an acknowledgment in the product 
   documentation would be appreciated but is not required.

3. Altered source versions must be plainly marked as such, and must
   not be misrepresented as being the original software.

4. The name of the author may not be used to endorse or promote 
   products derived from this software without specific prior written 
   permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Julian Seward, jseward@acm.org
```

<!-- notice: notice-commons-lang3 -->
Apache Commons Lang 3.13.0: its `NOTICE.txt` file, which the build removes from the app. Source: `META-INF/NOTICE.txt` in <https://repo1.maven.org/maven2/org/apache/commons/commons-lang3/3.13.0/commons-lang3-3.13.0.jar> (jar SHA-1 `b7263237aa89c1f99b327197c41d0669707a462e`), lines 1-5.
```text
Apache Commons Lang
Copyright 2001-2023 The Apache Software Foundation

This product includes software developed at
The Apache Software Foundation (https://www.apache.org/).
```

<!-- notice: notice-commons-text -->
Apache Commons Text 1.11.0: its `NOTICE.txt` file, which the build removes from the app. Source: `META-INF/NOTICE.txt` in <https://repo1.maven.org/maven2/org/apache/commons/commons-text/1.11.0/commons-text-1.11.0.jar> (jar SHA-1 `2bb044b7717ec2eccaf9ea7769c1509054b50e9a`), lines 1-5.
```text
Apache Commons Text
Copyright 2014-2023 The Apache Software Foundation

This product includes software developed at
The Apache Software Foundation (https://www.apache.org/).
```

<!-- notice: notice-httpclient -->
Apache HttpClient 4.5.14: its `NOTICE` file, which the build removes from the app. Source: `META-INF/NOTICE` in <https://repo1.maven.org/maven2/org/apache/httpcomponents/httpclient/4.5.14/httpclient-4.5.14.jar> (jar SHA-1 `1194890e6f56ec29177673f2f12d0b8e627dec98`), lines 1-8.
```text

Apache HttpClient
Copyright 1999-2022 The Apache Software Foundation

This product includes software developed at
The Apache Software Foundation (http://www.apache.org/).


```

<!-- notice: notice-httpcore -->
Apache HttpCore 4.4.16: its `NOTICE` file, which the build removes from the app. Source: `META-INF/NOTICE` in <https://repo1.maven.org/maven2/org/apache/httpcomponents/httpcore/4.4.16/httpcore-4.4.16.jar> (jar SHA-1 `51cf043c87253c9f58b539c9f7e44c8894223850`), lines 1-8.
```text

Apache HttpCore
Copyright 2005-2022 The Apache Software Foundation

This product includes software developed at
The Apache Software Foundation (http://www.apache.org/).


```

<!-- notice: public-suffix-list -->
Public Suffix List, `mozilla/public-suffix-list.txt`, which Apache HttpClient 4.5.14 puts in the app: its notice. Source: `mozilla/public-suffix-list.txt` in <https://repo1.maven.org/maven2/org/apache/httpcomponents/httpclient/4.5.14/httpclient-4.5.14.jar> (jar SHA-1 `1194890e6f56ec29177673f2f12d0b8e627dec98`), lines 1-3.
```text
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
```

## Release V339

V339 has the same Java libraries as the current code. It differs from the current code in
these ways that matter for this page:

- It also contains `libhidapi.so`, for all four ABIs. The app never uses it. It is SDL2's
  HIDAPI. HIDAPI lets you choose one of three licenses (GPL-3.0, a BSD-style license or the
  original HIDAPI license), and SDL's Android code is under the zlib license. Its source and
  license texts are not in the repository.
- Its core does not have the commits from upstream that this repository took on 23 and 24
  October 2026. So it has xxHash 0.8.1 instead of 0.8.3 (commit `74691ed2b`), and older
  minizip and MD5 code: the current code took minizip from zlib 1.3.1.2 (`672fbfd67`) and
  the MD5 code that CPython 2.7 uses (`825fc33c3`). The notices are the same: V339's xxHash
  notice (lines 4-29 of `mupen64plus-core/upstream/subprojects/xxhash/xxhash.h` at `V339`)
  is the text of the block above for xxHash 0.8.0, and its minizip and MD5 notices are the
  blocks above. It has no `skipset.h` or `ints.h`, so Mark Adler's two notices above do not
  apply to it.
- Its core and Rice contain libpng 1.6.34, from the prebuilt `libpng.a`, instead of libpng
  1.6.59. Its notice is the block above for libpng 1.6.34. The tag `V339` holds a libpng
  1.6.34 source tree in `ndkLibs/png/`.
- Its `libfreetype.so` is the prebuilt library of December 2021 instead of FreeType 2.15.0
  built from source: Android's copy of FreeType (commit `9c745321`, after version 2.5.3), with
  libpng 1.6.34 and FreeType's copy of zlib 1.1.4 inside, and with only the version 35
  TrueType interpreter. Its GLideN64 links the prebuilt `libpng.a` but takes its `png_*`
  functions from that `libfreetype.so`. Their notices are the blocks above for that FreeType
  and for libpng 1.6.34; zlib 1.1.4's is the block above for FreeType's copy of zlib, with
  "1995-2002" for "1995-2024" (lines 4-23 of `ndkLibs/freetype/src/gzip/zlib.h` at `V339`).
  The tag `V339` holds that FreeType source tree in `ndkLibs/freetype/`, but no record of how
  the library was built.
- Its `libSDL2_net.so`, `libminiupnp-build.so` and `libnatpmp.so` are the prebuilt libraries
  of December 2021 instead of SDL_net 2.4.0, miniupnpc 2.3.3 and libnatpmp `134fc89e`: SDL_net
  of upstream commit `01285e4a` (after version 2.0.1), miniupnpc of upstream commit `3a87be33`
  (after version 2.2.2) and libnatpmp of upstream commit `4536032a`. Their notices are the
  blocks above for those prebuilt libraries. The tag `V339` holds the source of that miniupnpc
  and libnatpmp (`ndkLibs/miniupnp/`, `ndkLibs/libnatpmp/`), but only the headers of that
  SDL_net (`ndkLibs/SDL_net/include/`).
- Its `libsoundtouch.so` and `libsoundtouch_fp.so` are SoundTouch 2.2, prebuilt in 2021,
  instead of SoundTouch 2.4.1 built from source. Their notice is the block above for
  SoundTouch 2.4.1, whose lines SoundTouch 2.2's files carry too. The tag `V339` holds the
  SoundTouch 2.2 source tree in `ndkLibs/soundtouch/`. For `armeabi-v7a`, these two also
  hold LLVM libunwind, which the copies built from source do not.
- Its `mupen64plus.ini` game database is an older version.
- It does not have the trophy icon, `ic_trophy.xml`, which was added on 21 October 2026.
