# v340 — RetroAchievements, core and netplay fixes, privacy, libraries built from source

This release collects the 36 pull requests merged since V339 (#19 to #54): achievements that read
the game's memory unlock again, and hardcore mode follows more of RetroAchievements' rules; the
emulator core takes upstream mupen64plus-core's fixes; netplay codes no longer need a server; the
app logs less, asks for fewer permissions and has a privacy policy and a licenses page; and the
native libraries that were committed in 2021 without a build record are now built from source.

Version code **340**, version name **3.0.340** plus the build commit hash. Package
`org.mupen64plusae.turnip.pwnedbygary`, signed with the same release identity as the earlier
signed releases, so it updates an existing signed release in place; it does not replace the
separate `.debug` beta or transfer that beta's private saves.

## Update check

The in-app update check compares the number in the latest release's tag with the app's version
code. V339 (15 October 2026) was built with version code 338, so V339, and every other build with
version code 338, kept offering V339 as an update after it was installed. Version code 340 fixes
this: with v340 installed, the check offers only a release whose tag has a higher number, and
earlier builds are offered v340.

## RetroAchievements

- **Achievements that read memory unlock again** (#19). A defect disabled every achievement that
  reads the game's memory as soon as the game loaded, in hardcore and in casual mode.
- **Hardcore unlocks are still recorded as casual.** Going by its code, RetroAchievements' server
  keeps hardcore unlocks only from emulators that RetroAchievements has approved. It has not
  approved this app, so hardcore unlocks are recorded as casual (not yet seen on a device), and in
  hardcore the server's "Warning: Unknown Emulator" notice appears. The app now names itself to
  RetroAchievements as `Mupen64Turnip/<version> (Android <release>)` instead of the parent
  emulator's name (#19).
- **Hardcore rules** (#19): fast-forward, slow motion, frame advance and audio sync are disabled;
  save states can be made but not loaded; cheats and GameShark are refused; a game resumed from
  its auto save and netplay sessions run in casual mode, each with a notice.
- **No built-in game fixes in hardcore** (#49): the emulator's ROM database applies codes to three
  games in every mode: an end-credits fix for the GameCube dumps of Ocarina of Time, a multiplayer
  timing fix for Mario Kart 64 and a boot fix for Rat Attack (53 database entries list or inherit
  them). Hardcore sessions now run without them, with the notice "Hardcore mode: this game's
  compatibility fixes are disabled", and the Hardcore mode setting says that Rat Attack may not
  start. Casual, resumed and netplay sessions keep them.
- **During play** (#19): notices for the session's mode, each unlock, an unlock or leaderboard
  entry that the server refuses, a lost and a restored connection, and each leaderboard attempt
  (started, failed, or submitted with its value). A line at the top of the screen shows active
  challenges, achievement progress and running leaderboard values, and lets touches through to the
  controls below it.
- **Achievement list** (#19): the in-game menu's new "Achievements" item, shown when
  RetroAchievements is on, lists the mode, the unlocked count and points, then each achievement
  with its description, points and status. Every item of the in-game menu now has an icon, so
  their titles line up.
- **Unlocks made offline are kept** (#19): each unlock and leaderboard entry stays on the device
  until the server records or refuses it, and is sent again after the next sign-in, with a notice
  of how many the server recorded.
- **Progress kept with each save state** (#19): loading a state restores the achievement progress
  saved with it.
- **Sign-in** (#19, #37): a game signs in with the saved session token first, and the app deletes
  the saved password once a sign-in returns a token. If RetroAchievements later rejects the token,
  the app says "the saved sign-in was rejected; enter your password in Settings again".
- **Float conditions** (#38): conditions that compare a floating-point value in the game's memory
  with a constant no longer treat a NaN as if it were the constant. Derived from tests, not seen
  in a game.

Checked with debug builds on a Retroid Pocket 6 (Android 13): casual sessions with the mode notice
and the achievement list; "Match Winner" unlocked in Mario Tennis in casual mode, and the server
recorded it; in hardcore, the speed toggle, the Game speed dialog, audio sync, slot loads and
auto-save loads refused with their notices, and slot saves and the exit's auto save made with
their progress; Resume in casual mode; a reset while paused; the menu icons; and the password
deleted after a game's sign-in (#19, #37). Not run on a device: a hardcore unlock, an unlock made
offline, the line at the top of the screen, leaderboard attempts, netplay, and #49.

## Emulator core

- **Upstream mupen64plus-core** (#21, #25): the core followed a fork of mupen64plus-core that
  stopped taking upstream's changes in 2022. It now has 28 of upstream's bug and safety fixes
  (#21) and 125 more of upstream's commits, up to upstream's master of 19 October 2026 (#25):
  bounds checks in the emulated hardware and in ROM loading, dynamic recompiler and interpreter
  fixes, the interpreters' newer CPU accuracy, memory and register fixes, upstream's netplay code
  and an updated ROM database. The core keeps its own save file names, RetroAchievements, N64DD
  work, writable cartridge ROM, 256 MB ROM limit and 16 MB of RDRAM.
- **What can change for you** (#21, #25):
  - Save states are written in version 1.9; 1.8 states still load. Read from the code, not
    tried: an older build would load a 1.9 state without its two new values.
  - Every netplay player needs v340 (see "Netplay").
  - The bundled ROM database is new, and installed apps extract it at their next start.
  - The game's process uses about 32 MiB more memory from the start, measured with debug builds
    on an arm64 phone and on the Retroid Pocket 6.
  - The dynamic recompiler now compiles code that a game runs from uncached memory, where it used
    to end the process.
  - A save state made by an earlier version with a Transfer Pak attached loads two old Transfer
    Pak values in the place of two new ones.
- **Memory safety** (#29): the gaps that upstream's master also has are closed. SRAM and FlashRAM
  reads, writes and transfers, and transfers into a writable cartridge ROM, stay inside their
  memory, a damaged save state can no longer make the core write past its buffers, and the x86
  recompiler gets three guards that the other recompilers already had.
- **Fast math off for the core** (#38): the core is compiled without fast math, and two
  floating-point compare helpers are fixed, so the interpreters' compares give the right result
  when one side is a NaN. The interpreters run when an emulation profile picks them, and for games
  whose GLideN64 settings name `DisableFBInfo`, such as Pokémon Snap and Jet Force Gemini. Mario
  Tennis on the dynamic recompiler used about the same CPU time at 59.8 fps on the Retroid Pocket
  6 (debug builds); the interpreters' speed was not measured.
- **Saves and save states** (#19, #29, #34, #35):
  - A save state whose file cannot be written (full storage, a missing folder) no longer freezes
    the game. It, or a state whose file fails to close, is reported as failed, so Resume does not
    pick a cut-short auto save (#19, #29).
  - Controller Pak saves of players 2 to 4 are no longer replaced by player 1's data (#19).
  - A Transfer Pak's Game Boy save keeps what other Game Boy emulators append after its RAM, such
    as a clock (#19).
  - Project64 save states with an RDRAM size other than 4 or 8 MB, and M64P save states whose
    event list has no end, are refused instead of making the core read past its buffers (#19).
  - Save-state fields are copied out in a way that is defined on every processor; no change in
    behavior is expected (#34).
  - 32-bit builds (armeabi-v7a and x86) can open Project64 zip save states, which failed before
    (#35; shown under qemu).

Checked with debug builds: on an arm64 phone (Android 16), Mario Tennis resumed the previous
build's auto save (#21); on a Retroid Pocket 6 (Android 13), Mario Tennis loaded a 1.8 auto save,
saved and loaded a 1.9 slot state and resumed the exit's auto save (#25, #29), and F-Zero X with
its Expansion Kit disk reached the title screen with the 64DD logo and the attract race (#21,
#25). The game did not write to its disk in those runs. Not run on a device: the interpreters,
netplay, the Transfer Pak and Controller Paks, Project64 states, a game writing to its 64DD disk,
the 32-bit and x86 recompilers, #34 and #35.

## Crash and hang fixes

- **Game start on 32-bit ARM and x86 devices** (#20): every game start crashed on devices that use
  the app's armeabi-v7a, x86 or x86_64 libraries, such as 32-bit ARM TV boxes, Chromebooks with
  Intel or AMD processors and the Android emulator. Games start there again, and the Vulkan
  renderer uses the system's driver; a Turnip driver picked in Settings is not used on those
  devices. Shown on an Android 10 x86_64 emulator; not run on a real 32-bit ARM or x86 device.
- **Exit auto save** (#22): an exit through the in-game menu could hold its auto save back for
  most of a minute (45 seconds on a phone), and past 60 seconds the app's watchdog would have
  ended the process before the save was written. On the Retroid Pocket 6, in exits timed to hit
  the stall, the old code's saves landed up to 34.9 seconds after the request and the new code's
  about 0.11 seconds after it (debug builds).
- **Failed save-state writes** no longer freeze the game (#19; see "Emulator core").
- An unexpected error during a RetroAchievements request no longer ends the process (#19).
- On Android 6 to 10, the GPU driver benchmark's result notice and the hardcore cheats notice now
  show; before, they never did (#19; the benchmark notice was checked on an Android 10 emulator).
- Netplay crashes: see "Netplay" (#24, #30, #33).

## Optional hardware features

- Wi-Fi, "fake touch" input, sensors and a portrait screen are now declared as optional hardware,
  as in fzurita's FZ Edition (#23). Stores that filter apps by hardware, such as Google Play, used
  to treat Wi-Fi as required; devices without Wi-Fi, such as a TV on Ethernet only, now pass that
  filter. Android does not check these declarations when you install the APK yourself. Checked in
  the built APK; not installed on a device.
- On a device without Wi-Fi (Android 9 and later), hosting or joining a netplay room no longer
  crashes (#24). Not run on a device.

## Netplay

- **Codes without a server** (#45): online netplay no longer uses the server at `np.zurita.me`.
  **Get Code** still opens the router's ports; the app then asks the router for its public IPv4
  address and shows a 12-character code (`XXXX-XXXX-XXXX`) that holds the address and the room's
  port, with the address and port below it. The joining app reads the code, or an `address:port`
  typed instead, on the device and connects directly, as **Enter IP** does. A code with one wrong
  character or two swapped neighbors, or an old numeric code, is refused.
- **Limits of the new codes** (#45): with automatic port mapping off, the host gets no code and is
  asked to share the public address and port; only IPv4 is supported, and a router without a
  public IPv4 address (carrier-grade NAT, or a second router) gets a message that this network
  cannot host online games; and as no server checks the room anymore, a code can be shown for a
  room that nobody can reach.
- **Every player needs v340** (#25, #28): the core's netplay is now upstream's, so it no longer
  works with earlier builds of this app or with the official Mupen64Plus-AE app. This app's rooms
  announce their own version, 1001, so a player whose build does not match gets "Server netplay
  version doesn't match client!" instead of a game that waits at its start.
- **Rooms** (#27, #30, #32, #33): Cancel in the host's room dialog closes the room at once, and a
  stopped room is no longer advertised for up to 2 more seconds; a failed room registration no
  longer ends the emulation process; a joining player stops looking for rooms once the game starts
  or the player cancels, and no longer acts on a room's start signal after leaving it; picking a
  room in the join list joins that room even when two rooms appear at once; the room dialogs keep
  their room when Android recreates the game screen, for example after a font size, dark mode or
  language change; and lists and fields that several threads share are now locked or made
  volatile, which removes crashes that a room or a player arriving at the wrong moment could cause.
- A controller button mapped to frame advance no longer pauses a netplay game (#19; read from the
  code).

Checked on a Retroid Pocket 6 with debug builds: Cancel in the host's and the joining player's
dialogs (#27), both dialogs across two font size changes each (#32), and hosting and joining rooms
four times without a crash, which does not exercise the threading fixes (#33). Not run on a
device: a netplay session between two devices, the new codes (#45), a room of another version
(#28), a device without Wi-Fi (#24) and the registration and join-list fixes (#30).

## Privacy, permissions and licenses

- **Privacy policy and licenses page** (#43, #44): the
  [privacy policy](https://github.com/pwnedbygary/mupen64plus-ae-turnip/blob/master/PRIVACY.md)
  says what the app sends, to whom and when, what stays on your device and how to delete it; the
  [licenses page](https://github.com/pwnedbygary/mupen64plus-ae-turnip/blob/master/LICENSES.md)
  lists the app's license and each third-party library, data file and piece of art in the app,
  with its license and notice. The gallery drawer's About menu has "Privacy policy" and "Licenses"
  items that open them in the browser.
- **Cover art over HTTPS** (#46): cover art now comes over HTTPS from
  [a repository](https://github.com/pwnedbygary/mupen64plus-ae-turnip-coverart) under the
  maintainer's account, with 491 box-art pictures copied from libretro's Nintendo 64 thumbnails,
  instead of over plain HTTP from `coverart.zurita.me`, a server that this project does not
  control. Android before 7.1.1 most likely cannot download it, 363 of the 854 cover names in the
  ROM database have no picture there, and GitHub may limit downloads for a large library.
- **Less in the log** (#40): the log that you can share in a bug report no longer holds the Google
  account's e-mail address, IP addresses from netplay or the netplay host's device name.
- **Fewer permissions** (#36, #41): the app no longer asks for the advertising-ID and Google Play
  licensing permissions, whose code was already gone, nor for `ACCESS_WIFI_STATE` and
  `BLUETOOTH_CONNECT`. Without `BLUETOOTH_CONNECT`, netplay names a device by its model when the
  system settings give no device name.
- **Texts from the parent app** (#47, #48, #50): the FAQ popup no longer promises a reply within
  24 hours, in all 14 languages; the parent app's store listing is removed from the repository;
  and the F-Droid description is rewritten for this fork, in the folder that F-Droid reads. It can
  appear on F-Droid only with a release, and whether F-Droid lists this app was not checked.

None of these was run on a device.

## Native libraries built from source

Until now the app shipped native libraries that were committed in 2021 with no record in this
repository of how they were built. They are now built from pinned upstream sources with recorded
steps, and upgraded (#51 to #54). The build now compiles every native library in the app except
those that come with the Android NDK and the Oboe and JNA packages.

- libpng 1.6.59, was 1.6.34, for PNG files such as screenshots and texture packs; and EGLLoader
  (#51).
- SDL_net 2.4.0, miniupnpc 2.3.3 and libnatpmp, for netplay and its port mapping (#52). The router
  now sees the user agent `Android UPnP/1.1 MiniUPnPc/2.3.3`, and on a network with two routers
  the app can choose another one than before.
- SoundTouch 2.4.1, was 2.2, for the audio plugin's time-stretching (#53). Upstream's release
  notes list changes to time-stretching and to how the audio pipeline is cleared, so audio may
  sound slightly different.
- FreeType 2.15.0, was Android's copy of 2.5.3, for GLideN64's on-screen text (#54). Hinted text
  in GLideN64 can look slightly different.
- `libhidapi.so`, which nothing in the app used, is no longer packaged (#42).

The libraries were checked in local builds of the native code (each library's format,
dependencies, symbols and version), and CI built each pull request's debug APK. None was run on a
device; the device checks are listed under "Device verification".

## Tests and documentation

- New host tests cover RetroAchievements sessions and memory reads, save-state writes and loads,
  Controller Pak and Game Boy saves, cartridge save bounds, TLB writes, the FPU compares, minizip
  on the four Android ABIs and the native libraries' contents (#19, #29, #35, #38, #51 to #54).
- One host test builds with clang 23 again (#26), another finds `shasum` on Linux (#31), and the
  handoff notes tell a `git bisect` which of #25's commits to skip (#39).
- `docs/RETROACHIEVEMENTS_REMAINING_ISSUES.md` records RetroAchievements' hardcore compliance
  checklist and the dated device checks (#19 and later pull requests).

## Device verification

The device checks named above ran on debug builds of the pull requests' branches, not on this
release build.

DEVICE_RESULTS

Pending when these notes were written:

- The rebuilt native libraries: a game starts with GLideN64, Rice and Glide64mk2; a screenshot is
  saved and opens; GLideN64's on-screen text compared with the previous build; a GLideN64 and a
  Rice hi-res texture pack load and look the same; audio with "floating point" off and on, at
  normal speed and time-stretched, and after pause and resume; **Get Code** adds and removes the
  router's port mapping and shows the public address, through NAT-PMP and through UPnP; and a
  netplay session between two devices.
- The maintainer's three RetroAchievements checks: a hardcore unlock, and how the server records
  it; an unlock made offline, sent after the next sign-in; and a screenshot of the line at the top
  of the screen with an active challenge, achievement progress or a leaderboard value.

Still not re-tested, as for v338: N64DD disk saves (custom tracks and machines) across a restart,
and the writable-cart `.cart_ram` persistence path. Keep backups of those saves.
