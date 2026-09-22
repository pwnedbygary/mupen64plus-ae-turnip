# v338 — RetroAchievements, gallery and theme fixes, new branding

This release collects everything merged since the published v337 tag plus the branding work:
RetroAchievements support with hardcore-mode enforcement, a set of gallery, theme and driver
fixes, and a complete replacement of the retired yellow and green cube artwork with the neon
"M" logo.

Version code **338**, version name **3.0.338** plus the build commit hash. Package
`org.mupen64plusae.turnip.pwnedbygary`, signed with the same release identity as v337, so it
updates an existing signed release in place; it does not replace the separate `.debug` beta or
transfer that beta's private saves.

## Wave Race compatibility update

This v338 update adds the verified Wave Race Shindou English translation to the
ROM database, inheriting the original Shindou defaults, including Count per Op
**3**. The bundled database revision is increased so existing installations
refresh it. Explicit per-game overrides, save identity, native N64DD policy,
and independent WritableROM behavior remain unchanged.

The user confirmed fresh Dynamic Recompiler gameplay with a manual Count per Op
setting of 3 using both GLideN64 and ParaLLEl. Host regression checks cover
lookup/inheritance and the database refresh; automatic-default behavior and
save/reopen have not yet been verified on a device. Keep existing saves and the
working manual override until automatic defaults are confirmed.

The original v338 source at `ded55f412a778f15d839786776e223935afe7d0b` is
preserved under `archive/v338-original-ded55f41`. This replacement retains
version code 338 and the release signing identity. Users who already installed
v338 must download and install the replacement manually; update checks based
only on version code may not offer it.

## RetroAchievements (rcheevos)

- Vendored rcheevos with native glue (`upstream/src/api/ra_glue.c`, `api_export.ver`) and a JNA bridge.
- New RetroAchievements settings screen and preferences.
- The client is started from the emulation lifecycle and uses the default HTTPS host.
- Sign-in with password plus session-token capture, with escaped-string JSON handling fixed.
- Hardcore mode enforced: save states, slots, GameShark and cheats are denied, the overwrite prompt
  is skipped when hardcore would deny the save, and fast-forward is blocked.
- Hardcore denials no longer announce a save or load that never happens.
- Cancel on confirmation dialogs no longer performs the action.
- Device verification recorded in `docs/RETROACHIEVEMENTS_REMAINING_ISSUES.md`.

## Gallery, orientation, theme and driver fixes

- Gallery and Game activities handle orientation changes in place, and the gallery keeps its
  draw-behind-system-bars behaviour across rotation.
- Stale service broadcasts after gallery recreation are fixed (issue #1).
- The theme quick-switch crash caused by stacked `activity.recreate()` calls is fixed.
- All AlertDialogs and popups are themed; submenu rows use the secondary accent; dynamic menu-row
  colours no longer go stale; a preset change re-colours text immediately.
- ROMs larger than 64 MB are accepted, fixing the B3313 new-save crash (issue #3).
- Per-game GPU driver labelling corrected, with a per-game "force stock driver" option.

## Branding

- The launcher icon is the neon "M" logo, and the glyph now fits entirely inside the launcher's
  circular mask. The previous asset was authored at the legacy 48 dp size, so the system upscaled it
  and the mask cut its corners. The adaptive background is a retrowave gradient and every layer is an
  oval, so the icon is round with transparent corners in every rendering context, masked or not.
- Legacy `ic_launcher.png` and `ic_launcher_round.png` regenerated as round tiles for pre-API-26
  launchers and the `android:roundIcon` consumer.
- The splash screen shows the new logo on a matching neon backdrop; the retired `publisherlogo`
  nine-patch is gone.
- The retired cube is gone from the notification icon (`drawable-*/icon.png`), the Android TV banner
  and both web listing icons.
- Regeneration is reproducible through three scripts: `tools/generate-launcher-icon.py`,
  `tools/generate-launcher-legacy-icons.py` and `tools/generate-brand-assets.py`.

## Device verification

The signed release build was installed on a Retroid Pocket 6 running Android 13. Verified on device:
install and launch without crashes; a game ran with the emulator's foreground notification; the
launcher icon renders as a circle fully inside the mask (the glyph measured 98 px across inside the
148 px mask circle, against 97.6 px predicted for a masked draw); the splash screen and the first-install permission dialog show the new
logo; and the notification small icon matches the new logo (silhouette match 0.971 against 0.761 for
the retired cube).

Not re-tested on this build: N64DD and Expansion Kit behaviour, custom-machine saving, and the
writable-cart `.cart_ram` persistence path. v337 acceptance does not carry over, because this release
changed the cartridge-ROM paths those features use: `CART_ROM_ADDR_MASK` moved from 0x03ffffff to
0x0fffffff, `CART_ROM_MAX_SIZE` rose from 0x4000000 to 0x10000000, an oversized-ROM rejection was
added, and the `mem_base` layout shifted accordingly. N64DD and WritableROM therefore need their own
re-test before v338 can be described as v337-equivalent for disk and writable-cart use. The Android TV
banner and the pre-API-26 legacy icons were not exercised on a device.
