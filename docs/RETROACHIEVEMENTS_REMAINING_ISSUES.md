# RetroAchievements — remaining issues

Status: merged to `master` via PR #10 (`af7dd84f0`).
Confirmed working on device: Connect sign-in ("Signed in as pwnedbygary", session token
persisted), hardcore gates for save/load state, slots, GameShark and cheats (deny toast),
and denied actions no longer announce first.

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
- Overwrite confirmation is skipped when hardcore would deny the save: the UI asks the service
  via `blockIfRaHardcore()` before prompting.
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
  (`RetroAchievementsManager.java:57`). `CoreService.onRaEvent` logs each event's JSON; in
  `ff249b18a` it logged only the event type.
- DERIVED: hardcore unlocks are recorded as softcore until RetroAchievements approves this client.
  The server's warning achievement is shown on screen when it unlocks (see "On-screen notices")
  and in the in-game achievement list. The server's separate `Warning` text (RAWeb
  `ac7bf9b84d98`, `GetAchievementSetsAction.php:557`) is never shown, because rcheevos does not read
  that field. Not yet verified on device: that award requests reach the server and come back as
  softcore.

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
  kind of achievement that could still unlock. HYPOTHESIS: it was the achievement event in the
  2026-09-21 device notes.
- OBSERVED: the callback now treats the address as an offset into RDRAM. It refuses an address
  only while `g_mem_base` is NULL or at 16 MB and above, and reads from
  `mem_base_u32(g_mem_base, MM_RDRAM_DRAM + address)` (`ra_glue.c:86-111`).
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
  responses on its HTTP thread (`RetroAchievementsManager.java:321,358`). `g_mem_base` is NULL
  until `CoreStartup` (`main.c:111`; `frontend.c:105`), which the service runs on its handler
  thread (`CoreService.java:775`) after other start-up work, while the sign-in starts from
  `onStartCommand` (`CoreService.java:1248,1370`) and the game loads as soon as it succeeds
  (`CoreService.java:1533`); nothing orders the two. `CoreShutdown` frees that memory on the
  handler thread (`CoreService.java:947`; `CoreInterface.java:738-740`; `frontend.c:135-136`).
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
- OBSERVED: `ra_glue_create` now turns background memory reads off (`ra_glue.c:365-368`). rcheevos
  then queues the rest of the game load, which applies the server's unlocks, checks the addresses
  and reports the result (`rc_client.c:1787-1923`, queued at `:1936-1950`), and runs it from
  `rc_client_idle` (`:6507-6531`), which `rc_client_do_frame` calls at its end even before a game
  is loaded (`:6490`). The emulator calls `ra_glue_do_frame` from `new_vi` (`main.c:985`) on the
  handler thread, between `CoreStartup` and `CoreShutdown`. The load result now comes from that
  thread too, and the Java side still posts it to the main thread
  (`RetroAchievementsManager.java:384-386`). DERIVED: rich presence, which rcheevos reads for each
  ping (`rc_client.c:5745-5752`), is read there as well.
- OBSERVED: `ra_glue_idle` would run the same queued work (`ra_glue.c:404-408`), and rcheevos
  expects it to be called while emulation is paused (`rc_client.h:882-883`). Nothing calls its
  Java wrapper, `RetroAchievementsManager.idle()`, and while emulation is paused the emulator
  thread stays in the core's pause loop (`main.c:952-964`). The comments keep it on the emulator
  thread with `ra_glue_do_frame` (`ra_glue.c:10-16,365-367`), and the wrapper's javadoc says why
  nothing calls it (`RetroAchievementsManager.java:308-311`).
