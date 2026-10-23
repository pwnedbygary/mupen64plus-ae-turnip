# RetroAchievements — remaining issues

Status: merged to `master` via PR #10 (`af7dd84f0`).
Confirmed working on device: Connect sign-in ("Signed in as pwnedbygary", session token
persisted), hardcore gates for save/load state, slots, GameShark and cheats (deny toast),
and denied actions no longer announce first. Hardcore has since stopped refusing saves and
refuses only loading (see "Save states and Resume under hardcore").

This file lists what is still open. Evidence labels: OBSERVED = read from current source, or
seen in a named run such as "OBSERVED (host JVM)"; DERIVED = inferred from code paths;
REFERENCE = read from a pinned outside source (RetroAchievements server or client source or
documentation, AOSP, libretro, POSIX, Project64, or upstream mupen64plus-core); HYPOTHESIS =
plausible but untested.
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
  after a sign-in succeeds (`CoreService.java:1512-1538`, the only `loadGame` call) and shows the
  mode toast only once the game has loaded (`:1495-1509`).
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
- Still open after this run: the "Not yet verified on device" items in the issue #15 section,
  among them an unlock in softcore from an achievement that reads memory (seen on 2026-10-22,
  below), and everything under hardcore (the run of 2026-10-21 below covers part of it).

## Device verification (debug build `3.0.338 017e3557`, Retroid Pocket 6, 2026-10-21)
- OBSERVED (`adb install -r`, `adb shell dumpsys package`): the debug APK built from `017e35574`
  replaced `3.0.338 06aedfe1` in place at 16:20 that day. Its sha256, recorded when it was built
  (the file was overwritten later), was
  `64f3aa89208f3d0d65f4898c97bfe4b1c1080da72ae04358f9d7c30f18cc91f0`. The 25 files in the app's
  `GameData` folder had been copied off the device first, with matching MD5 sums. The device runs
  Android 13 (`getprop ro.build.version.release`). DERIVED: so the build sent
  "Mupen64Turnip/3.0.338 (Android 13)" (see "User-Agent").
- OBSERVED (device): Mario Tennis ran five times: three starts came from a view intent for the
  ROM, which the app treats as a launch from another app and so restarts the game (see "Save
  states and Resume under hardcore"), and two from the gallery. Notices were read from
  `dumpsys notification`; events, state loads and how each game was launched, from the app's log.
- OBSERVED (device), hardcore off in Settings: "RetroAchievements: casual mode" showed; the
  achievement list read "Casual: 0 of 97 unlocked" and "0 of 658 points" and listed the warning
  row and "Locked" rows, as on 2026-10-20; the speed item went to "Speed 250 %" with no notice;
  no warning toast showed. Connect in Settings then showed "Signed in as" and the account name,
  and hardcore was switched on there.
- OBSERVED (device), hardcore: "RetroAchievements: hardcore mode" showed, and about six seconds
  later "Warning: Unknown Emulator. Hardcore unlocks cannot be earned using this emulator.". The
  log has the matching event for achievement `101000001`, rcheevos's ID for the server's warning
  (`rc_client.c:25`). The two later hardcore starts showed the same pair; in all three the event
  came 6.5 to 6.7 seconds after the session's service started. Neither softcore session showed
  the warning. DERIVED: each mode notice means the session signed in and loaded the game
  (`CoreService.java:1495-1509,1512-1538`). The warning's timing matches "On-screen notices", and
  the server still treats `Mupen64Turnip` as an unknown emulator, as it will until
  RetroAchievements approves the name (see "Hardcore unlocks").
- OBSERVED (device), hardcore: the speed item, and OK in the Game speed dialog, were refused with
  "Hardcore mode: speed controls are disabled", and the menu stayed at "Speed 100 %". The dialog
  itself still opened, showing the stored custom speed, 250 %. The audio sync item read
  "Sync audio", its label while the frame limiter is on (`GameActivity.java:856-858`), and
  tapping it was refused with the same notice. DERIVED: the app's preferences on the device, read
  back at 20:10 that day after a later reinstall, have audio sync on and volume 100 (each session
  also logged "Change volume to 100" from the audio plugin), with which a session starts with the
  limiter off (`GlobalPrefs.java:727-730`), and the label was read before the item was first
  used, so hardcore turned the limiter on when the session started (`CoreService.java:884`).
- OBSERVED (device), hardcore: Save slot showed "Saving slot 0…" and wrote the state and
  `.ra-progress/Mario Tennis (U) [!].st0.progress` (14,040 bytes) in the slot folder. Load slot,
  and a state picked in "Load from auto save…", were each refused with "Hardcore mode: loading
  save states is disabled", and the log shows no state loaded. The achievement list read
  "Hardcore: 0 of 97 unlocked" under the game's title and still listed the warning row. The
  menu's Exit wrote an auto save and its progress file, and the auto-save cleanup then deleted
  the oldest auto save, one from 2026-10-20 that had no progress file.
- OBSERVED (device): the gallery's Resume then showed "Resumed game: hardcore mode is off for this
  session" and "RetroAchievements: casual mode", the log shows that auto save loaded, the list
  read "Casual: 0 of 97 unlocked", and Load slot loaded the slot saved under hardcore. The next
  start, from the gallery with hardcore still on in Settings, ran in hardcore and showed no
  resume notice, although the auto save still existed. DERIVED: so it was the gallery's Start,
  which restarts the game (see "Save states and Resume under hardcore"), and Start keeps
  hardcore.
- OBSERVED (device): afterwards hardcore was switched off in Settings (the app's preferences, read
  back at 20:10, have it off), the files the checks had added were copied off the device and
  removed, and the auto save the cleanup had deleted was put back, so the `GameData` folder again
  matched the copy taken before the install (25 files, same MD5 sums).
- Not covered, among others: an unlock of an achievement that reads memory, in either mode;
  leaderboard attempts; the challenge, progress and leaderboard lines; the offline queue;
  netplay; the sign-in fallbacks; game data on external storage and the Drive backup; the re-arm
  test; slow speeds and frame advance; saving to and loading from a file; cheats chosen in the
  game's settings; and the mode notice for a game load that ends while the menu is open (in one
  hardcore start the menu was meant to open in the first second, but the log shows only the
  start-up pause).

## Device verification (debug build `3.0.338 11f5f3b9`, Retroid Pocket 6, 2026-10-22)
- OBSERVED (read at 01:35 that night with `adb shell dumpsys package`, `getprop` and, through
  `run-as`, the app's preferences): the debug package is `3.0.338 11f5f3b9`, last updated at 16:52
  on 2026-10-21; RetroAchievements was on and hardcore off; the device runs Android 13. DERIVED:
  the version name ends with the short hash of the commit it was built from, `11f5f3b96`
  (`version_common.gradle:12,19`), which differs from `017e35574` only by two menu icons; the
  build sent "Mupen64Turnip/3.0.338 (Android 13)" (see "User-Agent").
- OBSERVED (device): the tester started Mario Tennis from the gallery at 01:26. At 01:30:36.229,
  4 minutes 22.5 seconds after the service started, the app logged the unlock event of
  achievement 48351, "Match Winner" ("Win a exhibition match.", 3 points), the only
  RetroAchievements event it logged in the session; it does not log progress or tracker updates
  (`CoreService.java:1408-1411`). The device's system log, from 01:26:01 to 01:35:19, shows two
  toast windows from the app: one opening 1.2 seconds after the service started, and one opening
  26 ms after the unlock was logged and closing about four seconds later. Asked whether
  "Achievement unlocked: Match Winner (3 points)" popped up, the tester said it did. The app's
  file of unsent unlocks (`retroachievements-unsent.txt` in its no-backup folder,
  `CoreService.java:1353`) was last written at 01:30:36.546 and then held only its comment line.
  The app's log has no server error event and no "RA request failed" line
  (`RetroAchievementsManager.java:300-304`) up to 01:54, when the tester closed the game. The
  tester's screenshot at 01:31:02, in the achievement list they opened at 01:30:43, shows
  "Match Winner" with a ticked box and "Unlocked, 3 points", and the other rows on screen
  "Locked".
- DERIVED: the session ran in softcore. Hardcore was off in Settings when read, and the system
  log shows no activity started after the game at 01:26:13, so Settings was not opened before the
  read; no server warning arrived, which in hardcore came within seven seconds of each start (see
  the run of 2026-10-21); and the tester called it "a casual achievement". The first toast was
  the mode notice (`CoreService.java:1504-1509`) and the second the unlock notice
  (`:1413-1414,1439-1450`; `strings.xml:280`). The server recorded the unlock within about a
  third of a second: the glue keeps an unlock in that file from just before it is sent
  (`ra_glue.c:548-552`), writes the file only when it keeps or drops one, so a write that leaves
  none is a drop (`:406-437`), drops one only when the server records or refuses it
  (`:620-623`), and counts an answer as a refusal only when rcheevos raises a server error for it
  (`:478-482`; `rc_client.c:4669-4673`), which the app would have logged
  (`CoreService.java:1409-1411`) and shown as a third toast (`:1425-1426,1452-1464,1555-1557`).
  Winning a match is read from the game's memory, so achievements that read memory unlock on the
  device in softcore (see "Memory reads").
- OBSERVED (device): the game was still open when these were read, its emulation paused since
  01:30:42, just before the achievement list was opened from its menu.
- Not covered, among others: an unlock in hardcore, which the server should record as softcore
  until RetroAchievements approves the client (see "Hardcore unlocks"); an unlock with the
  network off (see "Unlocks kept until the server answers"); leaderboard attempts; the challenge,
  progress and leaderboard lines; and the mode notice after a slow start.

## Device verification (debug APK `1193373b` with the reset fix, Retroid Pocket 6, 2026-10-23)
- OBSERVED (adb): at 00:40 the debug APK built for the reset fix, sha256
  `1193373ba0470264992d4cef894b5518ae0dc2cdc500415bb62f75d44945a7fc` (the file was later
  overwritten by another build), was installed in place with `adb install -r`, after the app's
  saves were copied off; they matched the copy afterwards (26 files). `aapt2` gave its version
  name as `3.0.338 fde9c41c`, the short hash of the commit it was built on
  (`version_common.gradle:12,19`), as the fix was not yet committed. At 01:12 the installed APK
  still had that sha256 (`sha256sum` on the device) and that version name, last updated at
  00:40:22 (`dumpsys package`), in the install folder that the app's log shows the session's
  process using. RetroAchievements was on and hardcore off. DERIVED: the APK runs the code of
  `1530dde6d`: the edits made after it was built were to comments and docs, and the core
  libraries rebuilt after them kept its build IDs.
- OBSERVED (device; times are the app log's, from the device's clock): Mario Tennis started from a
  view intent at 00:40:30, and emulation paused at 00:40:53.709 as the in-game menu opened. Reset
  in the menu (00:41:17.673) opened the confirmation dialog. When it was confirmed, the main thread
  logged the app's reset call (`CoreFragment.java:901-909`; `CoreService.java:615-618`) and then
  the dialog's window being taken down, both at 00:41:29.270, and emulation continued at
  00:41:29.701. A screenshot taken after the reset shows the Nintendo logo; before it the game was
  past the logo, in its intro, which shows behind the confirmation dialog. The achievement list,
  opened at 00:42:00.869, showed "Casual: 1 of 97 unlocked" and its rows. A quit broadcast sent
  from adb closed the game at 00:42:13 (`CoreService.java:233-254`). The screenshots' file times
  come from the PC's clock; the screenshot that shows the confirmation dialog has a file time 1.19
  seconds earlier than the log's time for choosing Reset.
- DERIVED: the PC's clock was more than 1.1 seconds behind the device's at 00:41, as the dialog
  opens only after the log line for choosing Reset (`GameActivity.java:948,1014-1015`;
  `CoreFragment.java:877-899`) and a screenshot's file time is that of its last write, which
  follows the capture; so this record orders the screenshots by what they show, not by their file
  times. The reset ran while emulation was paused, so the emulator thread was in the core's pause
  loop, which runs idle work after each 10 ms wait (see "Paused emulation"), while the main thread
  reset the achievements and asked the core for a soft reset (`CoreInterface.java:894-897`;
  `main.c:802-816`), which the emulator thread carries out once emulation goes on
  (`device.c:284-289`). The reset call returned within about a millisecond, and the game and the
  list worked afterwards. One reset shows no hang; it does not show that the race is gone. The
  host test shows that for a reset during a frame and, in a run added later, for a reset during
  the idle work that finishes a game's load (see "Threads in the glue"). This run does not show
  the idle-work case, as nothing records whether the reset met one of the pause loop's idle calls;
  that a reset and idle work cannot overlap rests on both taking the same lock
  (`ra_glue.c:991-1009`).
- Not covered, among others: a reset while a frame or idle work raises an event; the reset button
  mapping; a reset in hardcore.

## Deferred items from commit `8f14880ef` (login/token handling)
Reference: that commit's message lists these as nonblocking. The issue #15 section addresses all
four: "Sign-in order" three of them, and "User-Agent" the hardcoded User-Agent version. The saved
token now goes before the password, which stays in Settings as the fallback for a rejected token
(so there is no offer to clear it). The token is written with `commit()`. The missing token
fallback after a failed password login is covered by the same order: a saved token is always
tried before the password.

## Verification gaps (not bugs; close with device tests)
- Terminal-failure release policy: with hardcore active and a broken login (offline, or a bad
  password with no valid saved token: a saved token is tried first), confirm that loading save
  states, the GameShark button and other speeds are allowed again once the async failure
  arrives, instead of staying refused for the session. The cheats chosen in the game's settings
  stay off if the failure arrives after the emulation job has reached them: the job adds them
  only once, before emulation starts (`CoreService.java:886-899`). Code-verified only.
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
- OBSERVED: from `8f14880ef` up to `6e79f9e9b` the app sent the User-Agent
  `Mupen64PlusAE/3.0 (Android)`, and with it the device's achievement list showed the server's
  "Warning: Unknown Emulator" (see the device verification of 2026-10-20). Before `8f14880ef` it
  sent `Mupen64PlusAE`. It now sends its own name and version (see "User-Agent").
  `CoreService.onRaEvent` logs each event's JSON except progress and tracker updates; in
  `ff249b18a` it logged only the event type.
- DERIVED: hardcore unlocks are recorded as softcore until RetroAchievements approves this client.
  The server's warning achievement is shown on screen when it unlocks, which happens only in
  hardcore (see "On-screen notices"), and in the in-game achievement list in both modes. The
  server's separate `Warning` text (RAWeb `ac7bf9b84d98`, `GetAchievementSetsAction.php:557`) is
  never shown, because rcheevos does not read that field. OBSERVED (device, 2026-10-21): build
  `3.0.338 017e3557` still got the server's warning achievement, and it popped in hardcore (see
  "Device verification" for that build); DERIVED: that build sends the new User-Agent (see
  "User-Agent"). OBSERVED (device, 2026-10-22): an unlock left nothing in the app's file of
  unsent unlocks and raised no server error; DERIVED: it was made in softcore, and the server
  recorded it (see "Device verification" for build `3.0.338 11f5f3b9`). Not yet verified on
  device: that a hardcore award request comes back as softcore.

