# RetroAchievements — remaining issues

Status: merged to `master` via PR #10 (`af7dd84f0`).
Confirmed working on device: Connect sign-in ("Signed in as pwnedbygary", session token
persisted), hardcore gates for save/load state, slots, GameShark and cheats (deny toast),
and denied actions no longer announce first. Hardcore has since stopped refusing saves and
refuses only loading (see "Save states and Resume under hardcore").

This file lists what is still open. Evidence labels: OBSERVED = read from current source, or
seen in a named run such as "OBSERVED (host JVM)"; DERIVED = inferred from code paths;
REFERENCE = read from a pinned outside source (RetroAchievements server or client source or
documentation, AOSP, or libretro); HYPOTHESIS = plausible but untested.
Mode names: this file uses rcheevos's name, softcore, for the mode that RetroAchievements and the
app's text now call casual.
Issue #15 has its own section at the end; the rest are device verification results, deferred
token-handling items and the remaining device-verification gaps.

## Fixed on this branch
- Cancel on confirmation dialogs no longer performs the action: `CoreFragment`
  (`SAVE_STATE_FILE_CONFIRM_DIALOG_ID`) and `GamePrefsActivity` (`DOWNLOAD_CONFIRM_DIALOG_ID`,
  `UPLOAD_CONFIRM_DIALOG_ID`) now require `which == DialogInterface.BUTTON_POSITIVE`. The Drive
  download/upload confirmations previously ran on Cancel too.
- Hardcore denials no longer announce "Saving/Loading …" before being denied (`cb504d624`).
- Overwrite confirmation was skipped when hardcore would deny the save: the UI asked the service
  via `blockIfRaHardcore()` before prompting. Hardcore no longer denies saves, so that check and
  the method are gone (see "Save states and Resume under hardcore").
- Fast-forward is blocked under hardcore: `CoreService.setCustomSpeed()` rejects speeds above
  baseline (forcing baseline) and the UI does not flip its speed state; new toast
  `ra_hardcoreFastForwardBlocked`.
- Announcement TOCTOU removed: `CoreService.saveState(filename, overwriting)` takes the
  overwrite decision from the UI instead of re-checking `exists()`.
- Dead `LOAD_STATE_*` constants and `getLoadGameState()` removed (no callers; ABORTED is not
  observable through it). The load-state/ABORTED note now lives on `loadGame()`.
- `LoginPreference.onDetached()` uses `RetroAchievementsManager.peekInstance()` so it never
  constructs the client (or loads the native library) just to detach.

## Device verification (build `3.0.337 af7dd84f`, Retroid Pocket 6, 2026-09-21)
- Install + launch clean, no crashes; RA session active for Mario Tennis (achievement event).
- Hardcore slot load denied with only the deny toast — no "Loading slot 0…" announcement
  (the original ordering bug).
- Fast-forward toggle under hardcore shows `ra_hardcoreFastForwardBlocked` and the menu stays at
  "Speed 100 %" (UI state not flipped).
- Allowed path: "Save to file…" creates a user save and announces "Saving <name>…" once.
- Cancel on the overwrite confirmation left the existing file byte-identical
  (sha256 `d444cc902ee7d21b68ddd59ba968fd5de9167754358f8a36a658309c10efb854` before and after).
- Existing-file save under hardcore shows only the deny toast — the overwrite dialog is skipped.

## Device verification (debug build `3.0.338 06aedfe1`, Retroid Pocket 6, 2026-10-20)
- OBSERVED (`adb shell dumpsys package`, read at 22:15 that day): the debug package is version
  `3.0.338 06aedfe1`, first installed 2026-09-24 and last updated 2026-10-20 00:18, ten minutes
  before the first start below. DERIVED: the build replaced the earlier debug build in place,
  which keeps the app's data, and every session below ran it. The tester reported that Connect in
  Settings then showed "Signed in as" with their user name; the captures do not include that
  dialog.
- OBSERVED (device): the emulation process started 15 times. Two starts failed to open the ROM
  ("Failed to open ROM file for reading."; the ROM's content URI from the Argosy launcher had no
  provider) and showed no RetroAchievements notice. The other 13 ran Mario Tennis (MD5
  `759358FAD1ED5AE31DCB2001A07F2FE5`), and each showed "RetroAchievements: casual mode" and no
  other RetroAchievements notice (toasts read from `dumpsys notification`). DERIVED: none of this
  covers hardcore, and each of the 13 signed in and loaded the game: the app asks for the game only
  after a sign-in succeeds (`CoreService.java:1485-1511`, the only `loadGame` call) and shows the
  mode toast only once the game has loaded (`:1468-1482`).