- OBSERVED (host C test): `tools/test-ra-memory.sh` builds the production `ra_glue.c` and
  `memory.c` and reads five addresses in each region of rcheevos's N64 map, one byte and, where
  they fit, four bytes at a time, in the full and the compressed memory base modes. It expects the
  bytes stored at `mem_base_u32(base, MM_RDRAM_DRAM)`, at the offset that the region's real address
  gives (a KSEG0 address: physical = real - `0x80000000`), and checks that no read goes past the
  16 MB RDRAM area. Against the previous callback all 54 reads failed; all pass now, with GCC
  16.2.1 and with clang 23.1.1.
- OBSERVED (host C test): `tools/test-ra-session.sh` links the production `ra_glue.c` and
  `memory.c` with the rcheevos sources the core builds, and answers rcheevos's requests from a fake
  server. The game's one achievement reads the RAM byte at address `0x10`. The test checks that the
  achievement is active after two frames, and that it unlocks and an unlock request is sent when
  the byte goes from 0 to 1. This unlock runs once with the core's memory created before the game
  loads and once after; the test's other runs are under "On-screen notices". Both pass now, with
  GCC 16.2.1 and with clang 23.1.1. With the previous callback both fail; with the new callback
  but background reads on, the second fails: the achievement is not active and nothing unlocks.
- OBSERVED (host build): `./gradlew :app:testDebugUnitTest --rerun :app:assembleDebug` passes, and
  `ra_glue.c` was recompiled for all four ABIs with empty ndk-build stderr logs. In the linked arm64
  and x86_64 libraries, `ra_glue_read_memory` compares the address only with 16 MB and passes it to
  `mem_base_u32` unchanged, and `ra_glue_create` turns background memory reads off.
- Not yet verified on device: the list no longer shows every achievement as "Unsupported"; an
  achievement that reads memory unlocks in softcore; under hardcore the server still records it as
  softcore until RetroAchievements approves this client; the mode notice appears once the game
  runs, also after a slow start.

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
  Java threads; both now state the rule that follows (`ra_glue.c:10-16`;
  `RetroAchievementsManager.java:39-42`).
- OBSERVED: `ra_glue_reset` runs on the thread that resets the core (`main.c:802-816`;
  `frontend.c:359`; `CoreInterface.java:894-897`). The app resets it from `CoreService.restart()`
  (`CoreService.java:610-613`), which a confirmation dialog and the reset button mapping reach
  (`CoreFragment.java:910-916`; `GameActivity.java:1112`; `PeripheralController.java:332`).
  DERIVED: Android delivers those dialog and input callbacks on the main thread, so the reset runs
  there, not on the emulator thread that `ra_glue.h` named. Its comment now says so
  (`ra_glue.h:41-42`).
- DERIVED: the app keeps the header's rule. rcheevos unloads the game when `ra_glue_load_game`
  replaces it (`ra_glue.c:462-463`), on logout (`rc_client.c:887-888,894`), and on a reset whose
  media belongs to another game (`rc_client.c:6643-6652`). The app reaches none of these during a
  session: it loads one game per session, after the first successful sign-in
  (`CoreService.java:1533`); nothing calls `RetroAchievementsManager.logout()`
  (`RetroAchievementsManager.java:207-209`); and the glue never changes media, so the hash of the
  loaded game maps to that game (`rc_client.c:2329,2334-2335`; an unknown game keeps both at 0:
  `:2320,2427,2620-2622`). The client is destroyed by `ra_glue_shutdown` from
  `CoreService.onDestroy` (`CoreService.java:1307,1394`), on the main thread like every caller of
  the getters (`AchievementsDialog.java:50-53`; `CoreService.java:1499,1525`), and
  `ra_glue_shutdown` waits for a server response that an HTTP thread is handling
  (`ra_glue.c:382-388`). On a normal exit the service saves progress on its own thread after
  `emuStart` returns, and only then stops itself (`CoreService.java:915,918-921,981,1564-1577`);
  its other exits call `forceExit`, which kills the process right after asking the service to
  stop (`CoreService.java:1557-1561`).

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
  `CoreService.java:1148-1167`, or resume broadcast, `:240-243`, after the old activity was
  destroyed) starts the service again, via `GameActivity.onStart` (`GameActivity.java:660-666`),
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
  `:698-708`), and `ra_glue_async_cb` passes it to Java (`ra_glue.c:160-171`). A failed HTTP
  request becomes `RC_NO_RESPONSE` (`RetroAchievementsManager.java:351-356`,
  `rc_api_common.c:360-365`). Only -34 and -35 are retried with the password.
