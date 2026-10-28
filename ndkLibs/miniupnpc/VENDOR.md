# Vendored miniupnpc

Upstream: https://github.com/miniupnp/miniupnp, folder `miniupnpc/` (home page
https://miniupnp.tuxfamily.org/)
Release: 2.3.3, May 27, 2025 (`Changelog.txt`; the archive is dated May 26, 2025)
Tag: `miniupnpc_2_3_3`, commit `bf4215a7574f88aa55859db9db00e3ae58cf42d6` (GitHub API, read for
the plan on October 27, 2026)
Archive: https://miniupnp.tuxfamily.org/files/miniupnpc-2.3.3.tar.gz, 141,281 bytes, SHA-256
`d52a0afa614ad6c088cc9ddff1ae7d29c8c595ac5fdd321170a05f41e634bd1a`
License: `BSD-3-Clause` (see `LICENSE`).

Vendored contents: the whole archive, 104 files, without its top folder `miniupnpc-2.3.3/`.
Nothing is left out, and no file is changed. This file is the only addition. This folder
replaces `ndkLibs/miniupnp/`, the 2021 copy of the whole miniupnp repository (upstream commit
`3a87be33`, after 2.2.2, through PPSSPP's fork), and `ndkLibs/miniupnp-build/`, PPSSPP's
`Android.mk` and `miniupnpcstrings.h`.

Line endings: `testdesc/new_LiveBox_desc.xml` has CRLF line endings in the archive, and the
repository's `.gitattributes` stores it with LF (`*.xml text`). The project files in `msvc/`
have LF line endings and are stored so, but `eol=crlf` gives the `*.sln`, `*.vcproj`,
`*.vcxproj` and `*.vcxproj.filters` files CRLF line endings when they are checked out. None is
built.

Configuration: `ndkLibs/config/miniupnpc/miniupnpcstrings.h`, outside this folder, is the
archive's `miniupnpcstrings.h.in` with `OS_STRING "Android"` and
`MINIUPNPC_VERSION_STRING "2.3.3"`, the two values that upstream's `updateminiupnpcstrings.sh`
sets, and UPnP 1.1. Routers see the User-Agent `Android UPnP/1.1 MiniUPnPc/2.3.3`; with
PPSSPP's header, the 2021 prebuilt sent `Android, UPnP/1.1, MiniUPnPc/2.1`. 2.3.3's
`src/minisoap.c` also needs `UPNP_VERSION_MAJOR` and `UPNP_VERSION_MINOR`, which PPSSPP's header
did not define.

Build: the shared library `miniupnp-build` (`libminiupnp-build.so`, the 2021 name) in
`miniupnp-bridge/CMakeLists.txt`, which links it into `libminiupnp-bridge.so`.
- Sources: the 14 library files that upstream's `CMakeLists.txt` lists, in `src/`:
  `igd_desc_parse.c miniupnpc.c minixml.c minisoap.c minissdpc.c miniwget.c upnpcommands.c
  upnpdev.c upnpreplyparse.c upnperrors.c connecthostport.c portlistingparse.c receivedata.c
  addr_is_reserved.c`. Not built: `src/minixmlvalid.c`, a test program that the 2021 prebuilt
  held (it exported its `main`), the other test and sample programs, and the Python and Java
  modules.
- Include paths: `ndkLibs/config/miniupnpc`, then `include`, which is exported to the bridge.
- Flags: those of the 2021 prebuilt (PPSSPP's `Android.mk`): `-O3 -fsigned-char
  -fno-strict-aliasing -Wall -Wno-multichar -D__STDC_CONSTANT_MACROS`, and ARM mode (`-marm`)
  on `armeabi-v7a`; otherwise the NDK's CMake flags for the Release type. The prebuilt's
  `-D_M_IX86` and `-D_M_X64` for `x86` and `x86_64` are left out: no compiled file uses them.
  Upstream's CMake also defines `MINIUPNPC_SET_SOCKET_TIMEOUT MINIUPNPC_GET_SRC_ADDR
  _BSD_SOURCE _DEFAULT_SOURCE _XOPEN_SOURCE=600`. The prebuilt had none of them, and this
  build keeps it so: as before, `src/connecthostport.c` and `src/minissdpc.c` set no socket
  timeouts, and `src/receivedata.c` does not read the source address of SSDP replies.
- Upstream's own CMake build is not used: it names the library `libminiupnpc` and gives it a
  `SOVERSION` numbered after the API.

API changes that the bridge follows: 2.2.3 moved the headers to `include/`, so
`miniupnp-bridge/src/PortManager.h` includes `"miniupnpc.h"` and the others without a folder,
and 2.2.8 added two parameters to `UPNP_GetValidIGD()`, for the router's public address
(`miniupnp-bridge/src/PortManager.cpp`, which never logs that address).

To update: check the new archive's SHA-256, replace this folder's content with the archive's
(keep this file), make `ndkLibs/config/miniupnpc/miniupnpcstrings.h` again from the new
`miniupnpcstrings.h.in`, compare the source list in `CMakeLists.txt` with
`miniupnp-bridge/CMakeLists.txt`, check the bridge's calls against `Changelog.txt`, and update
this file, `LICENSES.md` and, if the bridge's calls change, `tools/check-native-libs.sh`. Add
the new files by the archive's file list, not with `git add -A`, and check with
`git check-ignore` that none of them is ignored.
