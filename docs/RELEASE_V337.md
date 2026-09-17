# v337 — Native DD investigation cleanup

This release removes the DD/RSP investigation tracing, snapshots, command-buffer
watches and diagnostic-generated CPU hooks from the working development build.
It retains explicit per-game DD policy, the existing DD CPU/DMA corrections and
independent WritableROM support. Normal error reporting remains available.

The confirmed English Expansion Kit configuration is:

- F-Zero X **USA cartridge**;
- the English-translated **USA-region NDD**;
- the **prototype USA IPL**;
- Dynamic Recompiler and Parallel RSP.

The working configuration was demonstrated on development commit `5ebdc720`.
That beta displayed version 336 but was **not** the published v336 tag
(`dc955483`). A subsequent user test of that tagged release showed the 3D N logo,
then “F-ZERO X cannot be played with this disk alone” and a request to insert the
Game Pak. The release source selects IPL-first when an IPL is loaded, whereas
the working beta supports explicit cartridge-first combo boot. That functional
boot selection is preserved here. The comparison does not isolate every other
intervening fix or establish that diagnostic logging made the game work.

The APK actually attached to the public v336 tag reports internal version code
335 and name `3.0.335 (beta) dc955483`. v337 aligns the tag with version code 337
and name `3.0.337` plus the build commit hash.

## Installation and saves

The release APK keeps the normal release application ID and signing identity.
It can update the earlier signed release without an uninstall. The diagnostic
beta uses a separate `.debug` package: this release does **not** update that beta
in place or automatically transfer its private saves. Keep the beta and its
saves until any desired transfer is backed up and verified.

No save-format changes are part of this release. Native disk save/reopen and
writable-cart persistence have not been re-tested on a device for this cleaned
binary. Preserve backups; an emulator autosave message alone does not verify
those features.