- OBSERVED: `loadGame` is requested only after a login succeeds. rc_client results carry no
  request ID (`ra_glue_async_cb`, `ra_glue.c:160-171`), so the session makes one request at a
  time and `mRaLoginCredential` tells which login a result belongs to. A request call that returns
  false ends the session at once, as before. It returns false when `ra_glue`'s own checks refuse
  without a callback (`ra_glue.c:430-431,440-441,459-460`), when rc_client refuses, which it also
  reports through the callback (login `rc_client.c:766-775,800-813,833-846`; load
  `:2444-2461,2927-2948`), and when the request finishes before the call returns (login
  `:786-792`, load `:2900`). `onRaAsyncResult` ignores any result that arrives after that.
- DERIVED: in that last case a successful login is dropped, and the session runs without
  achievements and with enforcement released. It needs the main thread to stall for a whole HTTP
  round trip (responses arrive on the HTTP thread, `RetroAchievementsManager.java:358`), so it
  should be rare. In `fd6b452f1` the same case latched the failure but still loaded the game, so
  achievements would run with enforcement released.
- DERIVED: the password login starts from the token login's result, which is posted to the main
  thread after rc_client has cleared its login state (`rc_client.c:700-708`), so rc_client does
  not refuse it as "Login already in progress" (`:757-760`).
- OBSERVED: the token from a successful login is written with `commit()`, and only when it differs
  from the saved one; `saveRaProgress` already used `commit()`. `forceExit` kills the emulation
  process (`CoreService.java:1557-1560`).
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
  gameplay"; the app shows neither yet (see Open items). The page does not mention unlock notices;
  they were requested for this branch, not taken from the page. This revision ("chore: rebrand
  'Softcore' to 'Casual'") renamed the other mode; the page mostly writes "casual mode".
- OBSERVED: the app's text follows that name: the mode notice ("RetroAchievements: casual mode"),
  the achievement list's summary row ("Casual: 5 of 20 unlocked"), the hardcore setting's summary
  ("Netplay runs in casual mode.") and the README. In `b67587935` they said softcore. The string
  resources were renamed with them (`ra_sessionCasual`, `achievements_summaryCasual`); no other
  locale has them.
- OBSERVED: `CoreService` shows a long toast (`showRaNotice`) in each of these cases:
  - when the game's achievements finish loading: "RetroAchievements: hardcore mode" or
    "RetroAchievements: casual mode", from `isRaHardcore()`;
  - when an achievement unlocks: "Achievement unlocked: <title> (<n> points)", or "(1 point)";
  - when the server's client warning unlocks (IDs from 101000001,
    `RetroAchievementsManager.WARNING_ACHIEVEMENT_ID`, now shared with `AchievementsDialog`): its
    title and description as one paragraph ("<title>. <description>"), so the text can wrap over
    both of the toast's lines (see the line limit below);
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
    own "RA network error: <message>" (`RetroAchievementsManager.java:354`), which rcheevos
    passes on as the error message (`rc_api_common.c:360-365`).
- OBSERVED: two start-up exits that ended achievements without a notice now call `failRaSession`:
  a game started without an MD5 (`CoreService.java:1336-1340`) and a failed
  `RetroAchievementsManager.initialize()` (`:1343-1347`). Both come before `mRaGameMd5` is set
  (`:1349`), so `isRaHardcore()` was already false and hardcore was never enforced; only the
  notice is new. The service reads the MD5 from its start intent (`CoreService.java:1233`); the
  gallery computes it when it scans a game (`CacheRomInfoService.java:490,534`). `initialize()`
  fails only when `rc_client_create` cannot allocate the client
  (`RetroAchievementsManager.java:151-154`; `ra_glue.c:357-361`; `rc_client.c:169-171`). Neither
  failure has been seen on a device.
