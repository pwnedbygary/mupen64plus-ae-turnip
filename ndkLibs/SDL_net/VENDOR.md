# Vendored SDL_net

Upstream: https://github.com/libsdl-org/SDL_net
Release: 2.4.0, May 15, 2026 (GitHub release `release-2.4.0`)
Tag: `release-2.4.0`, commit `904600c6133e0435d627ec1878bfdfeac414a899` (the archive's
`.git-hash`; `REVISION.txt` says `release-2.4.0-0-g904600c`)
Archive: https://github.com/libsdl-org/SDL_net/releases/download/release-2.4.0/SDL2_net-2.4.0.tar.gz,
8,691,464 bytes, SHA-256 `9cbca2527feb3f1a622d48ba65cc7dee9b1e3f2c55ceafb7d7720bb058aafb30` (the
digest that GitHub publishes for the release file)
License: `Zlib` (see `LICENSE.txt`). The file headers have the same terms; those of
`include/SDL_net.h` and `src/SDLnet.c` also name Simeon Maxein.

Vendored contents: the archive without its top folder `SDL2_net-2.4.0/`, and without its
folders `Xcode/` (220 files and 4 symbolic links, 21.3 MB: Apple project files and SDL2
frameworks) and `VisualC/` (9 Visual Studio files), which this build does not use (Gary's
decision of October 27, 2026). That leaves 72 files. No file is changed. This file is the
only addition. The archive's `AGENTS.md` and `CLAUDE.md` state upstream's policy for
contributions to SDL_net; they are kept with the rest of the tree.

Line endings: `CHANGES.txt`, `README.txt` and `src/version.rc` have CRLF line endings in the
archive, and the repository's `.gitattributes` stores them with LF (`*.txt text`, `*.rc text`).
They differ from the archive in their line endings only. None is built.

Configuration: in `ndkLibs/config/SDL_net/`, outside this folder.
- `begin_code.h` and `close_code.h` are SDL 2.0.9's (SHA-256
  `250ef5cd3c00a77133236d9a04fdf0f3461811cc4d05117cff40e4a8edfda03a` and
  `a05d08d1d7100ef2dcb84220ab1f701108aea8747928ba2ad7384bfc02b981e8`), the SDL headers that the
  2021 prebuilt was built with (in the mupen64plus-ae-deps project). `include/SDL_net.h` includes
  `begin_code.h` and needs only `DECLSPEC`, `SDLCALL` and `SDL_FORCE_INLINE` from it.
  `close_code.h` is the same file as `ndkLibs/SDL2_stub/close_code.h`; the stub's
  `begin_code.h` also includes the app's stub `SDL.h`, so this build does not use it.
- `SDL_net_libc.h` is this app's own file, and SDL_net's only local change. The build defines
  `WITHOUT_SDL`, but SDL_net's sources still call six SDL functions (`SDL_malloc`,
  `SDL_realloc`, `SDL_free`, `SDL_memset`, `SDL_memcpy`, `SDL_vsnprintf`; `SDL_realloc` only in
  the code for Windows, `src/SDLnet.c:273`). The build includes this header before each source
  file (`-include`), and it maps them to the C library. The 2021 prebuilt had the same mapping
  as 23 lines added to its copy of `SDL_net.h`.

Build: the shared module `SDL2_net` (`libSDL2_net.so`) in `mupen64plus-core/Android.mk`. Only
the core links it.
- Sources: `src/SDLnet.c src/SDLnetTCP.c src/SDLnetUDP.c src/SDLnetselect.c`, the list in
  upstream's `Android.mk` and `CMakeLists.txt`. Not built: `examples/`.
- Include paths: `include` and `src` (as in upstream's `Android.mk`), and
  `ndkLibs/config/SDL_net`. Only `include` is exported to the core, which keeps compiling
  against the stub's `begin_code.h`, as before.
- Flags: `-DWITHOUT_SDL=1` and the `-include` above; otherwise ndk-build's defaults (Thumb on
  `armeabi-v7a`), as for the 2021 prebuilt, and not the common flags of
  `build_common/native_common.mk`.
- The 2021 prebuilt (upstream commit `01285e4a`, after 2.0.1) exported 36 functions. 2.4.0
  exports them all and six more, 42 in all: `SDLNet_TCP_OpenServer` and `SDLNet_TCP_OpenClient`,
  and `SDLNet_Read16`, `SDLNet_Read32`, `SDLNet_Write16` and `SDLNet_Write32`, which
  `src/SDLnet.c` defines "for binary compatibility" beside the macros of the same names in
  `include/SDL_net.h`. The 15 functions that the core calls are declared as before.

To update: check the new archive's SHA-256 against the digest that GitHub publishes for the
release file, replace this folder's content with the archive's, without `Xcode/` and
`VisualC/` (keep this file), check which SDL functions `src/` calls under `WITHOUT_SDL` and
update `SDL_net_libc.h`, compare the source list with upstream's `Android.mk`, and update this
file and `LICENSES.md`. Add the new files by the archive's file list, not with `git add -A`,
and check with `git check-ignore` that none of them is ignored.
