# RetroAchievements — remaining issues

Status: merged to `master` via PR #10 (`af7dd84f0`).
Confirmed working on device: Connect sign-in ("Signed in as pwnedbygary", session token
persisted), hardcore gates for save/load state, slots, GameShark and cheats (deny toast),
and denied actions no longer announce first.

This file lists what is still open. Evidence labels: OBSERVED = read from current source, or
seen in a named run such as "OBSERVED (host JVM)"; DERIVED = inferred from code paths;
REFERENCE = read from RetroAchievements server source or documentation, or from AOSP source;
HYPOTHESIS = plausible but untested.
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
- HTTP User-Agent version is hardcoded (`RetroAchievementsManager.java:56`).

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
  (`RetroAchievementsManager.java:56`). `CoreService.onRaEvent` logs each event's JSON; in
  `ff249b18a` it logged only the event type.
- DERIVED: hardcore unlocks are recorded as softcore until RetroAchievements approves this client.
  The server's warning achievement is shown on screen when it unlocks (see "On-screen notices")
  and in the in-game achievement list. The server's separate `Warning` text (RAWeb
  `ac7bf9b84d98`, `GetAchievementSetsAction.php:557`) is never shown, because rcheevos does not read
  that field. Not yet verified on device: that award requests reach the server and come back as
  softcore.

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
  test: with hardcore on, the message shows and the achievement list says "Softcore".

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
  `:698-708`), and `ra_glue_async_cb` passes it to Java (`ra_glue.c:162-173`). A failed HTTP
  request becomes `RC_NO_RESPONSE` (`RetroAchievementsManager.java:345-350`,
  `rc_api_common.c:360-365`). Only -34 and -35 are retried with the password.
- OBSERVED: `loadGame` is requested only after a login succeeds. rc_client results carry no
  request ID (`ra_glue_async_cb`, `ra_glue.c:162-173`), so the session makes one request at a
  time and `mRaLoginCredential` tells which login a result belongs to. A request call that returns
  false ends the session at once, as before. It returns false when `ra_glue`'s own checks refuse
  without a callback (`ra_glue.c:398-399,408-409,427-428`), when rc_client refuses, which it also
  reports through the callback (login `rc_client.c:766-775,800-813,833-846`; load
  `:2444-2461,2927-2948`), and when the request finishes before the call returns (login
  `:786-792`, load `:2900`). `onRaAsyncResult` ignores any result that arrives after that.
- DERIVED: in that last case a successful login is dropped, and the session runs without
  achievements and with enforcement released. It needs the main thread to stall for a whole HTTP
  round trip (responses arrive on the HTTP thread, `RetroAchievementsManager.java:352`), so it
  should be rare. In `fd6b452f1` the same case latched the failure but still loaded the game, so
  achievements would run with enforcement released.
- DERIVED: the password login starts from the token login's result, which is posted to the main
  thread after rc_client has cleared its login state (`rc_client.c:700-708`), so rc_client does
  not refuse it as "Login already in progress" (`:757-760`).
- OBSERVED: the token from a successful login is written with `commit()`, and only when it differs
  from the saved one; `saveRaProgress` already used `commit()`. `forceExit` kills the emulation
  process (`CoreService.java:1526-1529`).
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
  they were requested for this branch, not taken from the page. Since this revision the page calls
  the other mode "Casual"; the app still says "softcore".
- OBSERVED: `CoreService` shows a long toast (`showRaNotice`) in each of these cases:
  - when the game's achievements finish loading: "RetroAchievements: hardcore mode" or
    "RetroAchievements: softcore mode", from `isRaHardcore()`;
  - when an achievement unlocks: "Achievement unlocked: <title> (<n> points)", or "(1 point)";
  - when the server's client warning unlocks (IDs from 101000001,
    `RetroAchievementsManager.WARNING_ACHIEVEMENT_ID`, now shared with `AchievementsDialog`): its
    title and description as one paragraph ("<title>. <description>"), so the text can wrap over
    both of the toast's lines (see the line limit below);
  - when the session ends with a terminal failure (`failRaSession`): "RetroAchievements is off for
    this session: <reason>", where the reason is rcheevos' error text (for example "Unknown
    game"), "could not start the request" or "no sign-in is saved in Settings".
- DERIVED: the mode toast shows what the app enforces. rcheevos changes its own hardcore setting
  only when the media changes (`rc_client.c:3221-3227`), and `ra_glue` never changes media, so
  `isRaHardcore()` matches rc_client's mode when the load completes.
- DERIVED: rcheevos sends the award request and then raises the unlock event for every triggered
  achievement (`rc_client.c:6185-6210`). For the server's warning the award returns early and
  submits nothing (`:4818-4821`), but the event is still raised. So the warning toast shows when
  the warning unlocks, which the server says happens a few seconds after loading (RAWeb
  `ac7bf9b84d98`, `GetAchievementSetsAction.php:493-499`).
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
  two lines, and the failure toast for a ROM RetroAchievements does not know and for a wrong
  password with no valid saved token.