- DERIVED: the mode toast shows what the app enforces. rcheevos changes its own hardcore setting
  only when the media changes (`rc_client.c:3221-3227`), and `ra_glue` never changes media, so
  `isRaHardcore()` matches rc_client's mode when the load completes.
- DERIVED: rcheevos sends the award request and then raises the unlock event for every triggered
  achievement (`rc_client.c:6185-6210`). For the server's warning the award returns early and
  submits nothing (`:4818-4821`), but the event is still raised. So the warning toast shows when
  the warning unlocks, which the server says happens a few seconds after loading (RAWeb
  `ac7bf9b84d98`, `GetAchievementSetsAction.php:493-499`).
- OBSERVED: when an unlock or a leaderboard entry fails, rcheevos raises a server error only if
  the answer carries an error message and `rc_client_should_retry` (`rc_client.c:586-640`) does
  not ask for a retry (unlocks `:4669-4674`, leaderboard entries `:5375`), or if an allocation
  fails (`:4691,4844,5393,5485`). The error message is the `Error` field of a JSON answer or, for
  an answer that is not JSON, the page's HTML title, else its first line
  (`rc_api_common.c:270-302,391-402`). For the app's offline answer it is the app's own text
  (`:360-366`), and for an empty answer with status 504 or 521-524 a fixed text from rcheevos
  (`:368-388`); both are retried, as below. `rc_client_should_retry` asks for a retry for HTTP
  status 502, 503, 504, 429 and 521-525, for an answer without a body, and for the app's offline
  answer (status -2, `RetroAchievementsManager.java:354-355`; `rc_client.c:625-627`), and an
  answer without an error message is retried as well. So an error page with any other status, for
  example 500 or 403, ends the request with a server error instead of a retry. The app passes on
  only HTTP statuses and -2 (`RetroAchievementsManager.java:346,355`). The first retry is sent at
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
  (`ra_glue.c:244-262`), and the server-error JSON carries it as `"title"`, empty when the lookup
  finds nothing (`:282-296`). The disconnected and reconnected events, which
  `ra_build_event_json` dropped before, arrive as `{"type":"disconnected"}` and
  `{"type":"reconnected"}` (`:298-304`). `CoreService.onRaEvent` shows the notices
  (`CoreService.java:1429-1444,1459-1471`).
- OBSERVED: the glue looks the title up in its event handler, on the thread that raises the
  server error. A refused answer (`rc_client.c:4669-4674`, `:5375`) and a failed allocation for a
  retry (`:4691,5393`) raise it in the response callback, which runs on the HTTP thread that
  delivers the answer (`RetroAchievementsManager.java:358`). When the glue has no free request
  slot, it answers at once on the sending thread with HTTP 503 (`ra_glue.c:192-202`), which
  rcheevos always retries (`rc_client.c:593-595`), so there only a failed allocation for that
  retry can raise one. A failed allocation when a frame starts a request (`rc_client.c:4844,5485`)
  raises it on the emulator thread, after the frame has released rcheevos's mutex
  (`rc_client.c:6185-6186,6359,6465-6467`).
  `rc_client_get_achievement_info` takes that mutex, which is recursive, to update the
  achievement's display data (`rc_client.c:4485-4531`; `rc_compat.c:230`);
  `rc_client_get_leaderboard_info` takes none (`rc_client.c:4900-4922`). The glue holds no lock of
  its own while rcheevos handles an answer (`ra_glue.c:222-234`), and `ra_glue_shutdown` waits
  until that handling ends (`ra_glue.c:382-388`). The disconnected and reconnected events come
  from the emulator thread. `RetroAchievementsManager` posts every event to the main thread
  (`RetroAchievementsManager.java:373`).
