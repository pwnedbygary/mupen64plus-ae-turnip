# RetroAchievements — remaining issues

Status: branch `feat/retroachievements` (pushed).
Confirmed working on device: Connect sign-in ("Signed in as pwnedbygary", session token
persisted), hardcore gates for save/load state, slots, GameShark and cheats (deny toast),
and denied actions no longer announce first.

This file lists what is still open. Evidence labels: OBSERVED = read from current source;
DERIVED = inferred from code paths. No open code issues remain on this branch; what follows are
deferred token-handling items and device-verification gaps.

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
- Allowed-path announcements after moving them into `CoreService`: confirm "Saving/Loading
  slot #" and file save/load announcements still appear exactly once when the action proceeds.

## Device data note
- The old `retroAchievementsWebApiKey` value remains in the device's SharedPreferences
  (inert — no reader in the tree). Removing it would require clearing app data (loses saves),
  so leave it unless a safe one-key cleanup is added.