### In-game achievement list
- OBSERVED: with RetroAchievements enabled in Settings, the in-game menu has an "Achievements" item
  (`game_drawer.xml`; `GameActivity` removes it otherwise). It opens `AchievementsDialog`, a
  read-only list titled with the game name: a summary row ("Hardcore: 5 of 20 unlocked" and the
  points), the server's warning achievement if there is one, then one row per achievement with its
  title, description, points and whether it is unlocked. The only button is OK; nothing in the
  dialog unlocks, submits or edits. If no game is loaded, the dialog shows one line instead.
- OBSERVED: `ra_glue_get_achievements_json` used to mark an achievement unlocked when either unlock
  bit was set, so under hardcore a softcore-only unlock showed as unlocked although the summary
  did not count it. It now tests the bit for the current mode, the same bit
  `rc_client_get_user_game_summary` counts (`rc_client.c:946-964`). The achievement `state` is not
  used: a DISABLED achievement keeps that state even when unlocked (`rc_client.c:1458-1485`).
- OBSERVED: the summary JSON has a new `"hardcore"` field from `rc_client_get_hardcore_enabled`.
  `ra_glue_get_hardcore()` returns the requested flag, which `ra_glue.c:41` calls a best-effort
  diagnostic.
- REFERENCE (RAWeb `ac7bf9b84d98`): `GetAchievementSetsAction.php:493-552` puts the client warning
  achievement first in the first set, and its comment says it unlocks itself a few seconds after
  loading. For this client its description is "Hardcore unlocks cannot be earned using this
  emulator." (`:540`). rcheevos leaves IDs from `101000001` up out of the summary
  (`rc_client.c:954`), so the dialog shows them as warning rows, not achievements, and leaves them
  out of its totals.
- OBSERVED: `rc_client_get_user_game_summary` counts only the game's main achievement set
  (`rc_client.c:1031`), but `rc_client_create_achievement_list` lists every active set
  (`rc_client.c:4397-4428`). So the dialog counts its totals from its own rows and reads only the
  mode and the game title from the summary JSON. For a game that also loads a bonus set, the
  totals still match the rows.
- OBSERVED (host build): `./gradlew :app:testDebugUnitTest --rerun :app:assembleDebug` passes.
  `ra_glue.c` was recompiled for all four ABIs with no compiler warnings (the ndk-build stderr logs
  are empty). The packaged `libmupen64plus-core.so` (all four ABIs) has the new summary format and
  still exports both JSON functions. There is no JVM test for the dialog: local unit tests get
  Android's stubbed `org.json`.
- Not yet verified on device: the item shows only with RetroAchievements enabled; the dialog
  matches the loaded game in hardcore and in softcore; the warning row shows for this client.

### Open items (not fixed here)
- OBSERVED: challenge and progress indicators are not shown, although section A of the compliance
  page requires them (see On-screen notices). rcheevos raises challenge-indicator and
  progress-indicator events (`rc_client.h:814-818`), but `ra_build_event_json` has no case for them
  and drops them (`ra_glue.c:300`), and the achievements JSON carries no measured progress
  (`rc_client.h:560-561`). REFERENCE (RetroAchievements docs): a Trigger flag exists "so a challenge
  indicator can be shown on the screen" (`developer-docs/flags/trigger.html`, revision `9a5f33c`),
  and a Measured flag "adds a progress bar to the achievement overlay"
  (`developer-docs/flags/measured.html`, revision `6226ebf`).
- OBSERVED: the achievement list has display limits. rcheevos labels each set's buckets ("<set
  title> - Locked", `rc_client.c:4176-4201`), but `ra_glue_get_achievements_json` passes on no
  labels, so achievements from a bonus set have no heading. Nothing marks an achievement as
  unsupported (rcheevos bucket "Unsupported", `rc_client.c:4166`): it shows as locked, or as
  unlocked if the player already earned it in this mode (`rc_client.c:1686-1689`). Long titles are
  cut to one line (`list_item_two_text_icon.xml:36-38`). The "No achievements are loaded" message
  also appears when the list fails to parse or does not fit the 128 KB text buffer
  (`RetroAchievementsManager.java:60,400-402`, `ra_glue.c:619`).
- DERIVED (predates this branch): `RetroAchievementsManager.onServerCall` runs each request with
  `execute()` (`RetroAchievementsManager.java:315`) and catches only `IOException` (`:345`). Any
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
- OBSERVED: server errors are only logged. When the server answers an unlock with an error that is
  not retried, rcheevos raises a server-error event (`rc_client.c:4669-4674`; leaderboard
  submissions likewise, `:5375`), which `ra_build_event_json` passes on (`ra_glue.c:265-270`) but
  `CoreService.onRaEvent` only logs. DERIVED: the player has already seen "Achievement unlocked"
  for that achievement, because the unlock event is raised right after the award request is sent
  (`rc_client.c:6185-6210`).
- OBSERVED: two early exits in `startRetroAchievements` show no notice: an empty ROM MD5
  (`CoreService.java:1330`) and a failed `initialize()` (`:1335-1338`). DERIVED: both return before
  `mRaGameMd5` is set, so `isRaHardcore()` is false and hardcore is not enforced, and no mode notice
  appears either.
- Not audited on this branch: the rest of the compliance page, for example offline queueing of
  unlocks (section A) and the resume and save-state rules (section B).