- DERIVED: both lookups read the loaded game, which stays loaded while the event is handled,
  because the app never unloads it during a session (see "Threads in the glue", also derived).
- DERIVED: a request that waits for a retry stays in memory only, and nothing retries it while
  emulation is paused or after the game closes (see Open items). So the disconnected notice asks
  the player to keep playing.
- OBSERVED (host C test): `tools/tests/ra-session-test.c`, which `tools/test-ra-session.sh` runs,
  has three more runs. The fake server's game also has a leaderboard that starts and submits when
  a second RAM byte becomes 1. A refused unlock, and a refused leaderboard entry in a hardcore
  session (rcheevos submits entries only in hardcore, `rc_client.c:5464-5467`), must each arrive
  as one server-error event with the request name, the server's message, the result
  `RC_API_FAILURE` that rcheevos reports for a JSON answer whose `Success` is false
  (`rc_client.c:499-501`), the ID and the title; no "disconnected" may follow the refused unlock
  on the next frame. An unlock whose first two answers are HTTP 503 must raise "disconnected" on
  the next frame, be sent again one second later on rcheevos's clock, which the test replaces,
  and raise "reconnected" on the frame after that answer succeeds. Against the previous glue 5
  checks fail: both title checks and the disconnected and reconnected checks. All five runs pass
  now, with GCC 16.2.1 and with clang 23.1.1, and under clang's AddressSanitizer and
  UndefinedBehaviorSanitizer with no memory error. UBSan reports two sites in vendored rcheevos,
  `condset.c:236` and `value.c:105` (the leaderboard's value), both in the null-pointer offset
  idiom of `RC_ALLOC_WITH_TRAILING` (`rc_internal.h:121-124`); they predate this change.
- REFERENCE (AOSP `android-14.0.0_r1`, SystemUI): a text toast shows at most two lines and ends
  longer text with an ellipsis (`res/layout/text_toast.xml:44-45`); only apps that target an API
  level below 31 get more (`SystemUIToast.java:187-189`). OBSERVED: this app targets 34
  (`build_common/version_common.gradle:10`). DERIVED: a long achievement title, the server's
  warning or a long error text from the server or rcheevos can be cut. HYPOTHESIS: how much is cut
  on the Retroid Pocket 6 depends on the toast width and the font scale.
- OBSERVED (host build): `./gradlew :app:testDebugUnitTest --rerun :app:assembleDebug` passes, and
  `aapt2 dump resources` shows the new strings in the debug APK. There is no JVM test for the
  notices: they need Android's `Toast`, resources and `org.json`.
- Not yet verified on device: the toasts' order and timing over the game (the mode toast after
  sign-in, the warning a few seconds later, one toast per unlock), whether long notices are cut at
  two lines, the failure toast for a ROM RetroAchievements does not know and for a wrong password
  with no valid saved token, and the sending-failed and connection-restored notices when the
  network is off for an unlock and comes back while the game runs. A refused unlock is hard to
  cause on purpose.

### In-game achievement list
- OBSERVED: with RetroAchievements enabled in Settings, the in-game menu has an "Achievements" item
  (`game_drawer.xml`; `GameActivity` removes it otherwise). It opens `AchievementsDialog`, a
  read-only list titled with the game name: a summary row ("Hardcore: 5 of 20 unlocked" and the
  points), the server's warning achievement if there is one, then one row per achievement with its
  title, description, points and status ("Unlocked", "Locked" or "Unsupported"). When the game has
  more than one active achievement set, each set's rows start with a heading row showing the set's
  title. The only button is OK; nothing in the dialog unlocks, submits or edits. If no game is
  loaded, the dialog shows one line instead.