- OBSERVED (device): the in-game achievement list read "Casual: 0 of 97 unlocked" and "0 of 658
  points", showed the warning row "Warning: Unknown Emulator" ("Hardcore unlocks cannot be earned
  using this emulator."), and listed the achievements on screen, "Match Winner", "Intense Match"
  and "MAXimum Overdrive!", as "Locked" (the third row's status line is cut off by the bottom of
  the list; what shows of it matches "Locked"). DERIVED: achievements about play, such as winning a
  match, have to read memory, so their addresses passed rcheevos's check when the game loaded;
  with the read callback at `29a0dfb48`, the commit before `cc3553790`, they would have shown as
  "Unsupported" (see "Memory reads" in the issue #15 section).
- OBSERVED (device): no session logged a RetroAchievements event (`CoreService.java:1430` at
  `06aedfe12` logs each event's JSON except progress updates). DERIVED: that fits what was played.
  The captured screens and the tester's reports show only the intro, the app's in-game menu and
  the achievement list, nothing is known to have been earned, and the server's warning does not
  pop in softcore (see "On-screen notices"). The captures do not cover every minute of the longer
  sessions, and no run on this build has yet shown an event reaching the app, so these logs cannot
  tell "nothing was earned" from "events did not arrive".
- Still open: the "Not yet verified on device" items in the issue #15 section, among them an
  unlock in softcore from an achievement that reads memory, and everything under hardcore.

## Deferred items from commit `8f14880ef` (login/token handling)
Reference: that commit's message lists these as nonblocking. The issue #15 section ("Sign-in
order") addresses three of them. The saved token now goes before the password, which stays in
Settings as the fallback for a rejected token (so there is no offer to clear it). The token is
written with `commit()`. The missing token fallback after a failed password login is covered by
the same order: a saved token is always tried before the password. Still open:
- HTTP User-Agent version is hardcoded (`RetroAchievementsManager.java:57`).

## Verification gaps (not bugs; close with device tests)
- Terminal-failure release policy: with hardcore active and a broken login (offline, or a bad
  password with no valid saved token: a saved token is tried first), confirm saves are released
  once the async failure arrives instead of being blocked forever. Code-verified only.
- Dialog teardown guard: tap Connect, then immediately Back/rotate before the result arrives;
  confirm no crash and no orphan dialog. Code-verified only.

## Device data note
- The old `retroAchievementsWebApiKey` value remains in the device's SharedPreferences
  (inert — no reader in the tree). Removing it would require clearing app data (loses saves),
  so leave it unless a safe one-key cleanup is added.

## Issue #15 (branch `fix/ra-hardcore-speed-achievements`)
Issue: https://github.com/pwnedbygary/mupen64plus-ae-turnip/issues/15 (stays open).

### Hardcore unlocks: server side, not fixable in app code
- REFERENCE (RAWeb `ac7bf9b84d98`, `master` on 2026-10-19):
  `app/Connect/Actions/AwardAchievementAction.php:121-123` clears the hardcore flag unless
  `allowsHardcoreUnlocks()`, which is true only for Full, Warned and CasualPending
  (`app/Enums/ClientSupportLevel.php:38-41`). `app/Platform/Services/UserAgentService.php:329-331`
  returns Unknown when the emulator name is not recognized.
  `app/Connect/Actions/GetAchievementSetsAction.php:532-536` titles its warning achievement
  "Warning: Unknown Emulator" for Unknown clients, and the client-warning achievement ID is
  `101000001` (`app/Models/Achievement.php:129`).
- OBSERVED: the app sends `USER_AGENT = "Mupen64PlusAE/3.0 (Android)"`
  (`RetroAchievementsManager.java:57`). `CoreService.onRaEvent` logs each event's JSON except
  progress updates; in `ff249b18a` it logged only the event type.
- DERIVED: hardcore unlocks are recorded as softcore until RetroAchievements approves this client.
  The server's warning achievement is shown on screen when it unlocks, which happens only in
  hardcore (see "On-screen notices"), and in the in-game achievement list in both modes. The
  server's separate `Warning` text (RAWeb `ac7bf9b84d98`, `GetAchievementSetsAction.php:557`) is
  never shown, because rcheevos does not read that field. Not yet verified on device: that award
  requests reach the server and come back as softcore.

### Memory reads
- OBSERVED: rcheevos addresses N64 RAM as `0x000000`-`0x7FFFFF`; `0x80000000` is only the matching
  real address (`rc_consoles.h:121-123`; `consoleinfo.c:731-735,1153-1154`). rcheevos passes the
  address unchanged to the read callback (`rc_client.c:1363,5890,5914-5929`; `rc_peek_value`,
  `memref.c:648-677`). When the game loads, an achievement that uses an address past the last
  region, or one the callback refuses, is disabled (`rc_client.c:1348-1350,1361-1366`). Its
  trigger is switched off (`rc_client.c:1304-1308`) and then always evaluates as inactive
  (`trigger.c:169-171`), only active achievements enter the runtime (`rc_client.c:1411-1412`), and
  the hardcore toggle and reset skip it (`rc_client.c:1457-1470,4865-4866`).
- OBSERVED: until this change, `ra_glue_read_memory` refused every address below `0x80000000` and
  subtracted that base before reading (`ra_glue.c:97-100` at `29a0dfb48`; added in `4df6051aa`).
  Its comments (`ra_glue.c:4-8,84-88` at `29a0dfb48`) said rcheevos passes `0x80000000`-based
  addresses.
- DERIVED: so every achievement whose logic reads memory was disabled when the game loaded, in
  hardcore and in softcore, and the in-game list showed it as "Unsupported", or as "Unlocked" if the
  server already had it in this mode (`rc_client.c:1808-1811`). Leaderboards that read memory were
  disabled the same way (`rc_client.c:1316-1344`). This stopped softcore unlocks too, separately
  from the server rule under "Hardcore unlocks: server side".
- REFERENCE (RAWeb `ac7bf9b84d98`, `app/Connect/Actions/CreateWarningAchievementAction.php:16`):
  the server's warning achievement is `1=1.300.`, which reads no memory. DERIVED: it was the only
  kind of achievement that could still unlock, and only in hardcore, since the server marks it
  earned in softcore (see "On-screen notices"). HYPOTHESIS: it was the achievement event in the
  2026-09-21 device notes. It would need that session to have run in hardcore, as several of that
  day's checks did.
- OBSERVED: the callback now treats the address as an offset into RDRAM. It refuses an address
  only while `g_mem_base` is NULL or at 16 MB and above, and reads from
  `mem_base_u32(g_mem_base, MM_RDRAM_DRAM + address)` (`ra_glue.c:109-134`).
- REFERENCE: in RetroArch, address X is byte X of the core's RDRAM. Its Mupen64Plus-Next core
  (mupen64plus-libretro-nx `develop` `12edd2c74a51`, `libretro/libretro.c:2108-2112,2120-2124`)
  exposes `g_dev.rdram.dram` as system RAM with no memory map, and rcheevos `v12.5.0`
  `src/rc_libretro.c:653-725,727-744` maps each N64 region onto system RAM at its offset from the
  first region.
- OBSERVED: here `rdram->dram` is `mem_base_u32(base, MM_RDRAM_DRAM)` (`device.c:187`;
  `rdram.c:132`). DERIVED: so for address X the callback now reads the byte RetroArch reads.
- OBSERVED: rcheevos checks a loaded game's addresses when the last load response arrives, on the
  thread that delivers it, unless background memory reads are off (`rc_client.c:1982-1983`,
  `:1997-2001`, `:2414-2418`); they are on by default (`rc_client.c:175`). The app delivers
  responses on its HTTP thread (`RetroAchievementsManager.java:280,287`). `g_mem_base` is NULL
  until `CoreStartup` (`main.c:111`; `frontend.c:105`), which the service runs on its handler
  thread (`CoreService.java:780`) after other start-up work, while the sign-in starts from
  `onStartCommand` (`CoreService.java:1247,1367`) and the game loads as soon as it succeeds
  (`CoreService.java:1508`); nothing orders the two. `CoreShutdown` frees that memory on the
  handler thread (`CoreService.java:946`; `CoreInterface.java:738-740`; `frontend.c:135-136`).
- DERIVED: with the new callback alone (background reads still on), a session whose sign-in and
  game load finished before the core started would have every memory-reading achievement
  disabled, and a response handled while the core shut down could read freed memory. Before this
  change the timing did not matter: on the response thread, rcheevos passes the callback no
  address above `0x7FFFFF` (`rc_client.c:1362-1364`), and the previous callback returned without
  reading memory for all of those (`ra_glue.c:97-98` at `29a0dfb48`). So every session lost those
  achievements anyway, as above, and no response could read freed memory. A frame could pass a
  larger address only between the game becoming active and its address check, as it updates
  every address (`rc_client.c:1815-1817,1861,5995,6440-6444`); later frames skip a bad address
  and do not read past the map (`rc_client.c:5886-5887,5909-5910,5991-5992`). That read ran on
  the emulator thread during emulation, when the memory existed. HYPOTHESIS: an early finish
  would be rare, and likelier when the core is slow to start (a Raphnet adapter, or game data
  copied from external storage).
- OBSERVED: `ra_glue_create` now turns background memory reads off (`ra_glue.c:462-465`). rcheevos
  then queues the rest of the game load, which applies the server's unlocks, checks the addresses
  and reports the result (`rc_client.c:1787-1923`, queued at `:1936-1950`), and runs it from
  `rc_client_idle` (`:6507-6531`), which `rc_client_do_frame` calls at its end even before a game
  is loaded (`:6490`). The emulator calls `ra_glue_do_frame` from `new_vi` (`main.c:986`) on the
  handler thread, between `CoreStartup` and `CoreShutdown`. The load result now comes from that
  thread too, and the Java side still posts it to the main thread
  (`RetroAchievementsManager.java:314-316`). DERIVED: rich presence, which rcheevos reads for each
  ping (`rc_client.c:5745-5752`), is read there as well.
- OBSERVED: `ra_glue_idle` runs the same queued work (`ra_glue.c:514-520`;
  `rc_client.c:6493-6536`), and the core's pause loop now calls it while emulation is paused (see
  "Paused emulation"). The comments keep it on the emulator thread with `ra_glue_do_frame`
  (`ra_glue.c:10-17,462-464`).
- OBSERVED (host C test): `tools/test-ra-memory.sh` builds the production `ra_glue.c` and
  `memory.c` and reads five addresses in each region of rcheevos's N64 map, one byte and, where
  they fit, four bytes at a time, in the full and the compressed memory base modes. It expects the
  bytes stored at `mem_base_u32(base, MM_RDRAM_DRAM)`, at the offset that the region's real address
  gives (a KSEG0 address: physical = real - `0x80000000`), and checks that no read goes past the
  16 MB RDRAM area. Against the previous callback all 54 reads failed; all pass now, with GCC
  16.2.1 and with clang 23.1.1.
- OBSERVED (host C test): `tools/test-ra-session.sh` links the production `ra_glue.c` and
  `memory.c` with the rcheevos sources the core builds, and answers rcheevos's requests from a fake
  server. The game's first achievement reads the RAM byte at address `0x10`. The test checks that
  the achievement is active after two frames, and that it unlocks and an unlock request is sent
  when the byte goes from 0 to 1. This unlock runs once with the core's memory created before the
  game loads and once after; the test's other runs are under "On-screen notices", "Challenge and
  progress indicators", "Paused emulation" and "Network requests and shutdown". Both pass now,
  with GCC 16.2.1 and with clang 23.1.1. With the previous callback both fail. With the new
  callback but background reads on, the second fails because the achievement is not active and
  nothing unlocks, and both fail the check described under "Paused emulation".
- OBSERVED (host build): `./gradlew :app:testDebugUnitTest --rerun :app:assembleDebug` passes, and
  `ra_glue.c` was recompiled for all four ABIs with empty ndk-build stderr logs. In the linked arm64
  and x86_64 libraries, `ra_glue_read_memory` compares the address only with 16 MB and passes it to
  `mem_base_u32` unchanged, and `ra_glue_create` turns background memory reads off.
- OBSERVED (device, 2026-10-20): the list showed the achievements on screen as "Locked", not
  "Unsupported", and the mode notice appeared as the game started (see "Device verification" for
  build `3.0.338 06aedfe1`). Not yet verified on device: an achievement that reads memory unlocks
  in softcore; under hardcore the server still records it as softcore until RetroAchievements
  approves this client; the mode notice also appears after a slow start.

### Threads in the glue
- OBSERVED: rcheevos does not lock every read of the loaded game. The getters the glue uses for
  user, game and subset info and for the game-loaded, load and hardcore state take no mutex
  (`rc_client.c:900-916,3080-3107,3525-3541,3562-3588,6987-6998`). Other calls the glue makes
  check for a game, or read it, outside the mutex: among them the game summary
  (`rc_client.c:938-949,1030-1031`), the achievement list (`:4289-4294`), reset (`:6643-6657`),
  the hardcore switch (`:6967-6983`), the progress calls (`:6728,6755,6879`) and
  `rc_client_do_frame`, before it locks and after it unlocks (`:6436-6440,6465-6467`). The
  `ra_glue.c` header said every rcheevos call the glue makes is mutex-protected, and the
  `RetroAchievementsManager` javadoc said its API may be called from the emulator thread and from
  Java threads; both now state the rule that follows (`ra_glue.c:10-17`;
  `RetroAchievementsManager.java:39-42`).
- OBSERVED: the achievement list holds pointers to rcheevos's live achievement data
  (`rc_client.c:4413`), and the glue reads each one's progress text and bucket after rcheevos has
  released its mutex (`rc_client.c:4433`; `ra_glue.c:734-742`). The emulator thread rewrites
  both outside the mutex when it raises events for that achievement
  (`rc_client.c:6190-6193,6465-6467`). DERIVED: a list read during play can show a row as it was
  a frame earlier, or progress text that is being rewritten.
- OBSERVED: rcheevos makes a loaded game visible under its mutex (`rc_client.c:1815-1818`), and
  `rc_client_is_game_loaded` is true from then on (`:3106`), but it checks the game's addresses
  afterwards, outside the mutex (`:1861`). That check moves each achievement that uses a bad
  address to the "Unsupported" group (`:1303-1305,1346-1370`). The list builder holds the mutex
  while it counts the groups and allocates space for them, then fills them in a second pass that
  reads each achievement's group again (`:4294-4433`). An unlock cannot move an achievement
  between the glue's groups meanwhile: rcheevos sets the unlocked state under the mutex
  (`:4812-4816`), the group follows the state (`:4105-4158`), and the glue's lock-state grouping
  counts Recently unlocked and Unsynced as Unlocked, and Active challenge and Almost there as
  Locked (`:4226-4234`). DERIVED: a list built while a game finishes loading can see an
  achievement turn "Unsupported" between counting and filling. If that group was already counted,
  the second pass can list the achievement twice, writing past the end of the space allocated;
  with more than one active set, it can also fill a group that was not counted, writing over the
  list's own pointers (`:4353-4357,4397-4431`). That needs a set with an address the check
  refuses, which with the core's memory present is only an address above `0x7FFFFF`, the end of
  rcheevos's N64 map (`:1349-1350,1362`; `ra_glue.c:119-123`; see "Memory reads"). The overlay
  reads the list when its activity is created (`GameActivity.java:417`;
  `RaIndicatorOverlay.java:69-70`). Outside netplay, that happens during play only when Android
  recreates the activity for a configuration change the manifest does not list; rotation is
  listed (`AndroidManifest.xml:386`; `GameActivity.java:568`). Otherwise the old game screen's
  `onStop` has paused emulation, and emulation resumes only after the new screen's overlay has
  read the list (`GameActivity.java:643-676,780-790,1101-1112,1170`). Outside netplay, the dialog
  reads the list while emulation is paused (see "Paused emulation"). In netplay neither the menu
  nor `onStop` pauses emulation (`CoreService.java:439-447`; `main.c:481-482`), and achievements
  run there in softcore (`CoreService.java:1351-1355`), so in netplay both reads could overlap a
  frame before this change too.
- OBSERVED: `ra_glue_reset` runs on the thread that resets the core (`main.c:802-816`;
  `frontend.c:359`; `CoreInterface.java:894-897`). The app resets it from `CoreService.restart()`
  (`CoreService.java:608-611`), which a confirmation dialog and the reset button mapping reach
  (`CoreFragment.java:901-907`; `GameActivity.java:1121`; `PeripheralController.java:332`).
  DERIVED: Android delivers those dialog and input callbacks on the main thread, so the reset runs
  there, not on the emulator thread that `ra_glue.h` named. Its comment now says so
  (`ra_glue.h:42-43`).
- DERIVED: the app keeps the header's rule. rcheevos unloads the game when `ra_glue_load_game`
  replaces it (`ra_glue.c:577-578`), on logout (`rc_client.c:887-888,894`), and on a reset whose
  media belongs to another game (`rc_client.c:6643-6652`). The app reaches none of these during a
  session: it loads one game per session, after the first successful sign-in
  (`CoreService.java:1508`); nothing calls `RetroAchievementsManager.logout()`
  (`RetroAchievementsManager.java:208-210`); and the glue never changes media, so the hash of the
  loaded game maps to that game (`rc_client.c:2329,2334-2335`; an unknown game keeps both at 0:
  `:2320,2427,2620-2622`). The client is destroyed by `ra_glue_shutdown` from
  `CoreService.onDestroy` (`CoreService.java:1306,1390`), on the main thread like every caller of
  the getters (`AchievementsDialog.java:50-53`; `CoreService.java:1478,1500`), and
  `ra_glue_shutdown` waits for a server response that an HTTP thread is handling
  (`ra_glue.c:479-486`). On a normal exit the service stops itself only after `emuStart` returns
  (`CoreService.java:919,980,1539-1552`); its other exits call `forceExit`, which kills the
  process right after asking the service to stop (`CoreService.java:1532-1536`).

### Paused emulation
- OBSERVED: rcheevos expects `rc_client_idle` to be called while emulation is paused
  (`rc_client.h:882-883`). It runs the scheduled work that is due, such as retries, a queued game
  load and the ping timer, and raises pending disconnect events (`rc_client.c:6493-6536`). The
  ping timer fires 30 seconds after the game loads and then every two minutes, and sends a ping
  only if frames ran since the last one. rcheevos counts frames only in `rc_client_do_frame`;
  its comment says the server session otherwise expires, to be resumed or restarted once frames
  run again (`rc_client.c:1899,5731-5735,5771,6469`). While emulation is paused, the emulator
  thread stays in the core's pause loop, which now calls `ra_glue_idle` after each 10 ms wait
  (`main.c:952-965`). `main_check_inputs` in that loop only calls `SDL_PumpEvents`
  (`main.c:304-310`), an empty function in the app's SDL2 stub
  (`ndkLibs/SDL2_stub/SDL_events.h:1001-1004`). The Java wrapper `RetroAchievementsManager.idle()`,
  which nothing called and which invited calls from other threads, is gone.
- DERIVED: during a pause, scheduled award and leaderboard retries are sent and raise their
  "disconnected" and "reconnected" notices, a game load whose last server response arrives then
  finishes with its mode notice, and a progress indicator still hides two seconds after its last
  change. Before this change all of that waited until emulation resumed. Pings do not go on: a
  pause sends at most one, the first that falls due, and only if frames ran since the last ping.
  The calls run on the emulator thread while the core's memory exists, like `ra_glue_do_frame`,
  and end before the client is destroyed: stopping emulation clears the pause
  (`main.c:1990-1994`), the pause loop runs inside `emuStart` (`CoreService.java:919`), and the
  service destroys the client only after `emuStart` returns, or kills the process (see "Threads
  in the glue").
- DERIVED: outside netplay, the achievement dialog opens only from the in-game menu, which pauses
  emulation (`GameActivity.java:460-465,963-964`), and a game screen that opens anew for a
  running session reads the list for its overlay before emulation resumes (see "Threads in the
  glue"). Until this change no rcheevos work on the emulator thread could run while either built
  its list in that case. Now a game load that finishes during the pause can, and its address
  check changes achievements' list groups outside rcheevos's mutex (see "Threads in the glue").
  In netplay neither the menu nor `onStop` pauses emulation, so there both reads could overlap a
  frame before this change too.
- OBSERVED (host C test): every run in `tools/tests/ra-session-test.c` now checks that only the
  sign-in has reported before the first frame or idle call, so the game load waits for the
  emulator thread. Its seventh run pauses: `ra_glue_idle` alone finishes the game load, and sends
  no ping when the first one falls due, as no frame has run. Frames then run for an unlock, and
  `ra_glue_idle` between the frame in which the achievement is false and the one in which it is
  true does not unlock it. The unlock's first two answers are HTTP 503; in the pause that
  follows, `ra_glue_idle` raises "disconnected", sends the retry one second later on the test's
  clock and raises "reconnected", then sends the next ping and not the one after it. Every run
  passes with GCC 16.2.1 and clang 23.1.1, and under clang ASan and UBSan with no memory error
  (UBSan reports only the two vendored sites named under "On-screen notices"). A copy of the glue
  whose `ra_glue_idle` does nothing fails 10 checks, and one whose `ra_glue_idle` runs a whole
  frame fails 4, all in the paused run; one that turns background memory reads back on fails the
  sign-in check in every run. The pause loop itself has no host test: it is static in `main.c` and
  needs SDL and the video extension.
- OBSERVED (host build): `./gradlew :app:testDebugUnitTest --rerun :app:assembleDebug` passes with
  every unit test, and `main.c` and `ra_glue.c` were recompiled for all four ABIs with empty
  ndk-build stderr logs. In the linked arm64-v8a and x86_64 libraries, whose build IDs match the
  packaged ones, `new_vi` has the pause loop inlined: it checks `g_rom_pause`, calls
  `SDL_Delay(10)` and then `ra_glue_idle`, and repeats (NDK `llvm-objdump`).
- Not yet verified on device: opening the in-game menu right after a game starts, so the game load
  finishes during the pause, shows the mode notice while paused; a retry is sent during a pause.

### Network requests and shutdown
- OBSERVED: the HTTP thread now hands each request to `RetroAchievementsManager.request()`
  (`RetroAchievementsManager.java:279-290,382-419`). It answers a network failure (`IOException`)
  with status -2 and "RA network error: <message>", as before, and any other runtime exception
  with status -2 and "RA request error: <exception>", so every request still gets one answer
  (`:411-417`). Before, `onServerCall` caught only `IOException`. REFERENCE (AOSP
  `android-14.0.0_r1`, `RuntimeInit.java:125-177,235`, `KillApplicationHandler`): an exception
  that leaves a thread reaches Android's default handler, which kills the process; the app sets no
  handler of its own. DERIVED: so any other exception during a request closed the emulation
  process, or the app during Connect in Settings. An `Error`, such as `OutOfMemoryError`, still
  does.
- DERIVED: rcheevos retries a status -2 answer (`rc_client.c:625-627`), so an unlock or a
  leaderboard entry that hits such an exception is sent again, like one that hits a network
  failure. If the server had already recorded the unlock, rcheevos reads its "User already has"
  answer as a success (`rc_api_runtime.c:706-717`). A sign-in that hits one ends with
  "RA request error: <exception>" as the reason (see "On-screen notices").
- OBSERVED (host JVM): `RetroAchievementsManagerTest` runs `request()` with a fake connection,
  whose answer must be passed on, and with connectors that throw. A network failure must be
  answered with status -2 and "RA network error: <message>", and an `IllegalStateException` with
  status -2 and its message. Without the new catch the last test failed, with the exception
  escaping (Gradle: 7 tests, 1 failed); every unit test passes now.
- OBSERVED: `ra_glue_shutdown` now sets `s_shutting_down` under the request lock before it drops
  the pending requests and waits for answers being handled, and clears it after destroying the
  client (`ra_glue.c:78-81,479-495`). While it is set, `ra_glue_server_call` gives a request no
  slot and answers it at once with status -1, `RC_API_SERVER_RESPONSE_CLIENT_ERROR`
  (`ra_glue.c:202-204,216-234`), which rcheevos does not retry (`rc_client.c:629-631`): the unlock
  ends with a server error, and rcheevos frees its data (`rc_client.c:4669-4674,4753-4758`).
- DERIVED: before, an answer handled during shutdown could send an immediate retry
  (`rc_client.c:4675-4680,5377-5382`) into a slot taken after the drop, and the answer to that
  retry could reach the destroyed client. Answering it with HTTP 503 instead would leave a retry
  scheduled that never runs, as `rc_client_destroy` neither runs nor frees scheduled work and
  unloading the game removes only its ping (`rc_client.c:191-227,3185-3198`). The app kills its
  process right after the shutdown (`CoreService.java:1306-1308`) and removes its listener before
  it (`:1389-1390`), so the server error shows no notice.
- OBSERVED (host C test): the eighth run in `tools/tests/ra-session-test.c` answers an unlock with
  HTTP 503 on another thread and holds that thread just before rcheevos sends the unlock again,
  until `ra_glue_shutdown` has dropped the pending requests and waits for it. The new request must
  not reach Java or keep a slot, rcheevos must raise one server error for it that names the
  unlock's API and achievement, and a client created afterwards must sign in and load the game
  again. With the previous glue the run fails 5 checks and the other seven pass. With this glue
  all eight pass with GCC 16.2.1 and clang 23.1.1, 20 more times with each, and under clang ASan,
  UBSan and LeakSanitizer with no memory error or leak (UBSan reports only the two vendored sites
  named under "On-screen notices"). A copy that answers HTTP 503 during shutdown fails the three
  server-error checks and leaks 88 bytes, the unlock's data and its scheduled retry
  (`rc_client.c:4688,4841`); one that never clears the flag fails every later session.
- Not yet verified on device: neither case is easy to cause there.

### Speed lock under hardcore
- OBSERVED: `RaHardcoreRules.isSpeedAllowed` allows only 100%, so slow motion is refused as well as
  fast-forward (the old rule refused only speeds above 100%). `CoreService.blockIfRaHardcoreSpeed`
  replaces `blockIfRaHardcoreFastForward`. A request for 100% is never refused, so a session that
  is off normal speed can always return to it.
- OBSERVED: under hardcore, frame advance is refused. Emulation starts with the core frame limiter
  on when hardcore is active (`CoreService`: `isFramelimiterEnabled || isRaHardcore()`), and the
  limiter toggle ("Sync audio" / "Desync audio") may switch it on but never off
  (`RaHardcoreRules.isFramelimiterToggleAllowed`).
- DERIVED: so audio sync never paces a session while hardcore is enforced, because
  `isRaHardcore()` can only turn from true to false after the first start command (see "Hardcore
  session start"). In `2b2e75976` the toggle could return to the setting the session started
  with, and with audio sync on (the default, `GlobalPrefs.java:727-730`) that was limiter off.
  With the audio volume at 0 the dummy audio plugin is used (`AppData.java:175-177`) and does no
  pacing (`dummy_audio.c:54-57`). The real plugin paces only while the limiter is off
  (`plugin.cpp:348,378-379`), and only through the audio the game sends (`AiLenChanged`,
  `plugin.cpp:330-379`). Keeping the limiter on covers both cases. Whether a quiet game actually
  ran fast under audio sync was never tested.
- OBSERVED: toast `ra_hardcoreFastForwardBlocked` is now `ra_hardcoreSpeedBlocked` ("Hardcore mode:
  speed controls are disabled"). The older entries above keep the old name because they record
  what was tested at the time.
- OBSERVED (host JVM): `RaHardcoreRulesTest` failed against the old speed rule (99% allowed),
  against a limiter rule that refused every switch, and against the previous limiter rule at the
  default audio-sync setting (switching on was refused); it passes now.
- Not yet verified on device: hardcore starts with the limiter on and refuses fast speed, slow
  speed, frame advance and switching the limiter off; softcore still allows all of them.

### Hardcore session start
- OBSERVED: `startRetroAchievements()` returns at once on every start command after the
  service's first (`mRaStartAttempted`), before the settings check, `initialize()`,
  `setHardcore`, login and `loadGame`.
- DERIVED: a new `GameActivity` for the running game (notification intent,
  `CoreService.java:1147-1166`, or resume broadcast, `:240-243`, after the old activity was
  destroyed) starts the service again, via `GameActivity.onStart` (`GameActivity.java:665-671`),
  `CoreFragment.startCore` (`CoreFragment.java:395-404`) and `ActivityHelper.startCoreService`
  (`ActivityHelper.java:485-486`). That start no longer logs in again, reloads the game, clears
  `mRaSessionFailed` or calls `setHardcore`. In `2b2e75976` that path could re-arm hardcore on a
  live session after speed changes or cheats made while enforcement was released. A session
  whose login failed now stays released, without achievements, until the game is closed.
- OBSERVED: a netplay session runs achievements in softcore (`setHardcore(... && !mUsingNetplay)`)
  and shows "Netplay: hardcore mode is off for this session" when hardcore is on in Settings.
  Netplay catch-up turns the core speed limiter off and back on (`netplay.c:353,357`) with no
  hardcore check.
- Not yet verified on device. Re-arm test: turn on "Don't keep activities", start a hardcore
  session offline so login fails, fast-forward, press Home, reconnect, tap the notification, then
  check that save states and speed controls stay allowed and no new login is attempted. Netplay
  test: with hardcore on, the message shows and the achievement list says "Casual".

### Save states and Resume under hardcore
- REFERENCE (compliance page revision `361119b677dc`, section B): "Loading save states is ALWAYS
  blocked in hardcore." An emulator with a "resume/quick resume" feature must make "the resumed
  session ... drop to Casual mode". "Users should be allowed to create save states while in
  hardcore for debugging purposes, but those states cannot be loadable when hardcore mode is
  enabled."
- OBSERVED: hardcore no longer refuses creating a save state: a slot (`CoreService.saveSlot`), a
  file (`CoreService.saveState`; `CoreFragment.saveState` no longer asks the service before its
  overwrite prompt), or the auto save made when the game screen stops or the game is exited
  (`CoreService.autoSaveState`, called from `GameActivity.java:784-787,1470-1473`). Loading a slot
  or a file is still refused, now with "Hardcore mode: loading save states is disabled"
  (`CoreService.java:414-426`, called by `loadSlot` and `loadState`, `:508-524`). A GameShark
  press, which shared the old "save states and load states are disabled" text, shows "Hardcore
  mode: cheats are disabled" (`:585-591`).
- OBSERVED: in `9e8066d30` a hardcore start skipped the auto save (`CoreService.java:895-899` at
  `9e8066d30`) and started the game from power-on without saying so. Now the emulation job reads
  the restart flag and the latest auto save once (`CoreService.java:711-716`), right after it
  has copied the game's data back from external storage when that setting is on (`:706-709`).
  If that auto save exists, `leaveRaHardcoreForResume` turns hardcore off on that thread and
  posts the notice "Resumed game: hardcore mode is off for this session" to the main thread
  (`:398-412`). The job then sets the frame limiter and the cheats (`:877-892`) and loads that
  same file, only if it exists (`:900-903`). In `9e8066d30` a start that was not a restart (Resume
  or a long press), outside hardcore and with no auto save, asked the core to load the path that
  `getLatestAutoSave` returns when there is none (`GameDataManager.java:80`), which failed
  (`savestates.c:1456-1462`). The gallery's Resume and a long press on a game resume
  (`GalleryActivity.java:1050-1055,1161-1163`); its Start, netplay, and launches from another app
  or from Android TV restart (`:1056-1076,295-296,837-841`).
- REFERENCE (AOSP `android-10.0.0_r1`, `Toast.java:131-147,403-418,446-451`): before Android 11 a
  toast is shown by a message to the looper of the thread that made it. From Android 11 the text
  toasts of an app that targets API 30 or higher are drawn by SystemUI (`android-11.0.0_r1`,
  `Toast.java:113-114,207-210`). This app targets 34 and supports Android 6 and up
  (`build_common/version_common.gradle:9-10`). DERIVED: the job's thread stays in `emuStart`
  until emulation ends, so on Android 6 to 10 a toast made there never shows; that is why the
  notice is posted to the main thread. The cheats toast has the same problem (see Open items).
- OBSERVED: with game data on external storage, a session that ends normally copies the game's
  data folder to that storage and, if the copy succeeds, deletes the local copy
  (`CoreService.java:941-943,1005-1037`; the deletion `:1029-1034`), and the next session's job
  copies it back (`:984-1003`). The auto saves are in that folder (`GamePrefs.java:764`).
- DERIVED: so the decision has to wait for that copy, and the job makes it before emulation
  starts, on the thread that then runs the emulator. rcheevos activates the game's achievements
  only in the emulator's frames (see "Memory reads"), so the switch reaches it first. The switch
  and the load use one reading of the restart flag and the path, so a later start command cannot
  separate them, and a session that stays in hardcore asks for no load at all. A resumed session
  gets its cheats and the frame-limiter setting like any softcore session. One whose auto save
  exists but then fails to load still runs in softcore, after a notice that says it resumed. An
  auto save made in hardcore can be resumed only in softcore; Start keeps hardcore.
- DERIVED: rcheevos puts the current hardcore setting into its start-session request
  (`rc_client.c:2020`), which it sends as soon as the game's data arrive (`rc_client.c:2344-2351`),
  on the HTTP thread that delivers them (`RetroAchievementsManager.java:287`). The sign-in starts
  from `onStartCommand` and does not wait for the job, so that request can go out before the
  switch when the job is late, for example with a slow copy from external storage, a large
  texture-dump folder to delete first (`CoreService.java:704`) or a Raphnet adapter it waits for
  (`:1570,1587`). The server then sees a session that started in hardcore switch to softcore,
  which the page allows: "Switching from hardcore mode to casual mode is allowed mid-session."
  (section B).
- OBSERVED: `leaveRaHardcoreForResume` writes the glue's `s_hardcore` on the emulation thread,
  while the main thread reads it through `ra_glue_get_hardcore()` for every hardcore check
  (`CoreService.java:384-387`). It was a plain `int` (`ra_glue.c:49,471,478` at `7aa5bdb3e`),
  and a scratch ThreadSanitizer build in which one thread turns the mode off while another reads
  it reported that data race; it is now atomic (`ra_glue.c:57,530-541`), and the same build
  reports none. DERIVED: the mode only turns off during a session, so a stale read could only
  keep a hardcore check on for longer.
- No automated test: `CoreService` is an Android service, and the app's unit tests run on the
  host JVM without Android. OBSERVED (host build, `javap`): the compiled job calls
  `copyGameContentsFromSdCard`, then reads `mIsRestarting` and calls `getLatestAutoSave` once,
  checks that the file exists and calls `leaveRaHardcoreForResume`, all before it sets the frame
  limiter and the cheats, and calls `emuLoadFile` with that path, only when the file exists,
  before `emuStart`. `leaveRaHardcoreForResume` calls `setHardcore(false)`, then posts the code
  that shows the notice to a `Handler` on the main looper.
- Not yet verified on device: in hardcore, saving to a slot and to a file works and loading
  either is refused; exiting writes an auto save; Resume then shows the notice and the
  achievement list says "Casual", also with game data on external storage; Start keeps hardcore.
  The Retroid runs Android 13, so it cannot show the notice on Android 6 to 10.

### Achievement progress kept with save states
- REFERENCE (compliance page revision `361119b677dc`, section A): "Hit counts should be stored in
  save states. While not strictly required, this is highly recommended as it's important for
  debugging complex achievement logic and ensures proper functionality in casual mode."
- OBSERVED: the core calls `ra_glue_state_saved` after a save and `ra_glue_state_loaded` after a
  load, only when the save or load function reports success, with the state's path and the
  console RAM (`savestates.c:1478-1479,2215-2216`; declared without `EXPORT`, `ra_glue.h:48-54`).
  Both run on the emulator thread, which runs the core's save and load jobs
  (`interrupt.c:554-556,654-656`). A save writes the loaded game's progress, as rcheevos
  serializes it (`ra_glue.c:974-994`), to
  `<state folder>/.ra-progress/<state file name>.progress` (`ra_glue.c:854-868`): "RAPROG01",
  then the RAM's CRC-32, the RAM's size and the progress's size as little-endian 32-bit values,
  then the progress (`ra_glue.c:846-850,900-934`). It writes a temporary file and renames it over
  the old one, and if that fails it removes the old file (`ra_glue.c:929-930`). With nothing to
  keep, as with achievements off or before the game has loaded, the save removes the old file
  (`ra_glue.c:1021-1040`). So a state saved before the game has loaded keeps no progress, even
  while progress from a state loaded earlier waits for the game (next bullet).
- OBSERVED: an M64P save reports success once it has passed the file write to `queue_work`, even
  if that write then fails to open or write the file (`savestates.c:1492-1517,1933-1936`). The
  app's core build, `mupen64plus-core/mupen64plus-core.mk`, does not define `M64P_PARALLEL` or
  compile `workqueue.c` (only upstream's `projects/unix/Makefile:162` defines it), so `queue_work`
  runs the write at once, before the save hook (`workqueue.h:42-63`). A PJ64 zip save also reports
  success when its write fails (`savestates.c:2127-2158`), but the app saves only M64P states
  (`CoreInterface.java:834,850` pass type 1, `savestates_type_m64p` in `savestates.h:35-40`).
- OBSERVED: a load reads that file and applies it only if the RAM size and the CRC-32 in it match
  the RAM the core has just restored (`ra_glue.c:938-971`). Otherwise, or without a file, rcheevos
  is given no progress, which starts every count from zero (`ra_glue.c:997-1002`;
  `rc_client.c:6891-6893`). A load before the game has finished loading, as when a session
  resumes from its auto save, keeps the progress until the first `ra_glue_do_frame` or
  `ra_glue_idle` after the game has loaded (`ra_glue.c:83-91,504-520,1011-1019,1042-1060`).
  `ra_glue_reset` runs on another thread (see "Threads in the glue"), so it only marks that
  progress to be dropped, and the emulator thread drops it (`ra_glue.c:522-528`).
- OBSERVED: before this change the app kept one copy of the progress per game, in its
  `ra_progress` preferences: it saved it when emulation stopped and restored it once the next
  session's game had loaded, whatever that session started from, and loading a state kept the
  progress from before the load (`CoreService.java:922-925,1403-1430,1503-1508` at `7aa5bdb3e`).
  That code and `RetroAchievementsManager.serializeProgress` and `deserializeProgress` are gone.
  The glue's `ra_glue_serialize_progress` and `ra_glue_deserialize_progress` stay exported, and
  the app no longer calls them (`ra_glue.h:83-88`). The old `ra_progress` file stays on devices,
  unread.
- OBSERVED: the app's file prompts list the files of a folder but not its folders
  (`Prompt.java:337-340`, used by `CoreFragment.java:791,817`), so the progress files are not
  listed among the save states. Copies to and from external storage copy subfolders too
  (`FileUtil.java:359-366,567-578`), and so does the Google Drive backup
  (`DriveServiceHelper.java:421-440`). When the auto-save cleanup deletes an old auto save, it
  also deletes that state's progress file (`GameDataManager.java:124-125,135-139`).
- DERIVED: Resume now brings back the counts saved with its auto save, and a session from
  power-on starts every count from zero, in either mode. A state saved by an older build has no
  progress file, and neither has one saved before the game has loaded, as in a session whose
  sign-in failed, so loading either starts from zero. After an M64P save whose file write failed,
  the progress file holds the CRC-32 of RAM that no state file holds, so an older state left under
  that name also starts from zero. Loading is refused in hardcore, so apart from Resume, which
  runs in softcore, a save state's counts come back only in softcore. The frames that run between
  a Resume load and the game's load evaluate none of the game's achievements, as
  `rc_client_do_frame` needs a loaded game (`rc_client.c:6436`), so the counts Resume brings back
  miss those frames, and a reset condition that is true in them does not reset the counts. They
  run until the sign-in and the game's load have finished.
- OBSERVED (host C test): `tools/tests/ra-session-test.c` has six more runs with a fourth
  achievement, which unlocks after five frames in which a RAM byte is 1. After three such frames
  a save must write the progress file; one more frame, a load of that state and another frame
  must not unlock it, since the load took the count back to three; after a load with other RAM
  the count starts from zero, and the achievement first needs a frame in which it is false
  (`trigger.c:332-344`). A state loaded before the game has loaded must bring back its count once
  the game has loaded, but not when the core was reset in between, nor when a state with no
  progress file was loaded after it, which leaves the count at zero. In a session whose game never
  loads, a state saved after loading one with three hits must get no progress file, and a later
  session that loads it must not unlock the achievement in the two frames that three carried hits
  would need. Without a client, a save must remove an earlier save's progress file. All fifteen
  runs pass with GCC 16.2.1 and clang 23.1.1 through `tools/test-ra-session.sh`, which now links
  zlib, and in a scratch build under clang ASan, UBSan, LeakSanitizer and ThreadSanitizer (UBSan
  reports only the two vendored sites named under "On-screen notices"). In that scratch build,
  copies of the glue whose hooks do nothing fail 5 checks in 3 runs; one without the CRC check
  fails only the zero-count check; one that drops progress loaded before the game has loaded, and
  one whose reset keeps it, each fail only their run's unlock check; one that keeps the old
  progress file fails only the removal check; one that writes the waiting progress into a state
  saved before the game has loaded fails only the two checks of the run whose game never loads;
  one that keeps the waiting progress when a load finds no progress file fails only the unlock
  check of the run that loads such a state second. Against the glue of `7aa5bdb3e` the test does
  not compile.
- OBSERVED (host JVM): `GameDataManagerTest` checks the path of a state's progress file. It did
  not compile before `GameDataManager.getRaProgressFile` existed and passes now.
- OBSERVED (host build): `./gradlew :app:testDebugUnitTest --rerun :app:assembleDebug` passes with
  every unit test, and `ra_glue.c` and `savestates.c` were recompiled for all four ABIs with empty
  ndk-build stderr logs. In all four linked core libraries the hooks are local symbols, not
  exported, and `crc32` comes from `libz.so`, which the library already needed. In each ABI the
  packaged library has the linked one's build ID, and `savestates_load` and `savestates_save`
  call them (NDK `llvm-objdump`).
- Not yet verified on device: a slot or a file saved during play brings its counts back when it
  loads; Resume brings back the auto save's counts; the progress files sit under `.ra-progress`
  and stay out of the file prompts; the cleanup deletes them with old auto saves; copies to and
  from external storage, and a Google Drive backup and restore, keep the `.ra-progress` folder.

### Sign-in order
- OBSERVED: a game session signs in with the saved session token first and uses the password
  only when no token is saved or the server rejects the token (`RaSignInRules.firstCredential`
  and `nextCredential`, called from `CoreService.startRetroAchievements` and
  `CoreService.onRaLoginResult`). In `fd6b452f1` the password went first and the token was tried
  only when no password was saved, with no fallback either way. The password stays in Settings.
- REFERENCE (RAWeb `ac7bf9b84d98`, `app/Connect/Actions/LoginAction.php`): a token that does not
  match the user gets `invalid_credentials` (lines 105-107, response built at 159-169; banned
  accounts get the same at 117-118). An expired token gets `expired_token`, and the server
  replaces it (121-131). Both come back as HTTP 401, because the `Status` value becomes the HTTP
  status (`app/Connect/Support/BaseApiAction.php:35-39`). An account with an unverified e-mail
  address gets 403 `access_denied` (`BaseApiAction.php:75-83`), with a password too (91-92,
  113-114).
- OBSERVED: rcheevos reads the login response's `Code` field (`rc_api_user.c:57-59`,
  `rc_api_common.c:404-409`) and turns `invalid_credentials` and `expired_token` into
  `RC_INVALID_CREDENTIALS` (-34) and `RC_EXPIRED_TOKEN` (-35) (`rc_api_common.c:304-319`,
  `rc_error.h:47-48`). The login parser returns that code (`rc_api_user.c:72-74`), the login
  callback gets it unchanged (`rc_client_server_error_message`, `rc_client.c:497-516`; callback
  `:698-708`), and `ra_glue_async_cb` passes it to Java (`ra_glue.c:183-194`). A failed HTTP
  request becomes `RC_NO_RESPONSE` (`RetroAchievementsManager.java:411-417`,
  `rc_api_common.c:360-365`). Only -34 and -35 are retried with the password.
- OBSERVED: `loadGame` is requested only after a login succeeds. rc_client results carry no
  request ID (`ra_glue_async_cb`, `ra_glue.c:183-194`), so the session makes one request at a
  time and `mRaLoginCredential` tells which login a result belongs to. A request call that returns
  false ends the session at once, as before. It returns false when `ra_glue`'s own checks refuse
  without a callback (`ra_glue.c:545-546,555-556,574-575`), when rc_client refuses, which it also
  reports through the callback (login `rc_client.c:766-775,800-813,833-846`; load
  `:2444-2461,2927-2948`), and when the request finishes before the call returns (login
  `:786-792`, load `:2900`). `onRaAsyncResult` ignores any result that arrives after that.
- DERIVED: in that last case a successful login is dropped, and the session runs without
  achievements and with enforcement released. It needs the main thread to stall for a whole HTTP
  round trip (responses arrive on the HTTP thread, `RetroAchievementsManager.java:287`), so it
  should be rare. In `fd6b452f1` the same case latched the failure but still loaded the game, so
  achievements would run with enforcement released.
- DERIVED: the password login starts from the token login's result, which is posted to the main
  thread after rc_client has cleared its login state (`rc_client.c:700-708`), so rc_client does
  not refuse it as "Login already in progress" (`:757-760`).
- OBSERVED: the token from a successful login is written with `commit()`, and only when it differs
  from the saved one. `forceExit` kills the emulation process (`CoreService.java:1532-1535`).
- DERIVED: `apply()` only starts a background write (Android `SharedPreferences.Editor.apply`
  reference), so a token saved with it could be lost when that process is killed.
- OBSERVED (host JVM): `RaSignInRulesTest` failed against the old order (password first, no
  fallback) in `savedTokenGoesFirst` and `rejectedTokenIsRetriedWithPassword`; it passes now.
- Not yet verified on device: a valid token with a wrong password signs in; a wrong token with the
  right password signs in and saves a new token; with neither a valid token nor the right
  password, enforcement releases.

### On-screen notices
- REFERENCE (RetroAchievements docs revision `361119b677dc`,
  `general/hardcore-compliance-requirements.html`, read 2026-10-19): section E requires the
  hardcore state to be "visibly indicated in the UI during play", normally by "a brief
  informational message saying what mode they're in" when the game starts. Section A requires
  Measured and Trigger flags to be "visible in the achievement list as well as displayed during
  gameplay" (see "Challenge and progress indicators"). The page does not mention unlock notices;
  they were requested for this branch, not taken from the page. This revision ("chore: rebrand
  'Softcore' to 'Casual'") renamed the other mode; the page mostly writes "casual mode".
- OBSERVED: the app's text follows that name: the mode notice ("RetroAchievements: casual mode"),
  the achievement list's summary row ("Casual: 5 of 20 unlocked"), the hardcore setting's summary
  ("Resumed games and netplay run in casual mode.") and the README. In `b67587935` they said
  softcore. The string resources were renamed with them (`ra_sessionCasual`,
  `achievements_summaryCasual`); no other locale has them.
- OBSERVED: `CoreService` shows a long toast (`showRaNotice`) in each of these cases:
  - when the game's achievements finish loading: "RetroAchievements: hardcore mode" or
    "RetroAchievements: casual mode", from `isRaHardcore()`;
  - when an achievement unlocks: "Achievement unlocked: <title> (<n> points)", or "(1 point)";
  - when the server's client warning unlocks (IDs from 101000001,
    `RetroAchievementsManager.WARNING_ACHIEVEMENT_ID`, now shared with `AchievementsDialog`): its
    title and description as one paragraph ("<title>. <description>"), so the text can wrap over
    both of the toast's lines (see the line limit below);
  - in a hardcore session, when a leaderboard attempt starts or fails: "Leaderboard attempt
    started: <title>" or "Leaderboard attempt failed: <title>", and when it submits: "Submitted
    <value> for <title>" (see "Leaderboard attempts");
  - when the server does not record an unlock or a leaderboard entry and rcheevos does not retry
    the request: "RetroAchievements did not record the unlock of <title>: <message>" or
    "RetroAchievements did not record the leaderboard entry for <title>: <message>", with the
    error text rcheevos passes on; if the glue finds no title, "RetroAchievements server error:
    <message>";
  - when an unlock or a leaderboard entry has to wait for a retry: "RetroAchievements: sending
    failed. Keep playing while it retries.", and once no retry is scheduled any more:
    "RetroAchievements: connection restored";
  - when the session ends with a terminal failure (`failRaSession`): "RetroAchievements is off for
    this session: <reason>", where the reason is rcheevos' error text (for example "Unknown
    game"), "could not start the request", "no sign-in is saved in Settings", "the game's MD5 is
    not known" or "the achievements client could not start". Offline, rcheevos' text is the app's
    own "RA network error: <message>" (`RetroAchievementsManager.java:413`), and after any other
    failure of the request "RA request error: <exception>" (`:417`); rcheevos passes either on as
    the error message (`rc_api_common.c:360-365`).
- OBSERVED: two start-up exits that ended achievements without a notice now call `failRaSession`:
  a game started without an MD5 (`CoreService.java:1333-1337`) and a failed
  `RetroAchievementsManager.initialize()` (`:1340-1344`). Both come before `mRaGameMd5` is set
  (`:1346`), so `isRaHardcore()` was already false and hardcore was never enforced; only the
  notice is new. The service reads the MD5 from its start intent (`CoreService.java:1232`); the
  gallery computes it when it scans a game (`CacheRomInfoService.java:490,534`). `initialize()`
  fails only when `rc_client_create` cannot allocate the client
  (`RetroAchievementsManager.java:152-155`; `ra_glue.c:454-458`; `rc_client.c:169-171`). Neither
  failure has been seen on a device.
- DERIVED: the mode toast shows what the app enforces. rcheevos changes its own hardcore setting
  only when the media changes (`rc_client.c:3221-3227`), and `ra_glue` never changes media, so
  `isRaHardcore()` matches rc_client's mode when the load completes.
- DERIVED: rcheevos sends the award request and then raises the unlock event for every triggered
  achievement (`rc_client.c:6185-6210`). For the server's warning the award returns early and
  submits nothing (`:4818-4821`), but the event is still raised. So the warning toast shows when
  the warning unlocks, which the server says happens a few seconds after loading (RAWeb
  `ac7bf9b84d98`, `GetAchievementSetsAction.php:493-499`), in hardcore only (see the next item).
- REFERENCE (RAWeb `ac7bf9b84d98`, `app/Connect/Actions/StartSessionAction.php:131-141`): unless
  the client has full support, the start-session answer lists the warning as earned in softcore,
  "so it only pops in hardcore" (its comment). OBSERVED: rcheevos records that list with the
  softcore bit (`rc_client.c:1810-1811`). When the game loads, an achievement already earned in
  the current mode becomes unlocked instead of active (`rc_client.c:1457-1485,1518-1525`).
  DERIVED: the warning toast shows a few seconds after a hardcore session loads, and never in
  softcore.
- OBSERVED: when an unlock or a leaderboard entry fails, rcheevos raises a server error only if
  the answer carries an error message and `rc_client_should_retry` (`rc_client.c:586-640`) does
  not ask for a retry (unlocks `:4669-4674`, leaderboard entries `:5375`), or if an allocation
  fails (`:4691,4844,5393,5485`). The error message is the `Error` field of a JSON answer or, for
  an answer that is not JSON, the page's HTML title, else its first line
  (`rc_api_common.c:270-302,391-402`). For the app's offline answer it is the app's own text
  (`:360-366`), and for an empty answer with status 504 or 521-524 a fixed text from rcheevos
  (`:368-388`); both are retried, as below. `rc_client_should_retry` asks for a retry for HTTP
  status 502, 503, 504, 429 and 521-525, for an answer without a body, and for the app's answer
  to a request that failed without an HTTP answer, offline or otherwise (status -2,
  `RetroAchievementsManager.java:411-417`; `rc_client.c:625-627`), and an answer without an error
  message is retried as well. So an error page with any other status, for example 500 or 403, ends
  the request with a server error instead of a retry. The app passes on only HTTP statuses and -2
  (`RetroAchievementsManager.java:405,412,416`). The first retry is sent at
  once, later ones after 1 s, doubling up to 120 s, with no limit on the count
  (`rc_client.c:4675-4706,5377-5408`). While a retry is scheduled, rcheevos counts as
  disconnected; when that changes, `rc_client_idle` raises a disconnected or a reconnected event
  (`rc_client.c:536-584,6534-6535`). rcheevos recomputes that state when it schedules a retry
  and after each answer to a request it has retried (`rc_client.c:4704,4753-4754,5406,5423-5424`).
  A retry that has been sent is no longer scheduled (`rc_client.c:6519-6523`).
- DERIVED: for one request, a failure that the immediate retry fixes raises neither event, and
  an outage raises one "disconnected" and one "reconnected", not one per retry; the host test
  below checks the outage. With two requests waiting, "reconnected" can come while the second is
  still on its way, and a new "disconnected" follows if that one fails again.
- OBSERVED: the glue passes these on. `ra_server_error_title` looks up the title of the
  achievement or leaderboard that a server error names, by the request name rcheevos gives
  (`ra_glue.c:276-294`), and the server-error JSON carries it as `"title"`, empty when the lookup
  finds nothing (`:348-362`). The disconnected and reconnected events, which
  `ra_build_event_json` dropped before, arrive as `{"type":"disconnected"}` and
  `{"type":"reconnected"}` (`:364-370`). `CoreService.onRaEvent` shows the notices
  (`CoreService.java:1396-1424,1439-1451`).
- OBSERVED: the glue looks the title up in its event handler, on the thread that raises the
  server error. A refused answer (`rc_client.c:4669-4674`, `:5375`) and a failed allocation for a
  retry (`:4691,5393`) raise it in the response callback, which runs on the HTTP thread that
  delivers the answer (`RetroAchievementsManager.java:287`). When the glue has no free request
  slot, it answers at once on the sending thread with HTTP 503 (`ra_glue.c:216-234`), which
  rcheevos always retries (`rc_client.c:593-595`), so there only a failed allocation for that
  retry can raise one. While the client shuts down, the glue answers with status -1 instead, which
  rcheevos does not retry, so an unlock or leaderboard entry ends with a server error there (see
  "Network requests and shutdown"). A failed allocation when a frame starts a request
  (`rc_client.c:4844,5485`) raises it on the emulator thread, after the frame has released
  rcheevos's mutex (`rc_client.c:6185-6186,6359,6465-6467`).
  `rc_client_get_achievement_info` takes that mutex, which is recursive, to update the
  achievement's display data (`rc_client.c:4485-4531`; `rc_compat.c:230`);
  `rc_client_get_leaderboard_info` takes none (`rc_client.c:4900-4922`). The glue holds no lock of
  its own while rcheevos handles an answer (`ra_glue.c:254-266`), and `ra_glue_shutdown` waits
  until that handling ends (`ra_glue.c:479-486`). The disconnected and reconnected events come
  from the emulator thread. `RetroAchievementsManager` posts every event to the main thread
  (`RetroAchievementsManager.java:303`).
- DERIVED: both lookups read the loaded game, which stays loaded while the event is handled,
  because the app never unloads it during a session (see "Threads in the glue", also derived).
- DERIVED: a request that waits for a retry stays in memory only, and nothing retries it after the
  game closes (see Open items). So the disconnected notice asks the player to keep playing.
- OBSERVED (host C test): `tools/tests/ra-session-test.c`, which `tools/test-ra-session.sh` runs,
  has three more runs. The fake server's game also has a leaderboard that starts and submits when
  a second RAM byte becomes 1. A refused unlock, and a refused leaderboard entry in a hardcore
  session (rcheevos submits entries only in hardcore, `rc_client.c:5464-5467`), must each arrive
  as one server-error event with the request name, the server's message, the result
  `RC_API_FAILURE` that rcheevos reports for a JSON answer whose `Success` is false
  (`rc_client.c:499-501`), the ID and the title; no "disconnected" may follow the refused unlock
  on the next frame. An unlock whose first two answers are HTTP 503 must raise "disconnected" on
  the next frame, be sent again one second later on rcheevos's clock, which the test replaces,
  and raise "reconnected" on the frame after that answer succeeds. Against the previous glue, 5
  checks in these three runs fail: both title checks and the disconnected and reconnected checks.
  Every run in the file passes now, with GCC 16.2.1 and with clang 23.1.1, and under clang's
  AddressSanitizer and UndefinedBehaviorSanitizer with no memory error. UBSan reports two sites in
  vendored rcheevos, `condset.c:236` and `value.c:105` (the leaderboard's value), both in the
  null-pointer offset idiom of `RC_ALLOC_WITH_TRAILING` (`rc_internal.h:121-124`); they predate
  this change.
- REFERENCE (AOSP `android-14.0.0_r1`, SystemUI): a text toast shows at most two lines and ends
  longer text with an ellipsis (`res/layout/text_toast.xml:44-45`); only apps that target an API
  level below 31 get more (`SystemUIToast.java:187-189`). OBSERVED: this app targets 34
  (`build_common/version_common.gradle:10`). DERIVED: a long achievement title, the server's
  warning or a long error text from the server or rcheevos can be cut. HYPOTHESIS: how much is cut
  on the Retroid Pocket 6 depends on the toast width and the font scale.
- OBSERVED (host build): `./gradlew :app:testDebugUnitTest --rerun :app:assembleDebug` passes, and
  `aapt2 dump resources` shows the new strings in the debug APK. There is no JVM test for the
  notices: they need Android's `Toast`, resources and `org.json`.
- OBSERVED (device, 2026-10-20): the softcore mode toast showed at each of the 13 starts that
  opened the game (see "Device verification" for build `3.0.338 06aedfe1`). Not yet verified on
  device: the hardcore mode toast and the warning a few seconds after it, one toast per unlock,
  whether long notices are cut at two lines, the failure toast for a ROM RetroAchievements does
  not know and for a wrong password with no valid saved token, and the sending-failed and
  connection-restored notices when the network is off for an unlock and comes back while the game
  runs. A refused unlock is hard to cause on purpose.

### In-game achievement list
- OBSERVED: with RetroAchievements enabled in Settings, the in-game menu has an "Achievements" item
  (`game_drawer.xml`; `GameActivity` removes it otherwise). It opens `AchievementsDialog`, a
  read-only list titled with the game name: a summary row ("Hardcore: 5 of 20 unlocked" and the
  points), the server's warning achievement if there is one, then one row per achievement with its
  title, description, points and status ("Unlocked", "Locked" or "Unsupported", followed by any
  progress or active challenge, see "Challenge and progress indicators"). When the game has more
  than one active achievement set, each set's rows start with a heading row showing the set's
  title. The only button is OK; nothing in the dialog unlocks, submits or edits. If no game is
  loaded, the dialog shows one line instead.
- OBSERVED: `ra_glue_get_achievements_json` used to mark an achievement unlocked when either unlock
  bit was set, so under hardcore a softcore-only unlock showed as unlocked although the summary
  did not count it. It now tests the bit for the current mode, the same bit
  `rc_client_get_user_game_summary` counts (`rc_client.c:946-964`). The achievement `state` is not
  used: a DISABLED achievement keeps that state even when unlocked (`rc_client.c:1458-1485`).
- OBSERVED: the summary JSON has a new `"hardcore"` field from `rc_client_get_hardcore_enabled`.
  `ra_glue_get_hardcore()` returns the requested flag, the mode the app enforces
  (`ra_glue.c:49-52`).
- REFERENCE (RAWeb `ac7bf9b84d98`): `GetAchievementSetsAction.php:493-552` puts the client warning
  achievement first in the first set, and its comment says it unlocks itself a few seconds after
  loading; in softcore the server's start-session answer lists it as already earned (see
  "On-screen notices"). For this client its description is "Hardcore unlocks cannot be earned
  using this emulator." (`GetAchievementSetsAction.php:540`). OBSERVED: rcheevos leaves IDs from
  `101000001` up out of the summary (`rc_client.c:954`), and the dialog takes the same IDs as
  warning rows, whatever their state, which skip its totals
  (`AchievementsDialog.java:95-97,110-115`). DERIVED: the dialog lists the warning as a warning
  row, not an achievement, in both modes.
- OBSERVED: `rc_client_get_user_game_summary` counts only the game's main achievement set
  (`rc_client.c:1031`), but `rc_client_create_achievement_list` lists every active set
  (`rc_client.c:4397-4431`). So the dialog counts its totals from the achievements in the list and
  reads only the mode and the game title from the summary JSON. For a game that also loads a bonus
  set, the totals cover every set. Set headings and warning rows are not counted.
- OBSERVED: each achievement in the list JSON also has `"unsupported"` and `"set"`, taken from the
  rcheevos bucket it is listed in (`ra_glue.c:720-725`). `"unsupported"` is 1 in the "Unsupported"
  bucket. rcheevos puts an achievement there when its logic cannot be parsed
  (`rc_client.c:2113-2116,2126-2129`), or when its logic uses a bad address
  (`rc_client.c:1293-1314`). When the game loads, rcheevos checks every address once: an address
  past the last region of the console's memory map is bad without a read, and so is one the memory
  read callback refuses (`rc_client.c:1346-1370`, called at `:1861`). During play, a failed read
  makes its address bad, unless the read followed a pointer
  (`rc_client.c:5864-5876,5895-5896,5937-5938`). The bucket never changes afterwards
  (`rc_client.c:4100-4101`). `ra_glue_read_memory` refuses an address only while `g_mem_base` is
  NULL, or at 16 MB and above (`ra_glue.c:119-123`); "Memory reads" describes what it refused
  before. The dialog shows an unsupported achievement as "Unsupported", or as "Unlocked" if the
  player already earned it in this mode (`rc_client.c:1686-1689`). `"set"` is the set's title from
  `rc_client_get_subset_info` when the game has more than one active set, and empty otherwise,
  because rcheevos sets a bucket's `subset_id` only then (`rc_client.c:4297-4301,4420`).
- OBSERVED: with the lock-state grouping the glue requests (`ra_glue.c:699-701`), rcheevos lists
  all of one set's buckets before the next set's (`rc_client.c:4397-4431`), and the dialog adds a
  heading row whenever the set title changes. DERIVED: two sets with the same title in a row would
  share one heading.
- OBSERVED: long titles wrap. The shared row layout keeps the title on one scrolling line
  (`list_item_two_text_icon.xml:36-38`) and other dialogs use it, so `AchievementsDialog` turns that
  off for its own rows.
- OBSERVED: the glue returns 0 both when it fails and when the list does not fit the buffer
  (`ra_glue.c:702-703,786-790`). The list used to be read into a fixed 128 KB buffer, so a longer
  list showed "No achievements are loaded". `RetroAchievementsManager.readText` now grows the
  buffer fourfold up to 2 MB for the list (`ACHIEVEMENTS_BUFFER_MAX`). "No achievements are loaded"
  now shows only when no game is loaded; a loaded game whose list cannot be read (over 2 MB, or
  JSON the dialog cannot parse) shows "Could not read this game's achievement list."
- OBSERVED (host JVM): `RetroAchievementsManagerTest` uses a fake getter that, like the glue's JSON
  getters, writes nothing and returns 0 when the text and its terminating NUL do not fit. Its 1 MB
  case (`textLargerThanTheFirstBufferIsRead`) failed against the single 128 KB read and passes now.
- OBSERVED (host build): `./gradlew :app:testDebugUnitTest --rerun :app:assembleDebug` passes.
  `ra_glue.c` was recompiled for all four ABIs with no compiler warnings (the ndk-build stderr logs
  are empty). The packaged `libmupen64plus-core.so` (all four ABIs) has the summary format with
  `"hardcore"` and the list format with `"unsupported"` and `"set"`, and still exports both JSON
  functions. There is no JVM test for the dialog: local unit tests get Android's stubbed
  `org.json`.
- OBSERVED (device, 2026-10-20): in softcore the dialog showed the game's title, the summary
  "Casual: 0 of 97 unlocked", the warning row, and "Locked" rows with descriptions and points (see
  "Device verification" for build `3.0.338 06aedfe1`). Not yet verified on device: the item shows
  only with RetroAchievements enabled; in hardcore the dialog matches the loaded game and still
  lists the warning row; a game with a bonus set shows set headings; long titles wrap.

### Challenge and progress indicators
- REFERENCE (RetroAchievements docs): a Trigger flag exists "so a challenge indicator can be shown
  on the screen" (`developer-docs/flags/trigger.html`, revision `9a5f33c`), and a Measured flag
  "adds a progress bar to the achievement overlay" (`developer-docs/flags/measured.html`, revision
  `6226ebf`). Section A of the compliance page wants both in the list and during play (see
  On-screen notices).
- OBSERVED: rcheevos raises a challenge-indicator show event when an active achievement's trigger
  becomes primed, and a hide event when it stops being primed (`rc_client.c:6082-6090`) or the
  game unloads or resets (`:3123,4870`). Restoring saved progress raises a hide or a show where
  the restored state differs (`:6782,6821`), and a hardcore switch hides the indicator of an
  achievement already unlocked in the new mode (`:1492`). When measured values change, it raises
  a progress event for the achievement closest to unlocking among them: "show" while the
  indicator is hidden, "update" while it is shown (`rc_client.c:6052-6075,6128-6146`); for a
  percentage, only when the percentage changes. Two seconds after the last change on its clock,
  it raises "hide" without an achievement (`rc_client.c:6109-6126,6145`). The progress text is
  "value/target", or "N%" for a percentage; it stays empty while the value is 0, and below 1% for
  a percentage (`rc_client.c:4121-4149`). rcheevos refreshes it before it raises the progress
  event (`rc_client.c:6190-6193,6399-6422`).
- OBSERVED: the glue passes these on, where `ra_build_event_json` used to drop them: challenge_show
  and challenge_hide with the achievement's ID and title, progress_show and progress_update with
  the progress text as well, and progress_hide with no other field (`ra_glue.c:314-346`). Each
  achievement in the list JSON also has `"progress"`, the same text, and `"challenge"`, 1 while
  it is primed (`ra_glue.c:734-742,747,770`): rcheevos refreshes both when it builds the list
  (`rc_client.c:4306`) and puts a primed achievement in the "Active Challenges" bucket
  (`rc_client.c:4151-4152`). The dialog adds ", progress 3/10" and ", challenge active" to a row's
  status (`AchievementsDialog.java:120-126`).
- OBSERVED: `RaIndicatorOverlay` is the last child of the game `FrameLayout` in both game
  layouts, at the top centre (`game_activity.xml`, `game_activity_landscape.xml`).
  `GameActivity` starts it when RetroAchievements is enabled and stops it when the activity is
  destroyed (`GameActivity.java:414-417,818-820`). It shows "Challenge active: <title>" for each
  primed achievement, in the order they became primed, and "<title>: <progress>" while rcheevos
  shows progress, in white on a dark rounded background. An activity created for a session that
  is already running starts with the active challenges from the achievement list. Nothing makes
  the overlay or its lines clickable or gives them a touch listener (`RaIndicatorOverlay.java`).
  `CoreService.onRaEvent` does not log progress updates (`CoreService.java:1397-1400`).
- DERIVED: Android draws the overlay above the game screen's other views, and passes each touch
  on to the views below it, because nothing in the overlay handles one. An FPS counter placed at
  the top centre (`GlobalPrefs.java:626-628`) can be hidden under the lines while they show. A
  measured hit count can change on every frame, so logging each progress update could add a log
  line per frame.
- OBSERVED (host JVM): `RaIndicatorStateTest` covers `RaIndicatorState`, which holds what the
  overlay shows; its tests pass. A copy with two deliberate faults fails two of them. Copies
  that keep the challenges in a `HashMap` or in a `TreeMap` in either order, or that list the
  titles sorted either way or reversed, fail the order test (javac 17.0.20.1, JUnit 4.13.2).
- OBSERVED: `GameActivity` gets the manager with `getInstance()` (`GameActivity.java:417`), which
  loads the core library (`RetroAchievementsManager.java:95-107,119`). Its other callers in the
  emulation process are `CoreService` methods (`CoreService.java:384,406,1339,1388,1461`). In
  Settings, Connect gets it (`LoginPreference.java:82,108`) and calls `initialize()` (`:83`) in
  the app's main process (`preferences_ra.xml:25`; `RaPrefsActivity.java:66`;
  `AndroidManifest.xml:208-213`). Only `CoreInterface` loads `ae-bridge` in the Java code
  (`CoreInterface.java:78,129`); its class does so before its field loads the core (`:128`). No
  packaged library lists `libae-bridge.so` as a dependency (all four ABIs, NDK `llvm-readelf`).
  Only `CoreService`, which runs in the emulation process (`AndroidManifest.xml:415-418`), creates
  a `CoreInterface`, in a static field (`CoreService.java:210`). Other code uses `CoreService` only
  as a type, a class literal (`ActivityHelper.java:468,491`) or a string constant
  (`CoreService.java:137-140`), which the compiler copies into the caller (`javap` of
  `GalleryActivity`).
- DERIVED: none of those uses initializes `CoreService`; creating the service does. So in the
  emulation process the manager used to load the core only after `ae-bridge` had loaded, and from
  `GameActivity` it can now load it first. In the main process nothing loads `ae-bridge`, so
  Connect has always loaded and called the core without it, and it signed in that way on a device
  with an earlier build (see the top of this file).
- OBSERVED (host build): in all four ABIs, the packaged core library lists `libSDL2_net.so`,
  `libz.so`, `libc.so`, `libm.so` and `libdl.so` as its dependencies; every symbol it imports
  resolves from them (116, 131, 124 and 116 imports for arm64-v8a, armeabi-v7a, x86 and x86_64,
  against the NDK's API 23 stubs), none is weak, and it has no initializer (no `DT_INIT` or init
  array). `libSDL2_net.so` needs only `libc.so`, `libm.so` and `libdl.so` and resolves from them
  (NDK `llvm-readelf` and `llvm-nm`). DERIVED: loading the core before `ae-bridge` cannot fail for
  a missing symbol, and the load runs none of the core's code.
- OBSERVED (host C test): `tools/tests/ra-session-test.c` has a sixth run with two more
  achievements: one whose trigger is primed while a RAM byte is 1, and one that measures another
  byte up to 10. Setting and clearing the first byte must each raise exactly one event,
  challenge_show and then challenge_hide, and change the list's `"challenge"`; setting the second
  byte to 3 and then 4 must raise progress_show "3/10" and progress_update "4/10", the list must
  show "4/10", and two seconds later on the test's clock progress_hide must follow, with no server
  request. With the glue from before the indicators (`f0711bb3d`), that run fails 15 checks and
  the five runs before it pass; with the current glue every run passes, with GCC 16.2.1 and clang
  23.1.1, and under clang ASan and UBSan with no memory error.
- OBSERVED (host build): `./gradlew :app:testDebugUnitTest --rerun :app:assembleDebug` passes with
  every unit test. `ra_glue.c` was recompiled for all four ABIs with empty ndk-build stderr logs,
  the packaged core libraries have the new event and list formats, and the APK has the overlay in
  both game layouts and the four new strings.
- Not shown: achievement badges (the overlay shows text only and downloads no images) and a
  progress bar. The overlay also shows leaderboard attempts (see "Leaderboard attempts").
- Not yet verified on device: a primed challenge shows its line and loses it again; a measured
  achievement's progress shows and goes about two seconds after the last change; the lines do
  not block the controls below them; the frame rate holds while a measured value changes on every
  frame; the list shows progress and "challenge active"; a game activity created again during a
  session shows the active challenges.

### Leaderboard attempts
- REFERENCE (compliance page revision `361119b677dc`): "Rich Presence and Leaderboards: Must
  function correctly" (section A); in hardcore they "cannot be disabled", and "Disabling
  leaderboard popups is okay to support for either mode" (section B).
- OBSERVED: rcheevos runs leaderboards only in hardcore, unless the client allows them in softcore
  (`rc_client.c:1604-1609,6454`), which the glue does not. A started attempt gets a tracker that
  holds its formatted value. Attempts whose value has the same definition and format share one
  tracker, so a tracker carries an ID and a value but no leaderboard (`rc_client.c:5201-5260`;
  `rc_client.h:665-668`). A tracker's ID is its place in the game's list of trackers, and a
  tracker that no attempt uses any more is used again (`rc_client.c:5206-5212,5237-5248`).
  rcheevos raises a tracker update only when the value changes (`:5273-5281`), and hides a tracker
  when the last attempt that uses it ends (`:5262-5271`), when the core is reset (`:5515,6667`)
  or hardcore is turned off (`:1666,6981-6982`), as both stop every running attempt, and when the
  game unloads (`:3130-3133,3183,3204`). When an attempt submits, it first puts the submitted
  value in the tracker (`:6278-6288`), and it raises tracker events before leaderboard events,
  "so formatted values are updated for leaderboard events" (`:6403-6405`).
- OBSERVED: the glue passes these on, where `ra_build_event_json` used to drop the tracker events
  and leave the value out of a submission: leaderboard_started and leaderboard_failed with the
  leaderboard's ID and title, leaderboard_submitted with its `tracker_value` as well,
  tracker_show and tracker_update with the tracker's ID and value, and tracker_hide with its ID
  (`ra_glue.c:380-424`).
- OBSERVED: `CoreService.onRaEvent` shows a long toast for each leaderboard event: "Leaderboard
  attempt started: <title>", "Leaderboard attempt failed: <title>" and "Submitted <value> for
  <title>" (`CoreService.java:1404-1413`; `strings.xml:287-289`). It logs neither progress nor
  tracker updates, which can come every frame (`CoreService.java:1397-1400`).
- OBSERVED: `RaIndicatorOverlay` shows "Leaderboard: <value>" for each tracker, between the
  challenge lines and the progress line, in the order the trackers were shown; an update changes
  its line in place (`RaIndicatorOverlay.java:50-53,96-106,156-174`;
  `RaIndicatorState.java:31-42`; `strings.xml:291`). The lines use a fixed-width font, and the
  overlay reuses them rather than making new ones for each update.
- DERIVED: a game screen created again while an attempt runs shows the attempt's value only when
  the value next changes. rcheevos's public leaderboard information has no tracker ID
  (`rc_client.h:650-658`), so the overlay cannot list the running trackers
  (`RaIndicatorOverlay.java:66-68`). No notice shows the rank: the server returns it with the
  submitted entry, and the glue does not pass it on. The leaderboard toasts share Android's
  five-toast limit (see Open items).
- OBSERVED (host C test): the ninth run in `tools/tests/ra-session-test.c` has a second
  leaderboard that starts, is cancelled and submits on three RAM bytes, with its value in a
  fourth. In a hardcore session, starting it must raise leaderboard_started and tracker_show "3";
  a new value, exactly one tracker_update "4"; a value of 5 set on the frame that submits,
  leaderboard_submitted with the value "5", tracker_hide, and exactly one entry of 5, not refused;
  a second attempt that starts and is then cancelled, leaderboard_started, leaderboard_failed and
  tracker_hide; and ending the session during a third attempt, tracker_hide. Against the glue of
  `ea136d07a` that run fails 12 checks and the other fourteen runs pass. In copies of the current
  glue with one fault each, an update sent as tracker_show fails one check, a missing
  tracker_hide fails seven, and an empty submitted value fails one, all in that run, with GCC
  16.2.1 and clang 23.1.1. In copies of the test, whose server answers that entry with status 200,
  malformed JSON or an empty body gives rcheevos no error text, so rcheevos sends the entry again
  and only the check that it is sent once fails; a refusal, or an HTML error page whose title
  rcheevos takes as the error text, fails only the check that no server error is raised
  (`rc_client.c:5371-5381`; `rc_api_common.c:270-301,368-392`). After a failed answer with a
  status that rcheevos retries, such as 503, it sends the entry again whether or not the answer
  has error text, as described under "On-screen notices". All fifteen runs pass with both
  compilers and under clang ASan, UBSan, LeakSanitizer and ThreadSanitizer (UBSan reports only
  the two vendored sites named under "On-screen notices").
- OBSERVED (host JVM): `RaIndicatorStateTest` has two more tests, 8 in all: a tracker keeps its
  place when its value changes, and hiding one tracker leaves the others. They did not compile
  before `RaIndicatorState` had trackers. Copies that keep the trackers sorted by ID, that move an
  updated tracker to the end, or that hide the wrong tracker each fail one of them (javac
  17.0.20.1, JUnit 4.13.2).
- OBSERVED (host build): `./gradlew :app:testDebugUnitTest --rerun :app:assembleDebug` passes with
  every unit test. `ra_glue.c` was recompiled for all four ABIs with empty ndk-build stderr logs,
  each packaged core library has the linked build ID and the new event formats, and the APK has
  the four new strings (`aapt2`).
- Not yet verified on device: a hardcore attempt shows its start, its value as it changes, and its
  submission or failure; the value's line goes when the attempt ends; the frame rate holds while
  a timer changes every frame.

### Open items (not fixed here)
- REFERENCE (AOSP `android-14.0.0_r1`, `NotificationManagerService.java`): Android refuses a new
  toast from an app that already has five queued, counting every toast of the app
  (`:384,3365-3377`), and shows queued toasts one at a time, a long one for 3.5 seconds
  (`:399,8648-8653,8763`). OBSERVED: the RetroAchievements notices share that limit with the app's
  other toasts, including the hardcore "disabled" toasts, because `Notifier.showToast` makes a new
  toast on each call (`Notifier.java:95`; `CoreService.java:422,540,555,588,882`). DERIVED: toasts
  beyond five are lost, so several unlocks on the same frame, or an unlock that arrives while
  repeated taps on a blocked control fill the queue, go unannounced. A "disabled" toast that
  follows a notice waits for it, up to 3.5 seconds for each notice ahead of it. The achievement
  list still shows every unlock.
- DERIVED (predates this branch, from `81ac3fb74`): the emulation job shows "Hardcore mode: cheats
  are disabled" from its own thread (`CoreService.java:882`), so on Android 6 to 10 that toast
  never shows (see the toast REFERENCE under "Save states and Resume under hardcore"). The cheats
  are still refused. Found in the review of the resume notice.
- DERIVED: an unlock or a leaderboard entry that still waits for a retry when the game closes is
  never sent. rcheevos keeps it only in memory: unloading the game removes only the game's ping
  from the scheduled work, and `rc_client_destroy` frees the client without running the rest
  (`rc_client.c:191-227,3139-3209`). The app destroys the client when the session ends
  (`CoreService.java:1306,1390`; `ra_glue.c:491`). The progress kept with a save state holds only
  the runtime state of active achievements (`rc_client.c:6762,6825-6826`), so a later session
  starts that achievement locked again, from the server's unlocks. REFERENCE (compliance page
  revision `361119b677dc`, section A): "Unlocks created while offline must be securely cached and
  sync to RetroAchievements when connectivity returns." Not seen on a device.
- DERIVED (predates this branch): frame advance has no netplay check. A controller button mapped
  to it reaches the core during netplay (`PeripheralController.java:349-351`;
  `CoreFragment.java:465-473`; `CoreService.java:577-583`, which refuses it only under hardcore,
  and netplay runs in softcore), and the core pauses after one frame (`main.c:510-515,859-863`).
  The core refuses to toggle the pause while netplay is active (`main.c:481-482`), so resuming
  that way fails; another frame advance runs one more frame and pauses again. Not seen on a
  device.
- DERIVED: an achievement list built while a game finishes loading can be written past the space
  rcheevos allocated for it, if the game's set has an address outside rcheevos's N64 map (see
  "Threads in the glue"). A lock in the glue, taken by `ra_glue_do_frame`, `ra_glue_idle` and the
  list getter, would prevent it. It would have to be taken first, never while holding rcheevos's
  mutex, the glue's request lock (`ra_glue.c:55`) or a Java monitor, and nothing called under it
  may call the list getter. Holding it only around `rc_client_create_achievement_list`, not
  while the JSON is built, limits how long a frame can wait for it. Not seen on a device.
- Not audited on this branch: the rest of the compliance page. Its save-state and resume rules
  (section B) are covered under "Save states and Resume under hardcore", and its hit-count
  recommendation (section A) under "Achievement progress kept with save states".