### User-Agent
- REFERENCE (compliance page revision `361119b677dc`, section C): "The emulator must use its own
  unique, stable user agent with the following format:"
  `EmulatorName/v1.0.0 (OSName 10.0) core_name/v0.5.0`. The name and version are required, and
  "Versions must be numeric and incrementing, with the exception of an optional "v" prefix". The
  operating system is optional, and so is the core, which is "strongly advised if the emulator
  supports multiple cores". An emulator that identified itself as another client before must
  disclose that "when applying for compliance".
- OBSERVED: every request, from a game session or from Connect in Settings, goes through
  `RetroAchievementsManager.request`, which sends `USER_AGENT`: rcheevos hands each request to the
  glue's `ra_glue_server_call`, which passes it to the manager, unless no request slot is free or
  the client is shutting down; then it answers at once and nothing is sent (`ra_glue.c:518-579,926`;
  `RetroAchievementsManager.java:297-299,423`; `LoginPreference.java:82,108`). `USER_AGENT` is
  now "Mupen64Turnip/", the leading number of the app's version name, and " (Android <release>)"
  with `Build.VERSION.RELEASE` (`RetroAchievementsManager.java:63-64,382-391`). The build makes
  the version name from the version and the git commit, as in `3.0.338 06aedfe1`
  (`version_common.gradle:11-12`). A version name without a leading number would give the
  version code instead, and an unknown release leaves the operating system out.
- OBSERVED: before this, the app sent two User-Agents, both with the parent emulator's name:
  `Mupen64PlusAE` from `4df6051aa` (`RetroAchievementsManager.java:52` at `4df6051aa` and at
  `cbe73ad69`), then `Mupen64PlusAE/3.0 (Android)` from `8f14880ef` up to `6e79f9e9b` (`:56` at
  `8f14880ef`). Both reached the server. The commit message of `cbe73ad69` records a device run in
  which the server answered "Invalid user/token combination" over TLS; builds of `4df6051aa` could
  not reach it, as the app's cleartext policy blocked their http:// URL (same message). The second
  got the warning row on the device (see "Hardcore unlocks").
- DERIVED: on Android 13 this branch's debug build sends "Mupen64Turnip/3.0.338 (Android 13)". The
  request for approval has to disclose both earlier User-Agents. The new one has no core segment:
  the app has one emulation core, `mupen64plus-core`, and its video and RSP plugins are not cores
  (`settings.gradle:1`).
  Until RetroAchievements approves the new name, hardcore unlocks stay softcore with it too (see
  "Hardcore unlocks").
- OBSERVED (host JVM): `RetroAchievementsManagerTest` has five more tests: the User-Agent for a
  version name with and without the commit, without an Android release, and for a version name
  without a leading number, and the User-Agent header of a request against the compliance page's
  format. Against the manager of `6e79f9e9b` the five fail and the other seven pass. A copy of the
  new manager that keeps the commit in the version fails four of them; one that writes
  "(Android null)" for an unknown release, and one without the version code, each fail one; one
  that still sends the old User-Agent fails only the header test; and one that keeps the version
  pattern in a static field declared after `USER_AGENT` fails all twelve, as the field is still
  null when the class sets `USER_AGENT` (javac 17.0.20.1, JUnit 4.13.2). On the host JVM
  `Build.VERSION.RELEASE` is null, so no test shows that the call site passes the release rather
  than another value; that part rests on the source.
- OBSERVED (host build): `./gradlew :app:testDebugUnitTest --rerun :app:assembleDebug` passes with
  every unit test, and the APK's code has "Mupen64Turnip/" and no longer the old User-Agent.
- OBSERVED (device, 2026-10-21): with build `3.0.338 017e3557`, Connect in Settings showed
  "Signed in as" and every game session showed its mode notice (see "Device verification" for
  that build). DERIVED: so sign-in and the game's load work with the new User-Agent, which that
  build sends as "Mupen64Turnip/3.0.338 (Android 13)". OBSERVED (device, 2026-10-22): build
  `3.0.338 11f5f3b9` logged an unlock that then left nothing in the app's file of unsent unlocks;
  DERIVED: it sends the same User-Agent, the unlock was made in softcore, and the server recorded
  it (see "Device verification" for that build).

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
  `mem_base_u32(g_mem_base, MM_RDRAM_DRAM + address)` (`ra_glue.c:138-163`).
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
  responses on its HTTP thread (`RetroAchievementsManager.java:298,305`). `g_mem_base` is NULL
  until `CoreStartup` (`main.c:111`; `frontend.c:105`), which the service runs on its handler
  thread (`CoreService.java:787`) after other start-up work, while the sign-in starts from
  `onStartCommand` (`CoreService.java:1254,1378`) and the game loads as soon as it succeeds
  (`CoreService.java:1535`); nothing orders the two. `CoreShutdown` frees that memory on the
  handler thread (`CoreService.java:953`; `CoreInterface.java:738-740`; `frontend.c:135-136`).
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
- OBSERVED: `ra_glue_create` now turns background memory reads off (`ra_glue.c:934-937`). rcheevos
  then queues the rest of the game load, which applies the server's unlocks, checks the addresses
  and reports the result (`rc_client.c:1787-1923`, queued at `:1936-1950`), and runs it from
  `rc_client_idle` (`:6507-6531`), which `rc_client_do_frame` calls at its end even before a game
  is loaded (`:6490`). The emulator calls `ra_glue_do_frame` from `new_vi` (`main.c:986`) on the
  handler thread, between `CoreStartup` and `CoreShutdown`. The load result now comes from that
  thread too, and the Java side still posts it to the main thread
  (`RetroAchievementsManager.java:332-334`). DERIVED: rich presence, which rcheevos reads for each
  ping (`rc_client.c:5745-5752`), is read there as well.
- OBSERVED: `ra_glue_idle` runs the same queued work (`ra_glue.c:991-999`;
  `rc_client.c:6493-6536`), and the core's pause loop now calls it while emulation is paused (see
  "Paused emulation"). The comments keep it on the emulator thread with `ra_glue_do_frame`
  (`ra_glue.c:10-17,934-936`).
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
  build `3.0.338 06aedfe1`). OBSERVED (device, 2026-10-22): "Match Winner" ("Win a exhibition
  match.") unlocked and then showed as "Unlocked" in the list; DERIVED: the session ran in
  softcore, and winning a match is read from the game's memory, so an achievement that reads
  memory unlocked in softcore (see "Device verification" for build `3.0.338 11f5f3b9`). Not yet
  verified on device: under hardcore the server still records an unlock as softcore until
  RetroAchievements approves this client; the mode notice also appears after a slow start.

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
  `RetroAchievementsManager.java:45-48`).
- OBSERVED: the achievement list holds pointers to rcheevos's live achievement data
  (`rc_client.c:4413`). rcheevos refreshes each one's progress text and bucket under its mutex
  while it builds the list (`:4306`), and the glue reads them after rcheevos has released it
  (`:4433`; `ra_glue.c:1215-1223`). The emulator thread rewrites both outside the mutex when it
  raises events for that achievement (`rc_client.c:6190-6193,6465-6467`), after a frame and also
  after a state's load: rcheevos restores the progress kept with the state under its mutex, then
  raises the restored events outside it (`:6882-6904`), and the glue restores it in the load hook
  on the emulator thread (`savestates.c:1479`; `ra_glue.c:1545`). DERIVED: a list read during
  play could show a row as it was a frame earlier, or progress text that was being rewritten.
- OBSERVED: rcheevos makes a loaded game visible under its mutex (`rc_client.c:1815-1818`), and
  `rc_client_is_game_loaded` is true from then on (`:3106`), but it checks the game's addresses
  afterwards, outside the mutex (`:1861`). That check moves each achievement that uses a bad
  address to the "Unsupported" group (`:1303-1305,1346-1370`). The list builder holds the mutex
  while it counts the groups and allocates space for them, then fills them in a second pass that
  reads each achievement's group again (`:4294-4433`). An unlock cannot move an achievement
  between the glue's groups meanwhile: rcheevos sets the unlocked state under the mutex
  (`:4812-4816`), the group follows the state (`:4105-4158`), and the glue's lock-state grouping
  counts Recently unlocked and Unsynced as Unlocked, and Active challenge and Almost there as
  Locked (`:4226-4234`). DERIVED: a list built while a game finished loading could see an
  achievement turn "Unsupported" between counting and filling. If that group was already counted,
  the second pass could list the achievement twice, writing past the end of the space allocated;
  with more than one active set, it could also fill a group that was not counted, writing over the
  list's own pointers (`:4353-4357,4397-4431`). That needs a set with an address the check
  refuses, which with the core's memory present is only an address above `0x7FFFFF`, the end of
  rcheevos's N64 map (`:1349-1350,1362`; `ra_glue.c:148-152`; see "Memory reads"). The overlay
  reads the list when its activity is created (`GameActivity.java:417`;
  `RaIndicatorOverlay.java:69-70`). Outside netplay, that happens during play only when Android
  recreates the activity for a configuration change the manifest does not list; rotation is
  listed (`AndroidManifest.xml:386`; `GameActivity.java:568`). Otherwise the old game screen's
  `onStop` has paused emulation, and emulation resumes only after the new screen's overlay has
  read the list (`GameActivity.java:643-676,780-790,1101-1112,1170`). Outside netplay, the dialog
  reads the list while emulation is paused (see "Paused emulation"). In netplay neither the menu
  nor `onStop` pauses emulation (`CoreService.java:446-454`; `main.c:481-482`), and achievements
  run there in softcore (`CoreService.java:1362-1366`), so in netplay both reads could overlap a
  frame before this change too.