- OBSERVED: `ra_glue_get_achievements_json` used to mark an achievement unlocked when either unlock
  bit was set, so under hardcore a softcore-only unlock showed as unlocked although the summary
  did not count it. It now tests the bit for the current mode, the same bit
  `rc_client_get_user_game_summary` counts (`rc_client.c:946-964`). The achievement `state` is not
  used: a DISABLED achievement keeps that state even when unlocked (`rc_client.c:1458-1485`).
- OBSERVED: the summary JSON has a new `"hardcore"` field from `rc_client_get_hardcore_enabled`.
  `ra_glue_get_hardcore()` returns the requested flag, which `ra_glue.c:44` calls a best-effort
  diagnostic.
- REFERENCE (RAWeb `ac7bf9b84d98`): `GetAchievementSetsAction.php:493-552` puts the client warning
  achievement first in the first set, and its comment says it unlocks itself a few seconds after
  loading. For this client its description is "Hardcore unlocks cannot be earned using this
  emulator." (`:540`). rcheevos leaves IDs from `101000001` up out of the summary
  (`rc_client.c:954`), so the dialog shows them as warning rows, not achievements, and leaves them
  out of its totals.
- OBSERVED: `rc_client_get_user_game_summary` counts only the game's main achievement set
  (`rc_client.c:1031`), but `rc_client_create_achievement_list` lists every active set
  (`rc_client.c:4397-4431`). So the dialog counts its totals from the achievements in the list and
  reads only the mode and the game title from the summary JSON. For a game that also loads a bonus
  set, the totals cover every set. Set headings and warning rows are not counted.
- OBSERVED: each achievement in the list JSON also has `"unsupported"` and `"set"`, taken from the
  rcheevos bucket it is listed in (`ra_glue.c:605-610`). `"unsupported"` is 1 in the "Unsupported"
  bucket. rcheevos puts an achievement there when its logic cannot be parsed
  (`rc_client.c:2113-2116,2126-2129`), or when its logic uses a bad address
  (`rc_client.c:1293-1314`). When the game loads, rcheevos checks every address once: an address
  past the last region of the console's memory map is bad without a read, and so is one the memory
  read callback refuses (`rc_client.c:1346-1370`, called at `:1861`). During play, a failed read
  makes its address bad, unless the read followed a pointer
  (`rc_client.c:5864-5876,5895-5896,5937-5938`). The bucket never changes afterwards
  (`rc_client.c:4100-4101`). `ra_glue_read_memory` refuses an address only while `g_mem_base` is
  NULL, or at 16 MB and above (`ra_glue.c:96-100`); "Memory reads" describes what it refused before.
  The dialog shows an unsupported achievement as "Unsupported", or as "Unlocked" if the player
  already earned it in this mode (`rc_client.c:1686-1689`). `"set"` is the set's title from
  `rc_client_get_subset_info` when the game has more than one active set, and empty otherwise,
  because rcheevos sets a bucket's `subset_id` only then (`rc_client.c:4297-4301,4420`).
- OBSERVED: with the lock-state grouping the glue requests (`ra_glue.c:584-586`), rcheevos lists
  all of one set's buckets before the next set's (`rc_client.c:4397-4431`), and the dialog adds a
  heading row whenever the set title changes. DERIVED: two sets with the same title in a row would
  share one heading.
- OBSERVED: long titles wrap. The shared row layout keeps the title on one scrolling line
  (`list_item_two_text_icon.xml:36-38`) and other dialogs use it, so `AchievementsDialog` turns that
  off for its own rows.
- OBSERVED: the glue returns 0 both when it fails and when the list does not fit the buffer
  (`ra_glue.c:587-588,665-669`). The list used to be read into a fixed 128 KB buffer, so a longer
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
- Not yet verified on device: the item shows only with RetroAchievements enabled; the dialog
  matches the loaded game in hardcore and in softcore; the warning row shows for this client; a
  game with a bonus set shows set headings; long titles wrap.

