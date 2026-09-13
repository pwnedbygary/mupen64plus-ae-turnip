# P06 checkpoint — candidate built; install blocked on signing identity (2026-09-13)

Published from [HANDOFF_NEW.md](HANDOFF_NEW.md) (P06 section);
related record: commit `a6d89018a` on `dd-eos-watchdog-checkpoint`.

## Review record (backfilled)

- Review loop: first review NEEDS CHANGES (stale 2026-08-24 symbol preservation asserted as verified) -> re-preserved with Build-ID verification -> two minor record fixes -> final PASS; verdicts recorded in commit `a6d89018a`'s
  message (the protocol-designated location for the full review
  narrative and the reviewed-snapshot hashes).
- The Checkpoint body below is extracted verbatim from the handoff
  block with only the two pending fields replaced.
- Snapshot table below is recomputed from the published commit
  itself (`git show a6d89018a:<path>`), so it cannot drift from what
  was reviewed and committed.

| File (at this commit) | Status | SHA-256 (git show recomputed) |
|---|---|---|
| `.gitignore` | M | `a36e98cc3ebe2de25f2ed830ec0e2b2ff1972fbe0735b5628c15f19f3258bf9f` |
| `docs/HANDOFF_NEW.md` | M | `172d34aab580b750812f7cd2b8c68b7d6461b949615a46e497a96a70603532a0` |

## Checkpoint

Package: P06 — Build and perform the first native decision test (mechanical
  steps complete; interactive route + decision table await native run)
Baseline / Reviewed snapshot: dd-eos-watchdog-checkpoint @ 63591de4f, clean
  tracked tree at build time; candidate APK
  app/build/outputs/apk/debug/Mupen64PlusAE-debug.apk SHA-256
  c528a09f37af960be82605bfc054f667d8fa507a27fb31b4205ed1526a347912
  (43,011,377 bytes), versionName '3.0.335 (beta) 63591de4', package
  org.mupen64plusae.turnip.pwnedbygary.debug, minSdk 23 / target 34.
Verified observations: fresh host verification batch all-pass on the final
  tree — re-run at record time: test-dd-policy, test-dd-dma-transfer (both
  modes); consistent with the P05-era runs: test-dd-core-imem-dma,
  test-dd-root-stacks (73/0); dd-startup callbacks step passes, dynarec step
  still blocked by the pre-existing new_dynarec.c warning-as-error.
  Packaged ARM64 Build IDs are NEW — core d1ccfe7915c77e1778ecf085ffd38afa
  67ad5c5f, rsp-parallel 555bbd246e86bd96d1e70db990470183d7f34c2e (DDSTART11
  was 2222278b…/1ec6dc22… — no stale-library reuse). The packaged
  libmupen64plus-rsp-parallel.so exports DdRspRuntimePolicySet (the P03/P04
  seam and correction are in this APK). Unstripped ARM64 symbols preserved
  at .fzxwork/symbols-p06/ (local-only, with provenance README) and
  Build-ID-verified against the packaged libs; a first preservation attempt
  silently picked stale 2026-08-24 libraries and was caught and replaced —
  the preserved copies now match the packaged Build IDs exactly and the
  rsp lib's symbol table contains DdRspRuntimePolicySet.
Derived results and inputs: SIGNING MISMATCH — the running install is signed
  311f4e35e939256ae8df53ecbf15c43235d1ee97f8a0138d332839c5e53c2bfc (verified
  against the pulled installed APK; the Mac debug keystore), while this
  host's ~/.android/debug.keystore produces 507709400e3f1c40467e722b567261c
  4f6fb703f3264401c3369e931c52f7a46; the repo's gitignored release.keystore
  (mupen64plusae alias) is a third identity (05cbe88f…). An install -r from
  this build would fail with INCOMPATIBLE_UPDATES. Per the runbook the
  install was NOT attempted and nothing was uninstalled or cleared. The
  device (Retroid Pocket 6) remains on DDSTART11 with data intact. Note for
  resolution (a): overwriting this host's ~/.android/debug.keystore changes
  debug signing for unrelated projects on this host; a project-scoped
  signing override is the alternative if that matters.
Remaining hypotheses: none about the code; the native behavior questions
  (boot, IMEM preservation, task return, audio, command recurrence) remain
  entirely open pending the run.
Changed files: this handoff section plus a `.gitignore` addition ignoring
  local scratch paths (`.fzxwork/`, `.gradle_home/`, `files/`,
  `gradle/gradle-daemon-jvm.properties`) — the candidate APK and symbols
  remain local artifacts, not repo changes.
Checks actually run and why: gradle assembleDebug BUILD SUCCESSFUL (JDK +
  NDK toolchain; host gcc not involved); apksigner/aapt/keytool identity
  checks above; host suites re-run; packaged-lib symbol and Build ID
  verification; preserved-symbol Build-ID match against the packaged libs
  (the check that caught the stale first preservation); explicit decision
  NOT to attempt install on signing mismatch (runbook installation rule).
Independent reviewer and verdict: see "Review record" above.
Commit, if approved: see "Review record" above.
Remaining blockers: BLOCKED for install + native route until the signing
  identity is resolved. Two supported resolutions (choose one):
  (a) copy the Mac's ~/.android/debug.keystore (cert 311f4e35…) over this
      host's ~/.android/debug.keystore, rebuild, and install -r here; or
  (b) run the established Mac flow: build on the Mac at the published
      commit and use tools/capture-dd-rsp-mac.sh with this candidate's
      path/hash once an identically-signed APK exists.
  No uninstall, no clear-data, no save deletion under any circumstance.
Next eligible package and required inputs: the same P06 route once signing
  is resolved: launch with diagnostics as desired (the correction is policy-
  driven; logcat will show 'DD runtime policy set: 1' plus the native
  Dynamic Recompiler marker), follow the validation §5 route (boot/menu/
  audio/gameplay), capture logcat (+ root stacks only if it freezes), then
  classify strictly by the §5 first-run decision table. The audio question
  is decided only from that evidence.
