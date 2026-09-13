# P00 baseline record — N64DD DMA/audio repair

Recorded 2026-09-13 by the coordinator session. This is a read-only inventory:
no emulator behavior was changed. All file references are relative to the
repository root unless marked local-only.

## 1. Source identity

| Item | Verified value |
|---|---|
| Checkout | `~/LLM-Projects/mupen64plus-ae-turnip` (this repository) |
| Branch | `dd-eos-watchdog-checkpoint` |
| HEAD | `9df1b637d` ("Add portable local-agent instructions and coordinator startup prompt") |
| Remote sync | HEAD equals `origin/dd-eos-watchdog-checkpoint` (no ahead/behind) |
| Tracked-tree state | Clean at inventory time (`git status`: no modified/deleted tracked files; the handoff edit recording this baseline was applied after inventory and is part of this commit) |
| Documented baseline publications | `f66e63dea` and `bd7aba4b7` both present in history (object-verified) |

Untracked paths, accounted as scratch (not part of the baseline, not to commit):

| Path | Contents |
|---|---|
| `.fzxwork/` | build logs, boot screenshots, `apkcheck`, `budget_*` artifacts, `r62_parked/ares_thread.{cpp,hpp}` (parked historical experiment — do not import), `device_base.apk` (stale Aug 29 device pull, SHA-256 `4ff371cd…`; **not** DDSTART11 — keep out of any P06 old-APK reuse) |
| `.gradle_home/` | local Gradle 8.4 cache/JDKs (build scratch) |
| `files/` | `wd_*` watchdog-era debug text/binaries (historical evidence) |
| `gradle/gradle-daemon-jvm.properties` | generated Gradle daemon JVM toolchain pin (Java 17) |

No uncommitted handoff edits were found (external-agent rule checked).

## 2. Production code state (P00 step 3)

`mupen64plus-rsp-parallel/upstream/rsp/cp0.cpp` `rsp_dma_read` (lines 217–320)
still matches the documented legacy behavior exactly:

- Length decode `(reg & 0xFFF) + 1`, 8-byte aligned (`cp0.cpp:220,241`).
- Raw 12-bit skip `(reg >> 20) & 0xFFF` — no low-bit alignment (`cp0.cpp:222`).
- Bank-boundary clamp: `length = 0x1000 - (dest & 0xFFF)` (`cp0.cpp:248-249`).
- 13-bit destination progression `(dest + j) & 0x1FFC` permitting DMEM→IMEM
  crossing; `dest += length` per row (`cp0.cpp:279,303`).
- Register masking: SP `&= ~0x3`, DRAM `&= ~0x7` (`cp0.cpp:244-245`).

The captured request `SP=0xfb0, RDRAM=0, length=0xffffffff` decodes to
row length 4096 → clamped to 0x50 = 80 bytes, count `0xff` → 256 rows,
skip 4095 — exactly the DDSTART11 record (80-byte rows, 256 rows, 5120 words,
3052 IMEM word writes). No delta exists; the documented baseline is current.

## 3. Artifacts and references

| Item | Status |
|---|---|
| DDSTART11 APK | **Installed on the device and hash-verified**: pulled `base.apk` SHA-256 `ffc6459f…cb2df1` equals the documented DDSTART11 hash. No copy of this APK exists in the workspace. |
| Local build output | `app/build/outputs/apk/debug/Mupen64PlusAE-debug.apk` SHA-256 `384b673a…`, dated Sep 11 — a stale build, **not** DDSTART11. |
| Raw capture archive | `ddstart10-rsp.ZJNdWU_1789303426018.zip` (SHA `a89cf94d…`) is **not present on this host**; its derived evidence lives in `docs/DDSTART11_NATIVE_ANALYSIS.md`. Local-only artifact, nothing to commit. |
| Ares/Phobos reference | `~/LLM-Projects/phobos` (local-only), clean git checkout pinned at `f1174e7654141accad40b9ffc2c7d978e93a00f0` (2026-09-01). All four plan-named files exist: `ares/n64/rsp/rsp.hpp`, `io.cpp`, `dma.cpp`, `rsp.cpp`. Companion `phobos.zip` SHA-256 `2ab44d5d23917d7343776a235b9aa42eca398f32ac27fa65d8337059327d2c5c` (798 MB). |
| Ares model spot-check | `dma.cpp` confirms the plan's description: separate latched `pbusRegion` (bank bit) and advancing 12-bit `pbusAddress`, 8-byte beats, recompiler invalidation only when `pbusRegion` is set. P01 still owes the complete row-by-row comparison. |
| CXD4 | `mupen64plus-rsp-cxd4/upstream/su.c` present (71 KB). |
| Core RSP | `mupen64plus-core/upstream/src/device/rcp/rsp/rsp_core.c` present. |
| Plan's `attached_assets/phobos-master_…zip` | Path absent from this checkout; the pinned local Phobos checkout substitutes. Equivalence of the DMA-relevant sections must be confirmed during P01 before relying on it. |

