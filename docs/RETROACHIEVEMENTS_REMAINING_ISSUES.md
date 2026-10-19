# RetroAchievements — remaining issues

Status: merged to `master` via PR #10 (`af7dd84f0`).
Confirmed working on device: Connect sign-in ("Signed in as pwnedbygary", session token
persisted), hardcore gates for save/load state, slots, GameShark and cheats (deny toast),
and denied actions no longer announce first.

This file lists what is still open. Evidence labels: OBSERVED = read from current source, or
seen in a named run such as "OBSERVED (host JVM)"; DERIVED = inferred from code paths;
REFERENCE = read from RetroAchievements server source; HYPOTHESIS = plausible but untested.
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
Reference: that commit's message lists these as nonblocking:
- Password outranks the stored token; offer to clear the password after a successful token capture.
- Token write in `CoreService.onRaAsyncResult` uses `apply()` (async) rather than `commit()`.
- HTTP User-Agent version is hardcoded (`RetroAchievementsManager.java:56`).
- No token fallback when the password login fails (`loginWithToken` is only tried when the
  password is empty).

## Verification gaps (not bugs; close with device tests)
- Terminal-failure release policy: with hardcore active and a broken login (bad password or
  offline), confirm saves are released once the async failure arrives instead of being blocked
  forever. Code-verified only.
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
  (`RetroAchievementsManager.java:56`), and `CoreService.onRaEvent` logs only the event type.
- DERIVED: the server's warning payload is never shown to the player, and hardcore unlocks are
  recorded as softcore until RetroAchievements approves this client. Not yet verified on device:
  that award requests reach the server and come back as softcore.

### Speed lock under hardcore
- OBSERVED: `RaHardcoreRules.isSpeedAllowed` allows only 100%, so slow motion is refused as well as
  fast-forward (the old rule refused only speeds above 100%). `CoreService.blockIfRaHardcoreSpeed`
  replaces `blockIfRaHardcoreFastForward`. A request for 100% is never refused, so a session that
  is off normal speed can always return to it.
- OBSERVED: under hardcore, frame advance is refused, and the frame limiter toggle ("Sync audio" /
  "Desync audio") may only switch back to the setting the session started with
  (`RaHardcoreRules.isFramelimiterToggleAllowed`).
- DERIVED: with the audio volume at 0 the dummy audio plugin is used (`AppData.java:175-177`) and
  the limiter starts on (`GlobalPrefs.java:724-727`). The dummy plugin does no pacing
  (`dummy_audio.c:54-57`), and the real plugin paces only while the limiter is off
  (`plugin.cpp:348,378-379`), so turning the limiter off at volume 0 would run the game uncapped.
- OBSERVED: toast `ra_hardcoreFastForwardBlocked` is now `ra_hardcoreSpeedBlocked` ("Hardcore mode:
  speed controls are disabled"). The older entries above keep the old name because they record
  what was tested at the time.
- OBSERVED (host JVM): `RaHardcoreRulesTest` failed against the old speed rule (99% allowed) and
  against a limiter rule that refused every switch; it passes now.
- Not yet verified on device: hardcore refuses fast speed, slow speed, frame advance and switching
  the limiter away from its starting setting; softcore still allows all of them.

### Open items (not fixed here)
- DERIVED: hardcore can re-arm on a core that is already running. `onStartCommand`
  (`CoreService.java:1200`) calls `startRetroAchievements()` (`:1234`) every time, and that clears
  `mRaSessionFailed` (`:1322`). A new `GameActivity` (notification intent `:1134-1151`, resume
  broadcast `:227-230`) starts the service again (`CoreFragment.java:395-398,452`,
  `ActivityHelper.java:485`), but only the first emulation job runs (`CoreService.java:1466`).
  After a terminal login failure has released enforcement, a speed or limiter change made while
  released survives a re-login that re-arms hardcore, and the new activity shows "Speed 100 %".
  A refused speed request snaps the core back to 100% and the limiter can be switched back, but
  nothing does this automatically. The same path also re-runs login and load-game on a live
  session and can re-arm hardcore with cheats applied while released, so it needs its own fix.
  Device test: turn on "Don't keep activities", start a hardcore session offline so login fails,
  fast-forward, press Home, reconnect, tap the notification, then check whether save states are
  denied while the game is still fast.
- DERIVED: netplay catch-up turns the core speed limiter off and back on (`netplay.c:353,357`)
  with no hardcore check, and `startRetroAchievements()` does not skip netplay sessions.
- HYPOTHESIS (untested, predates this change): with audio sync on, the real audio plugin paces the
  game only when the game sends audio (`AiLenChanged`, `plugin.cpp:330-379`), so a game that goes
  quiet could run unpaced.
