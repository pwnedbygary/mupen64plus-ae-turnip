# RetroAchievements — remaining issues

Status: branch `feat/retroachievements`, tip `cb504d624` (announcement-order fix; pushed).
Confirmed working on device: Connect sign-in ("Signed in as pwnedbygary", session token
persisted), hardcore gates for save/load state, slots, GameShark and cheats (deny toast),
and denied actions no longer announce first.

This file lists what is still open. None of these items were fixed in `81ac3fb74` or
`cb504d624`. Evidence labels: OBSERVED = read from current source; DERIVED = inferred from
code paths.

## Functional bugs

### 1. Cancel on the overwrite-confirmation dialog still overwrites the file — HIGH
- OBSERVED: `ConfirmationDialog.java:70-71` attaches the same `internalListener` to both the
  Cancel (negative) and OK (positive) buttons, and the listener forwards `which` unchanged
  (`ConfirmationDialog.java:55-64`). `CoreFragment.onPromptDialogClosed` for
  `SAVE_STATE_FILE_CONFIRM_DIALOG_ID` ignores `which` and calls `mCoreService.saveState(...)`
  (`CoreFragment.java:1001-1012`, save call at 1008). `onCancel` also routes to the same
  handler with `BUTTON_NEGATIVE` (`ConfirmationDialog.java:78-89`).
- Impact: saving a state to an existing name and then tapping Cancel overwrites the file.
- Direction: only save when `which == DialogInterface.BUTTON_POSITIVE`; decide separately what
  the cancel branch should do about `onSaveLoad()`.
- Pre-existing, unrelated to the RA work.

### 2. Overwrite confirmation appears before the hardcore denial — MEDIUM (UX)
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

### 3. "Fast-forward" is promised by the hardcore summary but not gated — MEDIUM
- OBSERVED: `strings.xml:259` — "Disables save states, fast-forward and cheats while
  achievements are active". `CoreFragment.fastForward()` calls `setCustomSpeed()`
  (`CoreFragment.java:580-587`; also speed menu at 940-959) with no hardcore check.
  `isRaHardcore()` is only consulted in `CoreService` (auto-save 294, blockIfRaHardcore 399,
  cheats 815, auto-load 834).
- Impact: hardcore players can fast-forward; the settings summary over-promises.
- Direction: gate fast-forward under hardcore, or soften the summary.
- Pre-existing.

### 4. TOCTOU wording mismatch on the overwrite announcement — LOW (cosmetic)
- OBSERVED: the fragment decides to prompt from its own `exists()` check
  (`CoreFragment.java:736`); the service re-checks `exists()` to choose the announcement
  wording (`CoreService.java:413-415`).
- Impact: if the file appears/disappears between the checks, the wording may not match the
  dialog. Save behaviour is unchanged.
- Direction: pass the overwrite decision into the service instead of re-checking.

## Cleanups

### 5. `getLoadGameState()` / `LOAD_STATE_*` have no callers outside the manager — LOW
- OBSERVED: no references outside `RetroAchievementsManager.java` (grep across `app/src/main`).
  `LOAD_STATE_ABORTED` is documented as never returned (rc_client detaches the load state in
  the same operation that records the abort).
- Direction: remove, or add the intended consumer (e.g., a UI status indicator).

### 6. `LOAD_STATE_*` javadoc parenthetical is incomplete — INFO
- OBSERVED: an unknown-game abort assigns `client->game` before `rc_client_load_error`
  (`mupen64plus-core/rcheevos/src/rc_client.c:2618-2625`), so `getLoadGameState()` reports
  `DONE`, not `NONE`, after that abort.
- Direction: correct the parenthetical in the javadoc.

### 7. `LoginPreference.onDetached()` constructs the RA client even if never used — LOW
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