- OBSERVED: the glue now holds one lock, `s_frame_lock`, while rcheevos runs a frame or its idle
  work, while the state-load hook restores a state's progress into the loaded game, while rcheevos
  resets the game (see the reset bullets below), and while the achievement list is built and read
  (`ra_glue.c:63-66,981-999,1001-1009,1279-1286,1543-1546`). A list waits only while rcheevos
  processes a frame, does its idle work, resets the game or restores progress, not while the core
  emulates the frame, and those wait while a list is built. Only `ra_glue_do_frame`,
  `ra_glue_idle`, `ra_glue_reset`, `ra_glue_state_loaded` and the list getter take the lock, and
  they take it before any other: rcheevos takes its mutex under it, and the glue its request lock
  when rcheevos sends a request (`ra_glue.c:523,539`). When the request sends an unlock or a
  leaderboard entry that the file of unsent ones does not hold yet, the glue first rewrites that
  file under its own lock (`ra_glue.c:406-423,548-552`; see "Unlocks kept until the server
  answers"), so a list can also wait for that write; for a retry of an entry the file holds, it
  only looks the entry up. The Java callbacks made under the frame lock only parse and post to the
  main thread or hand the request to the HTTP threads
  (`RetroAchievementsManager.java:297-298,310-321,332-334`), and neither caller of the list
  getter holds a lock (`AchievementsDialog.java:53`; `RaIndicatorOverlay.java:70`). `ra_glue.h`
  now says the getter waits for a frame, idle work, a reset or a restore that is running, so it
  must not be called from a callback the glue makes during one (`ra_glue.h:87-89`), and the
  `ra_glue.c` header names the lock (`ra_glue.c:17-19`). DERIVED: neither problem in the two
  bullets above can happen any more.
- OBSERVED: rcheevos still refreshes an achievement's progress text and group without the lock,
  under its mutex, on the HTTP thread that delivers an unlock's answer: when the glue looks up
  the title of an unlock the server refused (`rc_client.c:4673`; `ra_glue.c:755`;
  `rc_client.c:4494-4496,4525`), and when rcheevos checks for mastery after an accepted one that
  leaves no achievements remaining (`:4731-4735`). Both refresh the achievement that was unlocked,
  for which the refresh clears the progress text and picks an unlocked group (`:4103-4115,4158`).
  Of those two fields the glue reads the text and whether the group is an active challenge, after
  rcheevos releases its mutex (`ra_glue.c:1215,1223`). DERIVED: a list built meanwhile shows the
  same rows, as the list's own refresh has already cleared that text and an unlocked group is
  never an active challenge; only a race checker can tell. `ra_glue_deserialize_progress`, which
  the app does not call, restores progress without the lock, and `ra_glue.h` says it must not run
  beside frames, idle work, resets or the list on another thread (`ra_glue.h:91-98`).
- OBSERVED: the hardcore switch changes achievements only under rcheevos's mutex
  (`rc_client.c:1451-1504,6955-6967`), and raises nothing outside it while no game is loaded
  (`:6970`). The app switches hardcore before it signs in (`CoreService.java:1366`), and turns it
  off on the emulation thread before emulation starts when the session resumes from the auto save
  (`:413-419,719-723,926`). DERIVED: no game is loaded at either call, as a game's load finishes
  only in a frame or idle work (see "Memory reads" and "Paused emulation"), so the switch needs
  no lock. A reset could never overlap a list, as it runs on the main thread, where the list is
  built (see the `ra_glue_reset` bullet below), but it could overlap a frame or idle work, so it
  now takes the lock too (see the bullets after that one).
- OBSERVED (host build): three new runs of the session test hold the emulator thread where
  rcheevos has just changed achievements outside its mutex, while another thread asks for the
  list; the list must be built only after the hold ends. Two give the game an achievement at
  `0x800010`, which the load disables, and hold the thread in rcheevos's log callback while it
  checks the addresses, once in a frame and once in idle work; the list must show that
  achievement as unsupported. The third holds it in the event callback when a state's load
  restores progress that primes the challenge again; the list must show the challenge as active.
  All eighteen runs pass with GCC 16.2.1 and clang 23.1.1, and under clang ASan, UBSan,
  LeakSanitizer and ThreadSanitizer (UBSan reports only the two vendored sites named under
  "On-screen notices"; ThreadSanitizer reports no race). With the glue of `ceca932bc` the three
  new runs fail only the check that the list waited, and ThreadSanitizer reports these data
  races, though not every one in every run: the list getter reads the game before it takes
  rcheevos's mutex while the load sets it (`rc_client.c:4289,1817`), the list reads an
  achievement's group while the load moves it to "Unsupported" (`:4100,1305`), and the list reads
  the group and clears the progress text while the restored events rewrite them (`:4100,4158`;
  `:4103`). Under ThreadSanitizer the frame run passes that check, and fails it with symbolizing
  off (`TSAN_OPTIONS=symbolize=0`). DERIVED: symbolizing the first report of the test holds up
  the list thread until the hold has ended. A copy without the lock in frames fails only the
  frame run, one without it in idle work only the idle run, one without it in the state-load hook
  only the restore run (under ThreadSanitizer that run passes, and the two races with the
  restored events are reported), and one that takes the lock only in the list getter fails all
  three. Not verified on a device.
- OBSERVED (host build): `./gradlew :app:testDebugUnitTest --rerun :app:assembleDebug` passes with
  every unit test. `ra_glue.c` was recompiled for all four ABIs with empty ndk-build stderr logs;
  in each ABI's object the frame, idle, list and state-load functions call `pthread_mutex_lock`
  and `pthread_mutex_unlock` (on arm64 the frame and idle functions lock through a helper the
  compiler outlined), and each packaged core library has the build ID of the library linked from
  that object.
- OBSERVED: `ra_glue_reset` runs on the thread that resets the core (`main.c:802-816`;
  `frontend.c:359`; `CoreInterface.java:894-897`). The app resets it from `CoreService.restart()`
  (`CoreService.java:615-618`), which a confirmation dialog and the reset button mapping reach
  (`CoreFragment.java:901-907`; `GameActivity.java:1121`; `PeripheralController.java:332`).
  DERIVED: Android delivers those dialog and input callbacks on the main thread, so the reset runs
  there, not on the emulator thread that `ra_glue.h` named. Its comment now says so
  (`ra_glue.h:42-43`).
- OBSERVED: rcheevos resets the achievements under its mutex, then raises the events that follow,
  such as hiding a primed challenge, outside it (`rc_client.c:4869-4874,6657-6667`), as a frame
  raises its own (`:6465-6467`), and idle work can finish a game's load, which checks the
  addresses outside the mutex (`:1861`). Until this change `ra_glue_reset` took no lock
  (`ra_glue.c:1001-1007` at `4ad1f8b16`). DERIVED: a reset during play could run while the
  emulator thread was in a frame or, while emulation was paused, in idle work, and the two threads
  could then change the same achievements at once and raise an event twice or lose one. This was
  older than this branch: `ra_glue_reset` came with `4df6051aa`.
- OBSERVED: `ra_glue_reset` now holds `s_frame_lock` while rcheevos resets (`ra_glue.c:1001-1009`),
  so a reset waits while rcheevos runs a frame, does its idle work or restores a state's progress,
  and those wait for a reset. The reset's events reach Java callbacks that only post to the main
  thread (`RetroAchievementsManager.java:310-321`), so a reset on the main thread never waits for
  that thread. The core's own reset shortcuts would reset on the emulator thread, from the SDL
  event filter (`eventloop.c:394,649`) that its input check pumps (`main.c:304-309`), and that
  check runs before a frame's and idle work's RetroAchievements work, not inside it
  (`main.c:961-962,978,986`). The key shortcut also runs on the thread that sends
  `M64CMD_SEND_SDL_KEYDOWN` (`frontend.c:324-329`), which the app never sends (it only names the
  command, `CoreTypes.java:96-97`). DERIVED: no thread takes the lock twice, and on Android those
  shortcuts never fire anyway (see "Compliance checklist"). `ra_glue.h` says that the reset waits
  (`ra_glue.h:42-43`), the getter's comment names resets (`:87-88`), and the `ra_glue.c` header
  and the lock's comment name them (`ra_glue.c:17-19,63-65`). DERIVED: the overlap recorded until
  now under "Open items" can no longer happen.
- OBSERVED (host build): a new run of the session test holds the emulator thread in the event
  callback when a frame primes the challenge, while another thread resets the game; the reset must
  return only after the hold has ended, and the challenge indicator must show and then hide, once
  each. All twenty-five runs pass with GCC 16.2.1 and clang 23.1.3, and under clang ASan, UBSan,
  LeakSanitizer and ThreadSanitizer (UBSan reports only the two vendored sites named under
  "On-screen notices", LeakSanitizer only the retries named in "Open items", and ThreadSanitizer
  no race). With the glue of `4ad1f8b16` the new run fails the checks that the reset waited and
  that the indicator's last event hides the challenge (DERIVED: the overlay would have kept showing
  it). Under ThreadSanitizer that glue reports data races in every run, though not the same ones:
  the reset clears and sets the pending events that the frame is raising
  (`rc_client.c:6378,6381,4870`; `:6181,6387,6404,6411`), resets the trigger while the frame
  refreshes the achievement's display (`trigger.c:338`; `rc_client.c:4151`), and both clear the
  game's pending events (`rc_client.c:6421`). The runs with nine reports also show both threads
  refreshing the achievement's display and clearing its pending events
  (`rc_client.c:4100,4103,4158`; `:6213`), and both writing the test's own count and copy of the
  last indicator event from their event callbacks (`ra-session-test.c:306-307`). The new run's
  checks vary too. With symbolizing off (`TSAN_OPTIONS=symbolize=0`), three runs each failed the
  reset-waited and last-event checks, as without ThreadSanitizer. With symbolizing on, four runs
  all passed the reset-waited check; two of them failed the checks that the indicator showed and
  hid once each and that its last event hides the challenge, and two passed every check.
  DERIVED: symbolizing the first report holds up the reset thread until the hold has ended, and
  the rest of the reset then overlaps the rest of the frame or comes after it; a hide is lost
  when the frame clears the achievement's pending events after the reset has set it
  (`rc_client.c:6213`). A copy without the lock in frames fails this run and the frame run of the
  list. None of those runs reset the game while the emulator thread was in idle work; the next
  bullet adds one that does.
- OBSERVED (host build): a run added later resets the game while the emulator thread is in idle
  work. As in the idle-work run of the list, the log hook holds the emulator thread where the idle
  work that finishes the game's load checks the game's addresses, after rcheevos has set the game
  and released its mutex (`rc_client.c:1815-1823,1861`; `:1304,1310`), while another thread resets
  the game. The reset must return only after the hold has ended, and the load must still mark the
  achievement at the bad address as unsupported. All twenty-six runs pass with GCC 16.2.1 and clang
  23.1.3, and under clang ASan, UBSan, LeakSanitizer and ThreadSanitizer, which report only what
  the bullet above names. A copy without the lock in `ra_glue_reset`, and the glue of `4ad1f8b16`,
  fail the new run's check that the reset waited and the two checks of the frame run named above;
  a copy without the lock in `ra_glue_idle` fails the new run's check that the reset waited and the
  idle-work run of the list. Each of these copies also fails an older run; what the new run adds
  is a reset made during idle work, and the races ThreadSanitizer reports for it. Under
  ThreadSanitizer the copy without the lock in `ra_glue_reset` failed the new run's check that the
  reset waited in all eleven runs, seven with symbolizing on and four with it off. With symbolizing
  on, the frame run's checks vary, as the bullet above records: three of the seven runs failed some
  of them; with it off, all four did. In the seven symbolized runs the new run reported the reset
  reading the game (`rc_client.c:6643`) that the load sets (`:1817`) each time, and reading an
  achievement's state (`:4866`) that the load's address check writes (`:1304`) six times. DERIVED:
  a reset and idle work take turns, as a reset and a frame do.
- OBSERVED (host build): `./gradlew :app:testDebugUnitTest --rerun :app:assembleDebug` passes with
  every unit test. `ra_glue.c` was recompiled for all four ABIs with empty ndk-build stderr logs;
  in each ABI's object `ra_glue_reset` calls `pthread_mutex_lock`, `rc_client_reset` and
  `pthread_mutex_unlock` in that order (on arm64 the lock goes through a helper the compiler
  outlined), and each packaged core library has the build ID of the library linked from that
  object.
