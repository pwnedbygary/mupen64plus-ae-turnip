# Vendored libnatpmp

Upstream: https://github.com/miniupnp/libnatpmp (home page
http://miniupnp.free.fr/libnatpmp.html)
Version: commit `134fc89e2781e154e40042641f4d8bcbe42579f1` on `master`, April 3, 2025 (Gary's
choice of October 27, 2026, over the newest release). It is the release 20230423 plus one fix
at run time (`6367fc3a`: a reply whose type differs from the pending request's gives
`NATPMP_TRYAGAIN`, so a late reply of another type is no longer taken as the awaited one),
build-file changes and an OpenBSD fix. The `VERSION` file still says 20120821: upstream does
not update it.
Archive: https://codeload.github.com/miniupnp/libnatpmp/tar.gz/134fc89e2781e154e40042641f4d8bcbe42579f1,
which GitHub generates from the commit, so its bytes can change. Downloaded on October 27,
2026: 28,296 bytes, SHA-256 `be1864ed4e54a122590fd2de3824b085386b6e4aa9a91738c277c280ab8bf188`,
the same as the download for the plan that day. Its pax header names the commit, and the git
tree of its files is `ce922152446a8d317c480db90a01830133d0ac0f`, the commit's tree (GitHub
API, read for the plan that day), so the files are the commit's.
License: `BSD-3-Clause` (see `LICENSE`).

Vendored contents: the whole archive, 32 files, without its top folder. Nothing is left out,
and no file is changed. This file is the only addition. The archive's own `.gitignore`
applies inside this folder; it ignores none of the archive's files.

Line endings: the project files in `msvc/` (`libnatpmp.sln`, `libnatpmp.vcproj`,
`natpmpc-static.vcproj`) have LF line endings in the archive and are stored so, but the
repository's `.gitattributes` (`eol=crlf`) gives them CRLF line endings when they are checked
out. None is built.

Build: the shared library `natpmp` (`libnatpmp.so`, the 2021 name) in
`miniupnp-bridge/CMakeLists.txt`, which links it into `libminiupnp-bridge.so`.
- Sources: `natpmp.c getgateway.c`, the list in upstream's `CMakeLists.txt` for systems other
  than Windows. Not built: `natpmpc.c`, the command-line client, which the 2021 prebuilt held
  (it exported its `main` and `usage`), `testgetgateway.c`, and the Python, Java and JNI
  modules.
- Flags: those of `ndkLibs/miniupnpc` (see its `VENDOR.md`), as for the 2021 prebuilt. No
  `ENABLE_STRNATPMPERR`, also as for the prebuilt, so `strnatpmperr()` is not built (upstream's
  CMake defines it and builds a static library).
- The bridge includes `<libnatpmp/natpmp.h>`, with `ndkLibs` on its include path.

To update: download the archive of the new commit or release, check that its files are the
commit's (the git tree of its files, or each file's blob hash, against the commit's tree),
replace this folder's content (keep this file), compare the source list in `CMakeLists.txt`
with `miniupnp-bridge/CMakeLists.txt`, and update this file and `LICENSES.md`. Add the new
files by the archive's file list, not with `git add -A`, and check with `git check-ignore`
that none of them is ignored.
