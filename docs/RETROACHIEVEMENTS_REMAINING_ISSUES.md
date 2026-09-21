# RetroAchievements — remaining issues

Status: branch `feat/retroachievements` (pushed).
Confirmed working on device: Connect sign-in ("Signed in as pwnedbygary", session token
persisted), hardcore gates for save/load state, slots, GameShark and cheats (deny toast),
and denied actions no longer announce first.

This file lists what is still open. Evidence labels: OBSERVED = read from current source;
DERIVED = inferred from code paths.

## Fixed on this branch
- Cancel on confirmation dialogs no longer performs the action: `CoreFragment`
  (`SAVE_STATE_FILE_CONFIRM_DIALOG_ID`) and `GamePrefsActivity` (`DOWNLOAD_CONFIRM_DIALOG_ID`,
  `UPLOAD_CONFIRM_DIALOG_ID`) now require `which == DialogInterface.BUTTON_POSITIVE`. The Drive
  download/upload confirmations previously ran on Cancel too.
- Hardcore denials no longer announce "Saving/Loading …" before being denied (`cb504d624`).

## Functional bugs

### 1. Overwrite confirmation appears before the hardcore denial — MEDIUM (UX)
- OBSERVED: `CoreFragment.saveState()` shows the confirm dialog whenever the target file
  exists (`CoreFragment.java:722-746`, check at 736) with no hardcore knowledge; the hardcore
  deny happens later in `CoreService.saveState()` (`CoreService.java:406-410`).
- Impact: with hardcore active and an existing file, the player is asked to confirm an
  overwrite that is then denied.
- Direction: give the fragment a way to ask whether hardcore would deny before prompting (e.g.,
  a package-private query on the service), or move the overwrite decision service-side. Must use
  the live `isRaHardcore()` predicate (intent + terminal-failure latch), not a cached flag, to
  stay correct during session bootstrap.
- Pre-existing.

### 2. "Fast-forward" is promised by the hardcore summary but not gated — MEDIUM
- OBSERVED: `strings.xml:259` — "Disables save states, fast-forward and cheats while
  achievements are active". `CoreFragment.fastForward()` calls `setCustomSpeed()`
  (`CoreFragment.java:580-587`; also speed menu at 940-959) with no hardcore check.
  `isRaHardcore()` is only consulted in `CoreService` (auto-save 294, blockIfRaHardcore 399,
  cheats 815, auto-load 834).
- Impact: hardcore players can fast-forward; the settings summary over-promises.
- Direction: gate fast-forward under hardcore, or soften the summary.
- Pre-existing.

### 3. TOCTOU wording mismatch on the overwrite announcement — LOW (cosmetic)
- OBSERVED: the fragment decides to prompt from its own `exists()` check
  (`CoreFragment.java:736`); the service re-checks `exists()` to choose the announcement
  wording (`CoreService.java:413-415`).
- Impact: if the file appears/disappears between the checks, the wording may not match the
  dialog. Save behaviour is unchanged.
- Direction: pass the overwrite decision into the service instead of re-checking.

## Cleanups

### 4. `getLoadGameState()` / `LOAD_STATE_*` have no callers outside the manager — LOW
- OBSERVED: no references outside `RetroAchievementsManager.java` (grep across `app/src/main`).
  `LOAD_STATE_ABORTED` is documented as never returned (rc_client detaches the load state in
  the same operation that records the abort).
- Direction: remove, or add the intended consumer (e.g., a UI status indicator).

### 5. `LOAD_STATE_*` javadoc parenthetical is incomplete — INFO
- OBSERVED: an unknown-game abort assigns `client->game` before `rc_client_load_error`
  (`mupen64plus-core/rcheevos/src/rc_client.c:2618-2625`), so `getLoadGameState()` reports
  `DONE`, not `NONE`, after that abort.
- Direction: correct the parenthetical in the javadoc.

### 6. `LoginPreference.onDetached()` constructs the RA client even if never used — LOW
- OBSERVED: `onDetached()` calls `RetroAchievementsManager.getInstance().removeListener(this)`
  unconditionally (`LoginPreference.java:139-146`), which creates the singleton and loads the
  native library on settings-screen exit even when Connect was never tapped.
- Direction: static accessor with a null check before removing.

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