### Open items (not fixed here)
- DERIVED: while emulation is paused, nothing runs rcheevos's queued work: the emulator thread
  waits in `pause_loop` before it reaches `ra_glue_do_frame` (`main.c:979-985`), and
  `ra_glue_idle` has no caller. So pings stop during a pause, award and leaderboard retries that
  rcheevos has scheduled wait (`rc_client.c:4694-4702,5396-5404`), and a game load whose last
  server response arrives during a pause completes, with its mode notice, when emulation resumes.
- OBSERVED: challenge and progress indicators are not shown, although section A of the compliance
  page requires them (see On-screen notices). rcheevos raises challenge-indicator and
  progress-indicator events (`rc_client.h:814-818`), but `ra_build_event_json` has no case for them
  and drops them (`ra_glue.c:329`). The achievements JSON has no measured progress
  (`ra_glue.c:627-628,649-650`), although rcheevos provides it (`rc_client.h:560-561`). REFERENCE
  (RetroAchievements docs): a Trigger flag exists "so a challenge indicator can be shown on the
  screen" (`developer-docs/flags/trigger.html`, revision `9a5f33c`), and a Measured flag "adds a
  progress bar to the achievement overlay" (`developer-docs/flags/measured.html`, revision
  `6226ebf`).
- DERIVED (predates this branch): `RetroAchievementsManager.onServerCall` runs each request with
  `execute()` (`RetroAchievementsManager.java:321`) and catches only `IOException` (`:351`). Any
  other exception on the HTTP thread reaches Android's default uncaught-exception handler, which
  always kills the process (AOSP `android-14.0.0_r1`, `RuntimeInit.java:125-177,235`,
  `KillApplicationHandler`). The app sets no handler of its own. During a game this closes the
  emulation process; during Connect in Settings it closes the app. Not seen on a device.
- REFERENCE (AOSP `android-14.0.0_r1`, `NotificationManagerService.java`): Android refuses a new
  toast from an app that already has five queued, counting every toast of the app
  (`:384,3365-3377`), and shows queued toasts one at a time, a long one for 3.5 seconds
  (`:399,8648-8653,8763`). OBSERVED: the RetroAchievements notices share that limit with the app's
  other toasts, including the hardcore "disabled" toasts, because `Notifier.showToast` makes a new
  toast on each call (`Notifier.java:95`; `CoreService.java:309,418,543,558,877`). DERIVED: toasts
  beyond five are lost, so several unlocks on the same frame, or an unlock that arrives while
  repeated taps on a blocked control fill the queue, go unannounced. A "disabled" toast that
  follows a notice waits for it, up to 3.5 seconds for each notice ahead of it. The achievement
  list still shows every unlock.
- DERIVED: an unlock or a leaderboard entry that still waits for a retry when the game closes is
  never sent. rcheevos keeps it only in memory: unloading the game removes only the game's ping
  from the scheduled work, and `rc_client_destroy` frees the client without running the rest
  (`rc_client.c:191-227,3139-3209`). The app destroys the client when the session ends
  (`CoreService.java:1307,1394`; `ra_glue.c:393`). The progress the app saves at exit holds only
  the runtime state of active achievements (`rc_client.c:6762,6825-6826`), so the next session
  starts that achievement locked again, from the server's unlocks. REFERENCE (compliance page
  revision `361119b677dc`, section A): "Unlocks created while offline must be securely cached and
  sync to RetroAchievements when connectivity returns." Not seen on a device.
- DERIVED (predates this branch): `ra_glue_shutdown` drops the pending requests and then waits
  for answers that are being handled (`ra_glue.c:382-388`). If such an answer asks for an
  immediate retry (`rc_client.c:4675-4680,5377-5382`), the retry takes a request slot after the
  drop, so its answer could reach the destroyed client. The app ends its process right after the
  shutdown (`CoreService.java:1307-1309`), so that answer would have to arrive within that moment.
  Not seen on a device.
- Not audited on this branch: the rest of the compliance page, for example the resume and
  save-state rules (section B).