## 4. Toolchain, signing, device

| Item | Verified value |
|---|---|
| Host | CachyOS Linux, gcc 16.2.1, OpenJDK 17.0.20, Gradle on PATH (+8.4 in `.gradle_home`) |
| Android SDK | `/opt/android-sdk` (via `local.properties`); NDKs 26.1.10909125, 28.2.13676358, 29.0.14206865; build-tools 34/35/36 |
| ABIs (app/build.gradle:114) | armeabi-v7a, arm64-v8a, x86, x86_64 |
| Signing | `keystore.properties` present at repo root (contents not inspected, not disclosed); `app/build.gradle` uses its release config when present, else debug |
| Device | Retroid Pocket 6 (USB serial `49016…`, product kalama), authorized |
| Installed package | `org.mupen64plusae.turnip.pwnedbygary.debug` — pulled and hash-verified as DDSTART11 (§3) |
| `tools/replit-build.sh` | Absent (Replit-specific; local Gradle is the documented path) |

## 5. Host test status (P00 step 6; run on this host, no source changes)

| Command | Result |
|---|---|
| `bash tools/test-dd-startup.sh` | Callbacks test **passes**; dynarec-observer test **fails to compile**: `new_dynarec.c:4661,4739` `-Werror=unused-but-set-variable` under gcc 16. Fault-layout and rsp-provenance steps not reached. Toolchain portability issue, not an emulator defect. |
| `bash tools/test-dd-core-imem-dma.sh` | **Passes** (DDSTART11 core CPU→IMEM observer suite). |
| `bash tools/test-dd-rsp-mac.sh` | 6 pass / 8 fail — the mock-based Mac capture-helper orchestration tests fail on this Linux host (fixture artifacts not produced). Pre-existing host portability issue; the helper itself targets the documented Mac. |
| `bash tools/test-dd-root-stacks.sh` | **73/73 pass.** |

The P02 transfer fixture remains a deliverable to create; the validation
runbook's command ledger is otherwise confirmed present.

## 6. Fact / unknown ledger (matches DDSTART11 record)

**Directly established** (from DDSTART11 capture `dSOS6x`, analysis committed):

- Internal Parallel-RSP DMA read with raw operands `SP 0xfb0 / RDRAM 0 / length 0xffffffff`
  during audio task entry 1512 wrote 3052 IMEM words and mutated IMEM (hash delta,
  before/after samples), and the suspect words appear in later JIT compile inputs.
- Legacy decode of that request reproduces 80-byte clamped rows × 256 = 5120 words.
- The source still implements that legacy behavior (§2). The device still runs
  the DDSTART11 diagnostic build (§4).

**Reference-supported** (pinned Phobos checkout, spot-checked):

- Ares latches the bank and advances a 12-bit offset with full row length,
  differing from the current clamp/13-bit progression.

**Unresolved hypotheses** (unchanged from the plan):

- Why the command's size/source fields were zero (producer, reuse, corruption,
  fetch, decode — all open). The issuing site is explicitly `unknown`.
- Whether a reference-correct DMA alone restores gameplay/audio.
- Skip low-bit and post-transfer register semantics are undecided (P01).
- Native acceptance (menus/gameplay/audio, cart/save regressions) outstanding.

## 7. Source-file ownership list (for later packages)

| Area | Files | Package |
|---|---|---|
| Parallel-RSP DMA | `mupen64plus-rsp-parallel/upstream/rsp/cp0.cpp` | P04 |
| RSP diagnostics | `mupen64plus-rsp-parallel/upstream/rsp_diag.cpp/.hpp`, `parallel.cpp` | P03b/P05 |
| Core policy/plugin bridge | `mupen64plus-core/upstream/src/api/callbacks.c`, `src/plugin/plugin.c/.h` | P03a |
| Frontend wiring | `app/src/main/java/.../jni/CoreInterface.java` | P03a |
| Tests | `tools/tests/*`, `tools/test-dd-*.sh` | P02 (+ P03c end-to-end truth-table tests; P02/P03 share the P01 prerequisite) |
| Handoff/docs | `docs/HANDOFF_NEW.md`, `docs/P00_BASELINE.md` | all |

P02 and P03 touch disjoint files and may proceed in parallel with explicit
ownership. P04 waits for both.

## 8. P00 acceptance and stop check

- Acceptance: another engineer can identify the exact source (`9df1b637d`,
  clean), the tested APK (DDSTART11 `ffc6459f…`, hash-verified on the device),
  and the capture (`dSOS6x`, 2026-09-13, via the committed analysis).
- Stop conditions: none triggered — baseline matches the documented starting
  point, all dirty paths are accounted scratch, APK identity verified.
- Missing inputs: none blocking P01. Reference equivalence confirmation and the
  full policy table belong to P01 by design.
