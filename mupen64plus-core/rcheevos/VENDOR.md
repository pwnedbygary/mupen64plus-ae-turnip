# Vendored rcheevos

Upstream: https://github.com/RetroAchievements/rcheevos
Pinned tag: `v12.5.0`
Pinned commit: `1433173220a7eaede6a9ed7a18e94117be1821e0`
License: MIT (see `LICENSE`)

Vendored contents:
- `include/` — public headers
- `src/rc_client.c`, `src/rc_client_internal.h` — client
- `src/rcheevos/` — runtime
- `src/rapi/` — HTTP request helpers
- `src/rhash/` — hash generation (minus `md5.c`, whose `md5_*` symbols are
  provided by the mupen64plus core's `upstream/subprojects/md5/md5.c`)
- `src/rc_util.c`, `src/rc_compat.c`, `src/rc_version.c`

Not vendored: `rc_libretro.c`, `rc_client_external.c`,
`rc_client_raintegration.c` (not needed; the glue uses the plain
`rc_client_*` API).

`src/rhash/md5.h` declares `md5_append` with an `int` length, while the
core's `upstream/subprojects/md5/md5.h` has declared `unsigned int` since
upstream mupen64plus-core `316757d459`. `md5_state_t` is the same in both
headers, and the two types are passed the same way on all four Android ABIs,
so rcheevos' calls work with the core's function. The old `md5_append`
returned at once for a negative length; the core's now takes one as a length
of 2 to 4 GB, so no call may pass one. Keep the vendored header as rcheevos
has it.