- OBSERVED (device, 2026-10-23): a reset from the in-game menu while emulation was paused started
  the game again at the Nintendo logo, and the achievement list opened afterwards (see "Device
  verification (debug APK `1193373b` with the reset fix, Retroid Pocket 6, 2026-10-23)"). Not yet
  verified on device: a reset while a frame or idle work raises an event, and a reset from the
  reset button mapping.
- DERIVED: the app keeps the header's rule. rcheevos unloads the game when `ra_glue_load_game`
  replaces it (`ra_glue.c:1058-1059`), on logout (`rc_client.c:887-888,894`), and on a reset whose
  media belongs to another game (`rc_client.c:6643-6652`). The app reaches none of these during a
  session: it loads one game per session, after the first successful sign-in
  (`CoreService.java:1535`); nothing calls `RetroAchievementsManager.logout()`
  (`RetroAchievementsManager.java:226-228`); and the glue never changes media, so the hash of the
  loaded game maps to that game (`rc_client.c:2329,2334-2335`; an unknown game keeps both at 0:
  `:2320,2427,2620-2622`). The client is destroyed by `ra_glue_shutdown` from
  `CoreService.onDestroy` (`CoreService.java:1313,1401`), on the main thread like every caller of
  the getters (`AchievementsDialog.java:50-53`; `CoreService.java:1505,1527`), and
  `ra_glue_shutdown` waits for a server response that an HTTP thread is handling
  (`ra_glue.c:951-958`). On a normal exit the service stops itself only after `emuStart` returns
  (`CoreService.java:926,987,1566-1579`); its other exits call `forceExit`, which kills the
  process right after asking the service to stop (`CoreService.java:1559-1563`).

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
  (`main.c:1997-2001`), the pause loop runs inside `emuStart` (`CoreService.java:926`), and the
  service destroys the client only after `emuStart` returns, or kills the process (see "Threads
  in the glue").
- DERIVED: outside netplay, the achievement dialog opens only from the in-game menu, which pauses
  emulation (`GameActivity.java:460-465,963-964`), and a game screen that opens anew for a
  running session reads the list for its overlay before emulation resumes (see "Threads in the
  glue"). Until this change no rcheevos work on the emulator thread could run while either built
  its list in that case. After it, a game load that finished during the pause could, and its
  address check changed achievements' list groups outside rcheevos's mutex. The list now waits
  for the idle work or frame that is running to end (see "Threads in the glue"). In netplay
  neither the menu nor `onStop` pauses emulation, so there both reads could overlap a frame
  before this change too.
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
  (`RetroAchievementsManager.java:297-308,417-454`). It answers a network failure (`IOException`)
  with status -2 and "RA network error: <message>", as before, and any other runtime exception
  with status -2 and "RA request error: <exception>", so every request still gets one answer
  (`:446-452`). Before, `onServerCall` caught only `IOException`. REFERENCE (AOSP
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
  client (`ra_glue.c:107-110,951-967`). While it is set, `ra_glue_server_call` gives a request no
  slot and answers it at once with status -1, `RC_API_SERVER_RESPONSE_CLIENT_ERROR`
  (`ra_glue.c:523-525,558-576`), which rcheevos does not retry (`rc_client.c:629-631`): the unlock
  ends with a server error, and rcheevos frees its data (`rc_client.c:4669-4674,4753-4758`).
- DERIVED: before, an answer handled during shutdown could send an immediate retry
  (`rc_client.c:4675-4680,5377-5382`) into a slot taken after the drop, and the answer to that
  retry could reach the destroyed client. Answering it with HTTP 503 instead would leave a retry
  scheduled that never runs, as `rc_client_destroy` neither runs nor frees scheduled work and
  unloading the game removes only its ping (`rc_client.c:191-227,3185-3198`). The app kills its
  process right after the shutdown (`CoreService.java:1313-1315`) and removes its listener before
  it (`:1400-1401`), so the server error shows no notice.
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
- OBSERVED (device, 2026-10-21): a hardcore session refused fast speed, from the speed item and
  from the Game speed dialog, and switching the limiter off, and its menu showed the limiter on;
  a softcore session allowed fast speed (see "Device verification" for build
  `3.0.338 017e3557`). DERIVED (in that section): hardcore turned the limiter on when the session
  started. Not yet verified on device: slow speeds and frame advance in either mode, and
  switching the limiter off in softcore.

### Hardcore session start
- OBSERVED: `startRetroAchievements()` returns at once on every start command after the
  service's first (`mRaStartAttempted`), before the settings check, `initialize()`,
  `setHardcore`, login and `loadGame`.
- DERIVED: a new `GameActivity` for the running game (notification intent,
  `CoreService.java:1154-1173`, or resume broadcast, `:240-243`, after the old activity was
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
  (`CoreService.java:421-433`, called by `loadSlot` and `loadState`, `:515-531`). A GameShark
  press, which shared the old "save states and load states are disabled" text, shows "Hardcore
  mode: cheats are disabled" (`:592-598`).
- OBSERVED: in `9e8066d30` a hardcore start skipped the auto save (`CoreService.java:895-899` at
  `9e8066d30`) and started the game from power-on without saying so. Now the emulation job reads
  the restart flag and the latest auto save once (`CoreService.java:718-723`), right after it
  has copied the game's data back from external storage when that setting is on (`:713-716`).
  If that auto save exists, `leaveRaHardcoreForResume` turns hardcore off on that thread and
  posts the notice "Resumed game: hardcore mode is off for this session" to the main thread
  (`:398-419`). The job then sets the frame limiter and the cheats (`:884-899`) and loads that
  same file, only if it exists (`:907-910`). In `9e8066d30` a start that was not a restart (Resume
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
  notice is posted to the main thread.
- OBSERVED: the job now posts the cheats toast, "Hardcore mode: cheats are disabled", through the
  same method as the resume notice (`CoreService.java:398-406,417,889`). In the compiled job the
  cheats branch now calls that method instead of `Notifier.showToast` (`javap`). From `81ac3fb74`
  to this change the job showed that toast from its own thread. DERIVED: so on Android 6 to 10 it
  never showed. Not verified on a device running Android 6 to 10.
- OBSERVED: with game data on external storage, a session that ends normally copies the game's
  data folder to that storage and, if the copy succeeds, deletes the local copy
  (`CoreService.java:948-950,1012-1044`; the deletion `:1036-1041`), and the next session's job
  copies it back (`:991-1010`). The auto saves are in that folder (`GamePrefs.java:764`).
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
  on the HTTP thread that delivers them (`RetroAchievementsManager.java:305`). The sign-in starts
  from `onStartCommand` and does not wait for the job, so that request can go out before the
  switch when the job is late, for example with a slow copy from external storage, a large
  texture-dump folder to delete first (`CoreService.java:711`) or a Raphnet adapter it waits for
  (`:1597,1614`). The server then sees a session that started in hardcore switch to softcore,
  which the page allows: "Switching from hardcore mode to casual mode is allowed mid-session."
  (section B).
- OBSERVED: `leaveRaHardcoreForResume` writes the glue's `s_hardcore` on the emulation thread,
  while the main thread reads it through `ra_glue_get_hardcore()` for every hardcore check
  (`CoreService.java:384-387`). It was a plain `int` (`ra_glue.c:49,471,478` at `7aa5bdb3e`),
  and a scratch ThreadSanitizer build in which one thread turns the mode off while another reads
  it reported that data race; it is now atomic (`ra_glue.c:68,1011-1022`), and the same build
  reports none. DERIVED: the mode only turns off during a session, so a stale read could only
  keep a hardcore check on for longer.
- No automated test: `CoreService` is an Android service, and the app's unit tests run on the
  host JVM without Android. OBSERVED (host build, `javap`): the compiled job calls
  `copyGameContentsFromSdCard`, then reads `mIsRestarting` and calls `getLatestAutoSave` once,
  checks that the file exists and calls `leaveRaHardcoreForResume`, all before it sets the frame
  limiter and the cheats, and calls `emuLoadFile` with that path, only when the file exists,
  before `emuStart`. `leaveRaHardcoreForResume` calls `setHardcore(false)`, then posts the code
  that shows the notice to a `Handler` on the main looper.
- OBSERVED (device, 2026-10-21): in hardcore, saving to a slot worked and loading it, or an auto
  save, was refused; exiting wrote an auto save; Resume then showed the notice, the achievement
  list said "Casual" and loading the slot worked; the next start from the gallery stayed in
  hardcore with no resume notice while the auto save existed. DERIVED: so it was the gallery's
  Start, and Start keeps hardcore (see "Device verification" for build `3.0.338 017e3557`). Not
  yet verified on device: saving to and loading from a file, and Resume with game data on
  external storage. The Retroid runs Android 13, so it cannot show the notice on Android 6 to 10.

### Achievement progress kept with save states
- REFERENCE (compliance page revision `361119b677dc`, section A): "Hit counts should be stored in
  save states. While not strictly required, this is highly recommended as it's important for
  debugging complex achievement logic and ensures proper functionality in casual mode."
- OBSERVED: the core calls `ra_glue_state_saved` after a save and `ra_glue_state_loaded` after a
  load, only when the save or load function reports success, with the state's path and the
  console RAM (`savestates.c:1478-1479,2221-2222`; declared without `EXPORT`, `ra_glue.h:48-54`).
  Both run on the emulator thread, which runs the core's save and load jobs
  (`interrupt.c:554-556,654-656`). A save writes the loaded game's progress, as rcheevos
  serializes it (`ra_glue.c:1464-1484`), to
  `<state folder>/.ra-progress/<state file name>.progress` (`ra_glue.c:1344-1358`): "RAPROG01",
  then the RAM's CRC-32, the RAM's size and the progress's size as little-endian 32-bit values,
  then the progress (`ra_glue.c:1336-1340,1390-1424`). It writes a temporary file and renames it
  over the old one, and if that fails it removes the old file (`ra_glue.c:1419-1420`). With
  nothing to keep, as with achievements off or before the game has loaded, the save removes the
  old file (`ra_glue.c:1511-1530`). So a state saved before the game has loaded keeps no
  progress, even while progress from a state loaded earlier waits for the game (next bullet).
- OBSERVED: an M64P save reports success once it has passed the file write to `queue_work`, even
  if that write then fails to open or write the file (`savestates.c:1492-1523,1939-1942`). The
  app's core build, `mupen64plus-core/mupen64plus-core.mk`, does not define `M64P_PARALLEL` or
  compile `workqueue.c` (only upstream's `projects/unix/Makefile:162` defines it), so `queue_work`
  runs the write at once, before the save hook (`workqueue.h:42-63`). A PJ64 zip save also reports
  success when its write fails (`savestates.c:2133-2164`), but the app saves only M64P states
  (`CoreInterface.java:834,850` pass type 1, `savestates_type_m64p` in `savestates.h:35-40`).
- OBSERVED: a load reads that file and applies it only if the RAM size and the CRC-32 in it match
  the RAM the core has just restored (`ra_glue.c:1428-1461`). Otherwise, or without a file, rcheevos
  is given no progress, which starts every count from zero (`ra_glue.c:1487-1492`;
  `rc_client.c:6891-6893`). A load before the game has finished loading, as when a session
  resumes from its auto save, keeps the progress until the first `ra_glue_do_frame` or
  `ra_glue_idle` after the game has loaded (`ra_glue.c:112-120,979-999,1501-1509,1532-1552`).
  `ra_glue_reset` runs on another thread (see "Threads in the glue"), so it only marks that
  progress to be dropped, and the emulator thread drops it (`ra_glue.c:1001-1009`).
- OBSERVED: before this change the app kept one copy of the progress per game, in its
  `ra_progress` preferences: it saved it when emulation stopped and restored it once the next
  session's game had loaded, whatever that session started from, and loading a state kept the
  progress from before the load (`CoreService.java:922-925,1403-1430,1503-1508` at `7aa5bdb3e`).
  That code and `RetroAchievementsManager.serializeProgress` and `deserializeProgress` are gone.
  The glue's `ra_glue_serialize_progress` and `ra_glue_deserialize_progress` stay exported, and
  the app no longer calls them (`ra_glue.h:91-98`). The old `ra_progress` file stays on devices,
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
- OBSERVED (device, 2026-10-21): a slot save and the auto save written at exit each wrote its
  progress file under `.ra-progress` in its own folder (see "Device verification" for build
  `3.0.338 017e3557`). Not yet verified on device: a slot or a file saved during play brings its
  counts back when it loads; Resume brings back the auto save's counts; the progress files stay
  out of the file prompts; the cleanup deletes them with old auto saves (the auto save it deleted
  that day had none); copies to and from external storage, and a Google Drive backup and
  restore, keep the `.ra-progress` folder.

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
  `:698-708`), and `ra_glue_async_cb` passes it to Java (`ra_glue.c:503-514`). A failed HTTP
  request becomes `RC_NO_RESPONSE` (`RetroAchievementsManager.java:446-452`,
  `rc_api_common.c:360-365`). Only -34 and -35 are retried with the password.
- OBSERVED: `loadGame` is requested only after a login succeeds. rc_client results carry no
  request ID (`ra_glue_async_cb`, `ra_glue.c:503-514`), so the session makes one request at a
  time and `mRaLoginCredential` tells which login a result belongs to. A request call that returns
  false ends the session at once, as before. It returns false when `ra_glue`'s own checks refuse
  without a callback (`ra_glue.c:1026-1027,1036-1037,1055-1056`), when rc_client refuses, which
  it also reports through the callback (login `rc_client.c:766-775,800-813,833-846`; load
  `:2444-2461,2927-2948`), and when the request finishes before the call returns (login
  `:786-792`, load `:2900`). `onRaAsyncResult` ignores any result that arrives after that.
- DERIVED: in that last case a successful login is dropped, and the session runs without
  achievements and with enforcement released. It needs the main thread to stall for a whole HTTP
  round trip (responses arrive on the HTTP thread, `RetroAchievementsManager.java:305`), so it
  should be rare. In `fd6b452f1` the same case latched the failure but still loaded the game, so
  achievements would run with enforcement released.
- DERIVED: the password login starts from the token login's result, which is posted to the main
  thread after rc_client has cleared its login state (`rc_client.c:700-708`), so rc_client does
  not refuse it as "Login already in progress" (`:757-760`).
- OBSERVED: the token from a successful login is written with `commit()`, and only when it differs
  from the saved one. `forceExit` kills the emulation process (`CoreService.java:1559-1562`).
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
    own "RA network error: <message>" (`RetroAchievementsManager.java:448`), and after any other
    failure of the request "RA request error: <exception>" (`:452`); rcheevos passes either on as
    the error message (`rc_api_common.c:360-365`).
- OBSERVED: two start-up exits that ended achievements without a notice now call `failRaSession`:
  a game started without an MD5 (`CoreService.java:1340-1344`) and a failed
  `RetroAchievementsManager.initialize()` (`:1347-1351`). Both come before `mRaGameMd5` is set
  (`:1357`), so `isRaHardcore()` was already false and hardcore was never enforced; only the
  notice is new. The service reads the MD5 from its start intent (`CoreService.java:1239`); the
  gallery computes it when it scans a game (`CacheRomInfoService.java:490,534`). `initialize()`
  fails only when `rc_client_create` cannot allocate the client
  (`RetroAchievementsManager.java:159-162`; `ra_glue.c:926-930`; `rc_client.c:169-171`). Neither
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
  softcore. OBSERVED (device, 2026-10-21): it showed about six seconds after the mode notice in
  each of three hardcore sessions and in neither softcore session (see "Device verification" for
  build `3.0.338 017e3557`).
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
  `RetroAchievementsManager.java:446-452`; `rc_client.c:625-627`), and an answer without an error
  message is retried as well. So an error page with any other status, for example 500 or 403, ends
  the request with a server error instead of a retry. The app passes on only HTTP statuses and -2
  (`RetroAchievementsManager.java:440,447,451`). The first retry is sent at
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
  (`ra_glue.c:748-766`), and the server-error JSON carries it as `"title"`, empty when the lookup
  finds nothing (`:820-834`). The disconnected and reconnected events, which
  `ra_build_event_json` dropped before, arrive as `{"type":"disconnected"}` and
  `{"type":"reconnected"}` (`:836-842`). `CoreService.onRaEvent` shows the notices
  (`CoreService.java:1407-1437,1452-1464`).
- OBSERVED: the glue looks the title up in its event handler, on the thread that raises the
  server error. A refused answer (`rc_client.c:4669-4674`, `:5375`) and a failed allocation for a
  retry (`:4691,5393`) raise it in the response callback, which runs on the HTTP thread that
  delivers the answer (`RetroAchievementsManager.java:305`). When the glue has no free request
  slot, it answers at once on the sending thread with HTTP 503 (`ra_glue.c:558-576`), which
  rcheevos always retries (`rc_client.c:593-595`), so there only a failed allocation for that
  retry can raise one. While the client shuts down, the glue answers with status -1 instead, which
  rcheevos does not retry, so an unlock or leaderboard entry ends with a server error there (see
  "Network requests and shutdown"). A failed allocation when a frame starts a request
  (`rc_client.c:4844,5485`) raises it on the emulator thread, after the frame has released
  rcheevos's mutex (`rc_client.c:6185-6186,6359,6465-6467`).
  `rc_client_get_achievement_info` takes that mutex, which is recursive, to update the
  achievement's display data (`rc_client.c:4485-4531`; `rc_compat.c:230`);
  `rc_client_get_leaderboard_info` takes none (`rc_client.c:4900-4922`). The glue holds no lock of
  its own while rcheevos handles an answer (`ra_glue.c:607-637`), and `ra_glue_shutdown` waits
  until that handling ends (`ra_glue.c:951-958`). The disconnected and reconnected events come
  from the emulator thread. `RetroAchievementsManager` posts every event to the main thread
  (`RetroAchievementsManager.java:321`).
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
  opened the game (see "Device verification" for build `3.0.338 06aedfe1`). OBSERVED (device,
  2026-10-21): the hardcore mode toast showed at each of three hardcore starts, and the warning
  about six seconds after it (see "Device verification" for build `3.0.338 017e3557`). OBSERVED
  (device, 2026-10-22): a toast window from the app opened 26 ms after the app logged an unlock,
  the only one in the almost five minutes that followed, and the tester, asked whether
  "Achievement unlocked: Match Winner (3 points)" popped up, said it did (see "Device
  verification" for build `3.0.338 11f5f3b9`). Not yet verified on device: whether long notices
  are cut at two lines, the failure toast for a ROM RetroAchievements does not know and for a
  wrong password with no valid saved token, and the sending-failed and connection-restored
  notices when the network is off for an unlock and comes back while the game runs. A refused
  unlock is hard to cause on purpose.

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
  (`ra_glue.c:56-59`).
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
  rcheevos bucket it is listed in (`ra_glue.c:1201-1206`). `"unsupported"` is 1 in the "Unsupported"
  bucket. rcheevos puts an achievement there when its logic cannot be parsed
  (`rc_client.c:2113-2116,2126-2129`), or when its logic uses a bad address
  (`rc_client.c:1293-1314`). When the game loads, rcheevos checks every address once: an address
  past the last region of the console's memory map is bad without a read, and so is one the memory
  read callback refuses (`rc_client.c:1346-1370`, called at `:1861`). During play, a failed read
  makes its address bad, unless the read followed a pointer
  (`rc_client.c:5864-5876,5895-5896,5937-5938`). The bucket never changes afterwards
  (`rc_client.c:4100-4101`). `ra_glue_read_memory` refuses an address only while `g_mem_base` is
  NULL, or at 16 MB and above (`ra_glue.c:148-152`); "Memory reads" describes what it refused
  before. The dialog shows an unsupported achievement as "Unsupported", or as "Unlocked" if the
  player already earned it in this mode (`rc_client.c:1686-1689`). `"set"` is the set's title from
  `rc_client_get_subset_info` when the game has more than one active set, and empty otherwise,
  because rcheevos sets a bucket's `subset_id` only then (`rc_client.c:4297-4301,4420`).
- OBSERVED: with the lock-state grouping the glue requests (`ra_glue.c:1180-1182`), rcheevos lists
  all of one set's buckets before the next set's (`rc_client.c:4397-4431`), and the dialog adds a
  heading row whenever the set title changes. DERIVED: two sets with the same title in a row would
  share one heading.
- OBSERVED: long titles wrap. The shared row layout keeps the title on one scrolling line
  (`list_item_two_text_icon.xml:36-38`) and other dialogs use it, so `AchievementsDialog` turns that
  off for its own rows.
- OBSERVED: the glue returns 0 both when it fails and when the list does not fit the buffer
  (`ra_glue.c:1183-1184,1267-1271`). The list used to be read into a fixed 128 KB buffer, so a
  longer list showed "No achievements are loaded". `RetroAchievementsManager.readText` now grows
  the buffer fourfold up to 2 MB for the list (`ACHIEVEMENTS_BUFFER_MAX`). "No achievements are
  loaded" now shows only when no game is loaded; a loaded game whose list cannot be read
  (over 2 MB, or JSON the dialog cannot parse) shows "Could not read this game's achievement list."
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
  "Device verification" for build `3.0.338 06aedfe1`). OBSERVED (device, 2026-10-21): in hardcore
  it read "Hardcore: 0 of 97 unlocked" under the game's title and still listed the warning row,
  and after Resume it read "Casual" (see "Device verification" for build `3.0.338 017e3557`).
  OBSERVED (device, 2026-10-22): after an unlock, that achievement's row showed a ticked box and
  "Unlocked, 3 points" (the tester's screenshot; see "Device verification" for build
  `3.0.338 11f5f3b9`). Not yet verified on device: the item shows only with RetroAchievements
  enabled; a game with a bonus set shows set headings; long titles wrap.

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
  the progress text as well, and progress_hide with no other field (`ra_glue.c:786-818`). Each
  achievement in the list JSON also has `"progress"`, the same text, and `"challenge"`, 1 while
  it is primed (`ra_glue.c:1215-1223,1228,1251`): rcheevos refreshes both when it builds the list
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
  `CoreService.onRaEvent` does not log progress updates (`CoreService.java:1408-1411`).
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
  loads the core library (`RetroAchievementsManager.java:102-114,126`). Its other callers in the
  emulation process are `CoreService` methods (`CoreService.java:384,416,1346,1399,1488`). In
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
  (`ra_glue.c:852-896`).
- OBSERVED: `CoreService.onRaEvent` shows a long toast for each leaderboard event: "Leaderboard
  attempt started: <title>", "Leaderboard attempt failed: <title>" and "Submitted <value> for
  <title>" (`CoreService.java:1415-1424`; `strings.xml:295-297`). It logs neither progress nor
  tracker updates, which can come every frame (`CoreService.java:1408-1411`).
- OBSERVED: `RaIndicatorOverlay` shows "Leaderboard: <value>" for each tracker, between the
  challenge lines and the progress line, in the order the trackers were shown; an update changes
  its line in place (`RaIndicatorOverlay.java:50-53,96-106,156-174`;
  `RaIndicatorState.java:31-42`; `strings.xml:299`). The lines use a fixed-width font, and the
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
  16.2.1 and clang 23.1.1. In copies of the test (the server answers that entry with status 200),
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

### Frame advance during netplay
- OBSERVED: frame advance now does nothing during netplay, as pausing and switching the frame
  limiter already did (`CoreService.java:446-454,456-470,477-486,584-590`). It is reached only
  from a controller button mapped to it (`PeripheralController.java:349-351`;
  `CoreFragment.java:465-473`). In the compiled method the netplay check now comes first
  (`javap`). Before, `advanceFrame` refused it only under hardcore, and netplay runs in softcore
  (`CoreService.java:1366`). Not yet verified on device.
- DERIVED: so before this change a frame advance during netplay paused the core after one frame
  (`main.c:510-515,859-863`), and as the core refuses to toggle the pause while netplay is active
  (`main.c:481-482`), the game could not be resumed that way: another frame advance ran one more
  frame and paused again. Not seen on a device.

### Unlocks kept until the server answers
- REFERENCE (compliance page revision `361119b677dc`, section A): "Unlocks created while offline
  must be securely cached and sync to RetroAchievements when connectivity returns."
- DERIVED: until this change an unlock or a leaderboard entry that still waited for a retry when
  the game closed was never sent. rcheevos keeps it only in memory, and `rc_client_destroy` frees
  the client without sending it (`rc_client.c:191-227,3185-3198`); the app destroys the client
  when the session ends (`CoreService.java:1313,1401`; `ra_glue.c:963`). The progress kept with a
  save state holds only the runtime state of active achievements (`rc_client.c:6762,6825-6826`),
  so a later session started that achievement locked again, from the server's unlocks.
- OBSERVED: the glue now keeps each unlock and leaderboard entry that rcheevos sends in a file,
  from its first request until the server records it or refuses it in a way rcheevos does not
  retry (`ra_glue.c:208-226`). It reads the achievement or leaderboard, the hardcore flag or the
  score, the game's hash and the player's name from the request's form data and, if the file does
  not hold that entry yet, rewrites the file before it hands the request to Java
  (`ra_glue.c:302-334,406-423,543-552`). The file is plain text with no token, rewritten through a
  temporary file and a rename (`ra_glue.c:345-403`). The app keeps
  it as `retroachievements-unsent.txt` in its folder of files that Android does not back up, and
  names it only from the game's process (`CoreService.java:1352-1355`; `AndroidManifest.xml:418`;
  `RetroAchievementsManager.java:199-208`; `ra_glue.h:63-67`). Settings' Connect check signs in
  from the app's main process (`AndroidManifest.xml:208-213`; `LoginPreference.java:82,108`). The
  new entry point is exported and declared for JNA (`api_export.ver:74`; `CoreLibrary.java:110`).
- OBSERVED: the glue drops an entry when the server's answer reads, as rcheevos reads it, as
  recorded, which includes an unlock the player already has, or as a refusal rcheevos does not
  retry (`ra_glue.c:440-483,620-635`; `rc_client.c:497-516,586-640,4666-4674,5368-5376`;
  `rc_api_runtime.c:706-716`). An answer the glue gives itself, when no request slot is free or
  the client is shutting down, never counts as the server's (`ra_glue.c:558-576`), so an unlock
  that rcheevos ends with a server error during shutdown (see "Network requests and shutdown")
  stays in the file.
- OBSERVED: after each sign-in, the glue sends the signed-in player's entries again, one at a
  time, with the seconds since each was first sent in the request's `o` parameter
  (`ra_glue.c:670-729`; `rc_api_runtime.c:649-650,659-666,758-759`). The first is handed to Java
  before the app hears of the sign-in and asks for the game (`ra_glue.c:733-746,1030,1040`); Java
  answers on two threads, so the game's requests can still be answered first
  (`RetroAchievementsManager.java:127`). After an answer rcheevos would retry, the rest wait for
  the next sign-in. Once the server has recorded or refused any, the glue raises one
  `unsent_done` event with both counts (`ra_glue.c:648-666`), and the app shows
  "RetroAchievements recorded 2 unlocks or leaderboard entries from an earlier session", or "did
  not record" for refusals (`CoreService.java:1431-1432,1466-1478`; `strings.xml:287-294`).
- DERIVED: an entry sent again meets the same server rule for hardcore unlocks (see "Hardcore
  unlocks"). Another player's entries wait until that player signs in. The file keeps at most
  256 entries (`ra_glue.c:227`). If it cannot be rewritten it keeps its old entries, so one the
  server has answered since goes out again later, and the server answers a repeated unlock that
  the player already has it. An entry from an earlier session that rcheevos sends during the
  session, before the resend reaches it, goes out twice: keeping it again does not mark it as
  sent by this sign-in (`ra_glue.c:406-423`). An entry's age comes from the device's clock.
- OBSERVED (host build): six new runs of the session test name a file in the test's state folder.
  In a hardcore session, an unlock that gets the app's answer for a server it cannot reach
  (status -2 with the error's text, `RetroAchievementsManager.java:446-448`) and an entry that a
  proxy answers with an HTTP 503 page raise no server error and are in the file when the session
  ends; the next sign-in sends them again, one at a time, only the first before the app hears of
  the sign-in, with their hardcore flag, score and hash, and the file is empty once the server
  records them. Entries from a file written by the test go out with their age (an hour and two
  hours), another player's entry stays, and a line that holds no entry is dropped. Unlocks the
  server refuses, or already has, are dropped; after a 503 the unlock stays and the next is not
  sent. An unlock recorded and an entry refused during a session leave nothing. All twenty-four
  runs pass with GCC 16.2.1 and clang 23.1.1, and under clang ThreadSanitizer with no report.
  Under clang ASan and UBSan they pass, UBSan reports only the two vendored sites named under
  "On-screen notices", and LeakSanitizer reports only the two retries that the first run leaves
  pending when its first session ends (see "Open items"). Copies of the glue fail only the runs
  for their fault: with the file name ignored, all six; without keeping entries as they are
  sent, the first run and the in-session run; without the resend, the five resend runs; keeping
  refused entries, the refusal run and the in-session run; dropping entries after a 503, the
  first run and the run that keeps an unlock; reading the app's answer for a server it cannot
  reach as a refusal, with no case for it in the copy of rcheevos's retry rule or with no retry
  rule at all, only the first run; with the HTTP statuses left out of that copy, only the first
  run, as rcheevos takes the 503 page's title as error text; without the age, or sending another
  player's entries, the file run; going on after a 503, the run that keeps an unlock; sending the
  first entry after the app hears of the sign-in, only the first run's order check; sending the
  second entry with the first, or right after the app hears of the sign-in, the five resend runs,
  the first of them also at its order check.
- OBSERVED (host build): `./gradlew :app:testDebugUnitTest --rerun :app:assembleDebug` passes with
  every unit test. `ra_glue.o` was recompiled for all four ABIs with empty ndk-build stderr logs,
  and each packaged core library exports `ra_glue_set_unsent_path` (`llvm-nm -D`).
- OBSERVED (device, 2026-10-22): after an unlock, the file was last written about a third of a
  second after the app logged the unlock, and then held only its comment line; DERIVED: the
  server had recorded the unlock (see "Device verification" for build `3.0.338 11f5f3b9`). Not
  yet verified on device: an unlock made with the network off, the session closed, and the game
  started again with the network on, which should show the "recorded" notice and leave the file
  empty.

### Failed save-state writes
- OBSERVED: before this change, an M64P save that could not open its file, or could not write all
  of it, returned without releasing `savestates_lock` and without freeing the state's path and
  work item (`savestates.c:1503-1517` at `7f2379e26`). REFERENCE: in upstream mupen64plus-core's
  `master` (`3be6336a`) the same two returns keep the lock, though there they also report the
  failure to the front end (`src/main/savestates.c:1525-1541` there).
- OBSERVED: the lock is a pthread mutex made with default attributes
  (`ndkLibs/SDL2_stub/SDL.h:221-224,460-496,521-532`). REFERENCE: bionic's default mutex type is
  `PTHREAD_MUTEX_NORMAL` (NDK 26.1.10909125, `sysroot/usr/include/pthread.h:46,53`), and POSIX
  says that a thread relocking such a mutex deadlocks (POSIX.1-2017, `pthread_mutex_lock`).
- OBSERVED: the app's core build runs the write at once, on the emulator thread that asked for the
  save (see "Achievement progress kept with save states"). Loading an M64P state whose file opens
  takes the same lock (`savestates.c:203`, called at `:1473`), and so does an M64P save
  (`:1498`). A load whose file is missing or cannot be opened stops before that
  (`:1436-1441,1456-1461`), and loading a PJ64 state never takes the lock (`:963,1320,1355`).
- DERIVED: so after one failed write, as with full storage or a state folder that is gone, the
  emulator thread's next M64P save, or its next load of an M64P state that exists, waited for
  itself and the game froze. Not seen on a device.
- OBSERVED: both returns now free the path and the work item and release the lock, as a
  successful write does (`savestates.c:1503-1531`).
- OBSERVED, unchanged: a failed write is still reported as a saved state (see "Achievement
  progress kept with save states"). DERIVED: so the app also marks a failed auto save as
  complete, unless it cannot create the marker file either, as when the folder is gone
  (`CoreService.java:325-342`).
- OBSERVED (host build): `tools/test-savestate-lock.sh` builds `tools/tests/savestate-lock-test.c`,
  which includes `savestates.c` and hands it states through `queue_work` as `savestates_save_m64p`
  does (`savestates.c:1939-1940`): one written and read back, one to a missing folder, 1 MiB that
  zlib cannot shrink written to `/dev/full`, and one more written. After each, the lock must be
  free. It passes with GCC 16.2.1 and clang 23.1.3, and in a scratch build under clang ASan,
  UBSan and LeakSanitizer with no reports. With the `savestates.c` from before this change it
  stops at the missing folder, with the lock still held, and LeakSanitizer reports the path and
  the work item (98 bytes in 2 allocations, with `TMPDIR` unset); a copy that releases the lock
  only after a failed open stops at `/dev/full`.
- OBSERVED (host build): `./gradlew :app:assembleDebug` recompiled `savestates.o` for all four
  ABIs with empty ndk-build stderr logs, and each packaged core library has the build ID of the
  one that was linked. In the arm64 object, the failed open, the failed write and the successful
  write all end in one shared tail that frees the three allocations and then jumps to
  `SDL_UnlockMutex` (NDK `llvm-objdump -d -r`).

### Benchmark result toast
- OBSERVED: with "Benchmark mode" on in the display settings (`preferences_display.xml:136-140`;
  `strings.xml:431-432`), a session that runs the parallel plugin outside netplay averages the
  frame rate for 30 seconds, then shows the result in a toast and stores it under the GPU driver's
  name, which is empty for the device's own driver, so results with that driver are not stored
  (`CoreService.java:220,912-923,1667-1688,1700-1716`). That toast now goes through the method
  that posts the resume and cheats notices to the main thread, which now passes format arguments
  on (`:398-406,417,889,1714-1715`). This is outside RetroAchievements; it was an open item here.
- OBSERVED: `Notifier` has one `showToast` for a string resource, and it takes format arguments
  (`Notifier.java:79-82`), so the resume and cheats notices, which pass none, make the same call
  as before.
- DERIVED: the frame rate reaches `onFpsChanged` on the emulation thread. The parallel plugin
  swaps buffers from the core's screen update in its frame interrupt (`vi_controller.c:179`;
  `mupen64plus-video-parallel/upstream/gfx_m64p.c:281-284`; `parallel_imp.cpp:134,144`;
  `glguts.c:400`). The plugin's swap is the core's `VidExt_GL_SwapBuffers` (`glguts.c:296`), which
  calls the swap that ae-bridge gives the core (`CoreInterface.java:578`; `ae_bridge.cpp:577-581`;
  `frontend.c:401-406`; `vidext.c:84-86,684-688`; `ae_bridge.h:70`), and that swap counts frames
  and calls back into Java on that thread (`ae_bridge.cpp:397-407,453-460`;
  `CoreInterface.java:207-211`), whose listener is the service (`CoreService.java:1271`). So until
  this change the result toast was made on that thread, and on Android 6 to 10 it never showed
  (see the `Toast` REFERENCE under "Save states and Resume under hardcore"); from Android 11 it
  shows either way. Not verified on a device running Android 6 to 10; see the emulator run below.
- OBSERVED (device, 2026-10-21): each part of the toast's new route has shown on the Retroid
  Pocket 6: a notice posted through that method (the resume notice) and a toast made by the same
  `Notifier.showToast` with a format argument ("Saving slot 0…", `CoreService.java:511`); see
  "Device verification (debug build `3.0.338 017e3557`, Retroid Pocket 6, 2026-10-21)". This
  change itself was not run on a device.
- OBSERVED (Android 10 x86_64 emulator, 2026-10-23, one run of each build): the parallel plugin
  does not start on this emulator (Granite: "VK_KHR_16bit_storage for SSBOs is not supported!
  This is a minimum requirement for paraLLEl-RDP."), so two test builds of `017042299` dropped the
  parallel-plugin check from the benchmark's condition (`CoreService.java:913-914`) and ran it
  with Glide64. Both also had the ae-bridge fix of PR #20 (branch
  `fix/non-arm64-game-start-crash`), without which every game start crashes on x86_64. The build
  with `CoreService.java` as it was before this change (from `1530dde6d`) logged the result,
  "Benchmark: avg 15.7 FPS ()", on the emulation thread, the one that had attached the plugins.
  No toast window showed in the 10 seconds until the game was closed, none of six screenshots
  taken from about 0.4 to 4.6 seconds after the line shows a toast, and the emulator's log has no
  line that mentions a toast. The build with this change logged the result, "Benchmark: avg 15.9
  FPS ()", on that thread too. The window list, read about every 0.3 seconds, showed a toast
  window from 0.2 to 1.8 seconds after the line, allowing for the emulator's clock (0.4 seconds
  behind the PC's, measured two to three minutes after the runs); a screenshot shows the toast's
  text, "Benchmark: avg 15.9 FPS ()"; and 2.0 seconds after the line, by the emulator's own clock,
  its notification service logged "Toast already killed" for the app.
- REFERENCE (AOSP `android-10.0.0_r1`): the notification service tells the app's toast to show,
  and after its time, by default 2 seconds for a short toast, which accessibility settings can
  lengthen, tells it to hide and drops it from its queue
  (`NotificationManagerService.java:303,5981-5982,6003-6014,6048-6053,6057-6066`). The toast's
  show and hide steps run as messages on the looper of the thread that made it: the show step
  keeps the view and adds its window, and the hide step, if a view was kept, removes it and
  reports back, and the service logs "Toast already killed" for a toast no longer in its queue
  (`Toast.java:403-418,446-451,457-460,467-520,539-560`;
  `NotificationManagerService.java:2355-2371`).
- DERIVED: `Notifier.showToast` makes a short toast (`Notifier.java:95`), so the log line 2.0
  seconds after the result, timed by the emulator's clock alone, is the result toast being taken
  down after its show step ran, and the window list and the screenshot show it on screen before
  that. So on Android 10 the result toast shows with this change and did not before it, as the
  `Toast` REFERENCE under "Save states and Resume under hardcore" predicts. The test builds ran
  the benchmark with Glide64, because this emulator cannot run the parallel plugin, each build
  ran once, and nothing ran on a device with Android 6 to 10.
- OBSERVED (host build): `./gradlew :app:testDebugUnitTest --rerun :app:assembleDebug` passes
  with every unit test. In `javap -c -p` of the compiled classes, `finishBenchmark` passes the
  result and the driver's name as two format arguments, the method's lambda hands them on to
  `Notifier.showToast(Context, int, Object...)`, the cheats and resume notices pass an empty
  array, and no class of `CoreService` calls the `Notifier.showToast` that takes a string.

### Controller Pak saves of players 2 to 4
- OBSERVED: the core keeps the four Controller Paks in one 128 KB file
  (`main.c:164-167,1025-1028,1641`; `device.h:55`; `mempak.h:40`). Each player's pak is a 32 KB
  part of it, with a storage of its own whose save goes to the file's storage, and only the paks
  use that kind of storage (`main.c:1748-1754`; `file_storage.c:129-135,158-163`). The in-game
  menu has a pak choice for each of the four players (`GameActivity.java:997-1004`). A game
  writes a pak 32 bytes at a time (`game_controller.c:36,137`), and each write copies the bytes
  into that player's part and saves their range (`mempak.c:184-192`). The file's storage writes
  the whole file on the session's first save and only the given range after that
  (`file_storage.c:38,104-114`), and when the game closes, or a plugin fails to open, the core
  only frees it (`main.c:1928,1968`; `file_storage.c:76-80`).
- OBSERVED: before this change, a part's save passed the range at the pak's own address, without
  the part's place in the file (`file_storage.c:125-129` at `801d6b18d`). That code was the same
  where this branch starts (`d9779061b`).
- DERIVED: so after a session's first save to that file, a write to the pak of player 2, 3 or 4
  saved player 1's current bytes at the same address instead, which did player 1's pak no harm,
  and its own bytes never reached the file. The game still read them back until it closed; then
  they were lost. When a session's first save was a write to one of those paks, the whole file
  was written with it, but their later writes were not, so of those paks' writes only that first
  32-byte write reached the file. Not seen on a device.
- REFERENCE: upstream mupen64plus-core fixed this in `3c2955c910d5` (Rosalie Wanders, 2026-01-08,
  "Fix memory pak from player 2-4 not saving correctly"): the storage gets an `offset` field, a
  part's save adds it to the range, and the core sets it for each pak. The app's core was copied
  from mupen64plus-ae's fork at `3c4182f7473b` (`mupen64plus-core/upstream/.gitrepo`), which is
  still the head of that fork's `master` (checked 2026-10-23) and lacks the fix.
- OBSERVED: this change applies those three hunks unchanged (`file_storage.h:32`;
  `file_storage.c:129-135`; `main.c:1751`). Nothing fills that struct by position, which a new
  field would break, and the paks' storages, the only ones whose save reads the field, set it.
- OBSERVED (host build): `tools/test-mempak-save.sh` builds `tools/tests/mempak-save-test.c` with
  the core's `file_storage.c`, `mempak.c` and file helpers. The test sets up the four paks as
  `main_run` does, but sets each pak's offset itself, so `main.c:1751` is checked only by reading
  it and by the build. It writes 32-byte blocks to each pak after the session's first save,
  compares the file with the bytes it should hold, and reads the blocks back as the next session
  would. It passes with GCC 16.2.1 and clang 23.1.3, and in a scratch build under clang ASan,
  UBSan and LeakSanitizer with no reports. With `file_storage.c` and `file_storage.h` from before
  this change, and the test's line that sets the offset removed, as that struct had no such
  field, it fails with both compilers: the file's parts for players 2 to 4 differ from what they
  should hold, and their blocks read back as the old file's bytes. A copy of the fix whose save
  leaves out the offset, and a copy of the test that never sets it, fail the same way (GCC).
- OBSERVED (host build): `./gradlew :app:testDebugUnitTest --rerun :app:assembleDebug` passes with
  every unit test. The build after the code change recompiled `file_storage.o` and `main.o`, the
  only core files that include `file_storage.h`, for all four ABIs with empty ndk-build stderr
  logs, and each packaged core library has the build ID of the one that was linked. In the arm64
  object, a part's save loads the offset together with the file's storage and adds it to the
  range's start before it jumps to the file's save (NDK `llvm-objdump -d`).
- Not yet verified on device: two players saving to their Controller Paks in one session, and the
  saves still there after the game is started again.

### Save file formats
- OBSERVED: the core writes each save type to a file of its own (`main.c:164-182,1025-1088`):
  EEPROM, SRAM, FlashRAM, and the four Controller Paks together in one 128 KB file. It names them
  after the game's name in its ROM database, or for a game not in it, the name in the game's
  header followed by " (unknown rom)" (`rom.c:198,218-219`). The EEPROM file holds 512 bytes, or
  2 KB when the game's database entry, or for a game not in the database its homebrew header,
  gives a 16 Kbit EEPROM, as the session's first save rewrites the file at that size
  (`main.c:1084-1087`; `rom.c:235-244,323-324,553,625-627`;
  `mupen64plus-core/upstream/src/api/m64p_types.h:237-238`; `file_storage.c:104-114`;
  `util.c:70`). The core stores EEPROM and pak bytes in the order the game sees them
  (`eeprom.c:59,74`; `mempak.c:176,190`; `si_controller.c:64,69`), and SRAM and FlashRAM bytes
  with each 32-bit word's bytes reversed (`sram.c:54,71`; `flashram.c:84-88,211,231`), as
  `M64P_BIG_ENDIAN` is not defined (`preproc.h:78-92`): none of the core's Android build files
  defines it (`Android.mk`, `Application.mk`, `mupen64plus-core.mk` and `build.gradle` in
  `mupen64plus-core`, and the `build_common` files that `Android.mk` and `build.gradle` include,
  `native_common.mk` and `native_common.gradle`). Its SRAM is 32 KB, and it keeps only the low
  16 bits of an SRAM address (`sram.h:29`; `sram.c:31,51,68`), also for homebrew whose header
  asks for 768 Kbit (banked) or 1 Mbit of SRAM (`rom.c:326-329`). It reads a file shorter than
  its save type as far as the file goes and leaves the rest of its copy unset; it fills in a
  blank save only when it cannot open the file (`file_storage.c:42,50`; `util.c:58-61`;
  `main.c:1030,1054,1064,1079`).
- REFERENCE (Project64 `develop` at `24376e868350`): Project64 also stores EEPROM bytes in the
  game's order (`PifRamHandler.cpp:113,120,210,217,467`; `Eeprom.cpp:74,85,176,190`), SRAM bytes
  with each word's bytes reversed (`Sram.cpp:62-63,86,95-96`; `MemoryVirtualMem.cpp:642,821`),
  and each 128-byte FlashRAM page as it lies in RDRAM, so with each word's bytes reversed too, at
  the page's number times 128 (`CartridgeDomain2Address2Handler.cpp:104`;
  `FlashRam.cpp:87,140,168-177,196,206`). It opens those three files without truncating them and
  writes each block where the game writes it, so a file it creates ends after the furthest block
  the game has written (`Eeprom.cpp:151,194-195`; `Sram.cpp:31`; `FlashRam.cpp:128,158-159`),
  and it reads its EEPROM file over a copy that it first fills with 0xFF (`Eeprom.cpp:135,162`).
  It moves the bank bits of an SRAM address down, so that Dezaemon 3D's banks lie one after
  another in 32 KB steps in its file (`Sram.cpp:50-51,83-84`). It stores pak bytes in the game's
  order, but in one 32 KB file for each controller, which it writes whole when it creates it
  (`PifRamHandler.cpp:462,677,722`; `Mempak.cpp:20-22,47-59,63,215-216`), and it names its files
  after its own name for the game (`Eeprom.cpp:137`; `Sram.cpp:17`; `FlashRam.cpp:114`).
- REFERENCE (RetroArch's mupen64plus core, mupen64plus-libretro-nx `develop` at `15bada859065`):
  one block of save memory holds EEPROM, the four paks, SRAM and FlashRAM, in that order
  (`libretro/libretro_memory.h:10-16`; `libretro/libretro.c:2115,2127`;
  `mupen64plus-core/src/main/main.c:872-938` there). Its `eeprom.c` and `mempak.c` are the same as
  this core's, its `sram.c` and `flashram.c` differ only by bounds checks on the copies, and its
  `si_controller.c` copies the PIF RAM the same way (`:64,69` there).
- DERIVED: so for the same game data, Project64 writes the same EEPROM, FlashRAM and 32 KB SRAM
  bytes as this core, under its own file names, and the same Controller Pak bytes split into four
  files, while RetroArch's core keeps the same bytes in one block. Moving a save between them
  takes renaming, or splitting or joining files. A save file that Project64 has written only in
  part loads here with the rest of the core's copy unset; for EEPROM, Project64 itself reads that
  part as 0xFF. A game with more than 32 KB of SRAM, such as Dezaemon 3D, does not fit this
  core's SRAM, so the two emulators' files for it differ. 64DD disk saves, which the core also
  writes (`main.c:189-200`), were not compared. Not tried with either emulator.
- OBSERVED: a Game Boy save chosen for a Transfer Pak in the game's settings is copied whole into
  the session's working folder, in the app's cache, before the game starts, and the core is given
  that copy (`CoreService.java:703,725-738`; `CoreInterface.java:183-188,235-236,448-455`;
  `FileUtil.java:482-485`); with no save chosen, the core uses a file of its own in its save
  folder (`main.c:184-187,1416-1419`). The core opens the save at the RAM size that the Game Boy
  game's header gives and saves at each write to that RAM (`gb_cart.c:105,138,1024-1040`;
  `main.c:1404-1443`). After the game, the app writes the whole copy over the start of the chosen
  file, opened with mode "rw", and deletes the working folder (`CoreService.java:939,943`;
  `CoreInterface.java:522-530`; `FileUtil.java:676,691-700`).
- OBSERVED: before this change, the first write to that RAM rewrote the save at the RAM's size,
  without anything that followed the RAM (`file_storage.c:102-110` at `9fcd0913a`; `util.c:70`).
  This change applies upstream mupen64plus-core's `902bc60f5615` (2026-10-18): `init_gb_ram` marks
  the save, and the first write of a marked save writes the RAM over the start of the file and
  leaves the rest (`main.c:1432-1434`; `file_storage.c:104-114`; `util.c:91-94`). The two
  functions that open a save clear the mark (`file_storage.c:39,64`). The 64DD disk's save is not
  opened by them: `load_dd_disk` allocates it with `malloc`, fills it in by hand and gives it the
  file save that the disk's writes reach, all of them in the full-disk format that the app sets
  and only those to the disk's RAM area in the RAM-only format
  (`main.c:1177,1186,1256-1271,1281-1282`; `disk.c:49-53,60-64`; `NativeConfigFiles.java:118`).
  Upstream's patch leaves that save's mark unset (REFERENCE: so does upstream's `master`,
  `src/main/main.c:1270,1356,1363` at `3be6336a`), so this change also clears it there, in two
  lines of its own (`main.c:1262,1270`). Every save but the Game Boy one is therefore unmarked,
  and its first write still rewrites the file at its size. REFERENCE (upstream `902bc60f5615`):
  Game Boy saves now "keep what other emulators append after the cartridge RAM, like the MBC3
  RTC footer".
- OBSERVED (host build): a scratch program that fills a save as `load_dd_disk` does and writes to
  it once through `file_storage.c` gets a MemorySanitizer report of an uninitialized read of the
  mark (`file_storage.c:108`) without the cleared line, and none with it. DERIVED: so with
  upstream's patch alone, whether a 64DD save's first write cut the file at its size would have
  depended on what the allocator left in that field.
- REFERENCE (AOSP `android-13.0.0_r1`): mode "rw" opens a file for reading and writing without
  truncating it (`ParcelFileDescriptor.java:652-654`; `FileUtils.java:1341-1372,1396-1417`), and
  Android's local-storage document provider opens the file itself with that mode
  (`ExternalStorageProvider.java:68`; `FileSystemProvider.java:506-529`).
- OBSERVED: the app runs from Android 6 (`version_common.gradle:9`). On Android 8.1 and older, on
  Android TV before Android 11, and where no folder picker is available, it picks files with its
  own browser, which keeps a `file://` path instead of a document (`AppData.java:415-417`;
  `LegacyFilePicker.java:107`; `GamePrefsActivity.java:189,225-231,787,811-812`).
- DERIVED: so what another Game Boy emulator appends after the cartridge RAM, such as the clock
  of an MBC3 cartridge, now stays in the core's working copy, and the app writes it back with the
  RAM; a save that the core keeps in its own folder keeps it too. Before this change the working
  copy lost it once the game wrote to that RAM, and the chosen file kept it only where opening it
  with "rw" did not truncate it, as on Android 13 through the local-storage document provider (on
  the device's storage or an SD card); other Android versions, other document providers and the
  `file://` path of the app's own browser were not read. When the copy into the working folder
  succeeds, it has the chosen file's whole length and the app writes all of it back, so the result
  no longer depends on whether opening the chosen file truncates it. When that copy fails or is cut
  short, which the app does not notice (`FileUtil.java:493-501`; `CoreInterface.java:452`), the
  core gets a copy without the end of the chosen file, or its own file if no copy was made
  (`CoreInterface.java:184-188`; `main.c:1416-1419`), as before this change, and what the chosen
  file keeps still depends on how it is opened. Not tried.
- OBSERVED (host build): `tools/test-gb-ram-save.sh` builds `tools/tests/gb-ram-save-test.c` with
  `file_storage.c` and the core's file helpers. The test opens saves as `init_gb_ram` does, from a
  struct whose bytes start out nonzero, and writes to the RAM: a marked save with 48 bytes after
  its 32 KB of RAM must keep both writes and the same 48 bytes after them, an unmarked one must be
  written at its size, and a marked one with no file yet must be created at its RAM size by its
  first write. It passes with GCC 16.2.1 and clang 23.1.3, and under clang ASan, UBSan,
  LeakSanitizer and MemorySanitizer with no reports. With the `file_storage.c` and `.h` from
  before this change, which have no mark, and the test's marking line removed, only the first
  check fails, as it does with a copy of the fix whose first write ignores the mark; a copy whose
  opening does not clear the mark fails only the second, and one whose marked first write writes
  only the changed bytes fails only the third. The test marks the save itself, so
  `main.c:1432-1434` and the cleared mark in `load_dd_disk` are checked only by reading them and
  by the build.
- OBSERVED (host build): `./gradlew :app:testDebugUnitTest --rerun :app:assembleDebug` recompiled
  `file_storage.o` and `main.o`, the only core files that include `file_storage.h`, for all four
  ABIs with empty ndk-build stderr logs; each packaged core library has the build ID of the one
  that was linked, and the 31 unit tests pass. In the arm64 `main.o`, where `load_dd_disk` is
  inlined, both of its cases set `first_access` and the mark with one 8-byte store of 1 and then
  0 (NDK `llvm-objdump`).
- Not yet verified on device: moving each save type between the app and Project64 or RetroArch,
  and a Game Boy save with a clock after its RAM used through the Transfer Pak.

### Compliance checklist
Each requirement of RetroAchievements' compliance page (revision `361119b677dc`) and its state on
this branch. MET means the code and the host checks meet it, as DERIVED from the evidence cited
here or in the section named; that section also lists what is still to be checked on a device.
OPEN means it was not checked. USER means it needs the maintainer, outside the code. N/A means it
does not apply to this app.

A. RetroAchievements features
- MET (UI visibility): the in-game menu opens the achievement list (see "In-game achievement
  list").
- MET (achievements): achievements read the emulator's memory and unlock (see "Memory reads").
  Trigger and Measured conditions show as the challenge and progress indicators during play and
  in each row's status in the list (see "Challenge and progress indicators").
- MET (rich presence and leaderboards): leaderboard attempts start, fail and submit, with notices
  and the attempt's value on screen (see "Leaderboard attempts"). OBSERVED: rcheevos builds the
  rich presence text for each ping (`rc_client.c:5745-5753`), and the app has no setting for
  either (`preferences_ra.xml:7-37`). Rich presence was not checked on the server.
- MET (offline queueing): see "Unlocks kept until the server answers".
- MET (save state hit storage, recommended): see "Achievement progress kept with save states".
- N/A (toolkit support): the page asks for RAIntegration only in Windows builds, and this app
  ships only for Android.
- MET (save file compatibility, recommended), read from the code and not tried: for the same game
  data the core writes the same EEPROM, FlashRAM and 32 KB SRAM bytes as Project64, under other
  file names, and the same Controller Pak bytes, in one file where Project64 keeps one per
  controller (see "Save file formats", which also gives the limits and covers Game Boy saves used
  through the Transfer Pak).

B. Hardcore rules
- MET (cheats a player chooses): OBSERVED: under hardcore the emulation job adds none of the
  game's enabled cheats, and the GameShark button is refused (`CoreService.java:592-598,886-899`);
  `:896`, in that loop, is the app's only call of `coreAddCheat`.
- USER (the core's own database patches): OBSERVED: in every mode the core also adds the codes its
  ROM database lists under the game's `Cheat` keys (`rom.c:688-716`; `main.c:1587`;
  `cheat.c:497-536`). It writes boot codes at the first frame, and again after each soft reset,
  which sets the frame count back to zero (`interrupt.c:449`), and the others on every frame after
  the first 60; a code that follows a test is written only on frames when the test holds
  (`main.c:936-950,975`; `cheat.c:229-236,242-267,281-282`). In the shipped database
  (`app/src/main/assets/mupen64plus_data/mupen64plus.ini`) 14 entries list codes: seven GameCube
  dumps of Ocarina of Time ("End Credits Fix": tests, each followed by a write), five of
  Mario Kart 64 ("Multiplayer Timing Fix": writes) and two of Rat Attack ("Allows the game to
  boot": boot codes). 37 more entries refer to one of these with `RefMD5` and have no codes of
  their own, so they take its codes (`rom.c:350,406-411`): other dumps, translations, trainers and
  hacks of the same games, and three debug builds of Ocarina of Time Master Quest.
  That makes 51 entries: 15 of Ocarina of Time, 32 of Mario Kart 64 and 4 of Rat Attack. A dump
  the database does not list by MD5 gets the codes of the entry with its header's CRCs, if no
  other entry has the same CRCs (`rom.c:195-196,214,847-856`). REFERENCE (upstream mupen64plus-core
  pull requests 203, 333 and 714, merged as `337a0842`, `cc1e7f02` and `f93c9e77`): upstream
  added the Mario Kart 64 and Rat Attack codes, with these comments, to fix multiplayer timing and
  to let Rat Attack boot. HYPOTHESIS: the codes correct emulation or let a dump boot rather than
  change play, so they are not section G's "gameplay-altering cheats", and turning them off would
  bring the problems back; the game code they patch was not examined. The page also disables
  "Built-in cheat engines", and these codes run through the core's, so the approval request should
  name them for RetroAchievements to decide.
- N/A (rewind): OBSERVED: no file in the app's or the core's sources mentions rewind.
- MET (slowdown and frame advance): every speed other than 100% and frame advance are refused
  (see "Speed lock under hardcore").
- MET (loading save states): refused under hardcore, while creating save states is allowed (see
  "Save states and Resume under hardcore").
- MET (rich presence and leaderboards cannot be disabled): OBSERVED: the app has no setting to
  disable them (`preferences_ra.xml:7-37`).
- MET (resume): a game resumed from its auto save runs in softcore, with a notice (see "Save
  states and Resume under hardcore").
- MET (mode switching): OBSERVED: a session turns hardcore on only where it starts achievements,
  before signing in (`CoreService.java:1366`), once per service (`:1329-1332`) and from the
  settings read when the service was created (`:1101,1118`). Otherwise it only turns hardcore
  off, for Resume and netplay (`:416,1366`; see "Hardcore session start"). DERIVED: a changed
  setting applies to the next game.
- MET (no memory editors, debuggers, scripting or input playback): OBSERVED: the core's debugger
  is built only with `DBG` (`main.c:83,898,1886,1910,2005`), which none of the core's Android
  build files define (`Android.mk`, `Application.mk`, `mupen64plus-core.mk` and `build.gradle` in
  `mupen64plus-core`), and the app's Java sources mention no memory editor, Lua scripting or movie
  playback. The core exports `DebugMemGetPointer`, which returns a pointer to the console's RAM,
  without that guard (`debugger.c:227-247`; `api_export.ver:51`), but no code in this repository
  calls it. The core's own key and joystick shortcuts save and load states, change speed, advance
  frames and press the GameShark button (`eventloop.c:386-417,637-689`), but the core is built
  against an SDL stub whose event functions are empty (`mupen64plus-core.mk:25`;
  `ndkLibs/SDL2_stub/SDL_events.h:1001-1004,1120-1124`), and the app sends the core no key:
  `M64CMD_SEND_SDL_KEYDOWN` and `M64CMD_SEND_SDL_KEYUP` only appear in its list of commands
  (`CoreTypes.java:96-97`). OBSERVED (host build): in the debug APK `3.0.338 11f5f3b9`, built from
  `11f5f3b96` on 2026-10-21, the core library of every ABI imports only SDL_net's functions, none
  of SDL's own (`llvm-nm -D`), and the APK ships `libSDL2_net.so` but no `libSDL2.so`
  (`unzip -l`). DERIVED: so the core's event functions are the stub's empty ones, and those
  shortcuts never fire.

C. Identity and integrity of the client
- MET (user agent): see "User-Agent". USER: the approval request must disclose the two earlier
  User-Agents recorded there.

D. Eligibility timeline
- MET: REFERENCE: section D also counts "the parent emulator it is forked from". OBSERVED: the
  app is a fork of Mupen64Plus-AE (`README.md:8`), whose history in this repository starts with an
  import on 2012-05-05 (`8772620d5`) and carries the release tags `1.9` and `1.9.2`, on commits of
  2012-05-06 and 2012-05-07 that are ancestors of this branch. USER: the approval request should
  name the parent and its first public release.

E. Defaults and UX
- MET: OBSERVED: softcore is the default (`preferences_ra.xml:31-36`), and hardcore is one
  checkbox on the same RetroAchievements screen as the sign-in, whose summary says what it
  disables (`preferences_ra.xml:7-37`; `strings.xml:326-327`). Each session shows its mode once
  the game's achievements load (see "On-screen notices").

F. Transparency and legality
- N/A (monetization): OBSERVED: neither the app's sources nor its Gradle files use a billing or
  ads library or ask for the billing permission.
- USER (licenses and notices): OBSERVED: the README names the app's license and libadrenotools'
  (`README.md:151-153`), and the app ships the core's license list in its assets
  (`app/src/main/assets/mupen64plus_data/doc/LICENSES`), but no page lists every shipped core and
  plugin with its license and upstream link; the in-app Credits entry opens a contributor roll
  (`GalleryActivity.java:935-937`).
- USER (privacy policy): none is published with the app. OBSERVED: besides RetroAchievements, the
  app uses Google sign-in and the Google Drive API for its backups of game data
  (`app/build.gradle:214-215,220`). REFERENCE: section G rejects a policy with contradictions,
  such as "no third parties" while using social logins, so the policy has to name them.

G. Auto-fail criteria: each is covered under B, C or F above.

H. Banned emulators
- N/A: the page bans only Afterplay.

Before applying, the maintainer must publish a privacy policy (retention periods, server locations,
GDPR, and the Google services named under F), publish a page that lists every shipped core and
plugin with its license and upstream link, run the device checks the sections above list, and send
the approval request to RetroAchievements; the page names no contact for it. The request must
disclose `Mupen64PlusAE` and `Mupen64PlusAE/3.0 (Android)` (see "User-Agent") and should name the
parent and its first public release (see D) and the core's database patches (see B).

### Open items (not fixed here)
- REFERENCE (AOSP `android-14.0.0_r1`, `NotificationManagerService.java`): Android refuses a new
  toast from an app that already has five queued, counting every toast of the app
  (`:384,3365-3377`), and shows queued toasts one at a time, a long one for 3.5 seconds
  (`:399,8648-8653,8763`). OBSERVED: the RetroAchievements notices share that limit with the app's
  other toasts, including the hardcore "disabled" toasts, because `Notifier.showToast` makes a new
  toast on each call (`Notifier.java:95`; `CoreService.java:429,547,562,595`, and `:417,889`
  through `:405`). DERIVED: toasts beyond five are lost, so several unlocks on the same frame, or
  an unlock that arrives while repeated taps on a blocked control fill the queue, go unannounced.
  A "disabled" toast that follows a notice waits for it, up to 3.5 seconds for each notice ahead
  of it. The achievement list still shows every unlock.
- DERIVED: `rc_client_destroy` frees none of the work rcheevos has scheduled
  (`rc_client.c:191-227`), and unloading the game takes only its ping off that list
  (`:3185-3198`). OBSERVED (host build): in the session test, LeakSanitizer reports 88 bytes for
  each unlock or leaderboard entry still waiting for a retry when the client is destroyed
  (`:4688,4841,5390,5482`). In a copy of the test whose runs for unsent unlocks end without the
  idle call that finishes the game's load, it also reports each load still queued for the
  emulator thread (`:1936-1950`), about 9 KB with the test's game. DERIVED: the app leaves such
  work behind only when a session ends after the game's load has been answered but before the
  core's first frame, or while an unlock or entry waits for a retry. Not fixed here: rcheevos is
  vendored.
- DERIVED: rcheevos has data races of its own that the glue's locks do not reach. When Java's two
  HTTP threads (`RetroAchievementsManager.java:127`) hand rcheevos two unlock answers at once,
  both write the player's score outside its mutex (`rc_client.c:4709-4710`). Its idle work reads
  the list of scheduled work and the disconnect state outside the mutex (`:6507,6534`), while an
  answer on an HTTP thread changes both under it (`:536-562,6538-6557`). The effect would be a
  score taken from either answer, or a retry or a connection notice handled at the next frame or
  idle call. An independent reviewer's scratch stress test reported them under ThreadSanitizer;
  they were not repeated here. They are older than this branch: the two HTTP threads came with
  `4df6051aa`. Not fixed here: rcheevos is vendored.
