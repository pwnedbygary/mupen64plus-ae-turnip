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
