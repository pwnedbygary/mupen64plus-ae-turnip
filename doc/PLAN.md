# PLAN.md — N64DD on mupen64plus-ae-turnip: F-Zero X EK → menu with clean audio

Objective (user, 2026-09-11): make N64DD games boot and run properly in the recompiler
(emumode=2, parallel-RDP Vulkan) on the RP6: **F-Zero X (Japan).z64 + F-Zero X.ndd must
load fully, reach the menu, and run with clean audio**. All DD-specific changes **gated
to the DD route** (`g_dev.dd.idisk != NULL` / `IsDDPresent()`) with zero plain-game
regression (CI emumode=1 baseline keeps working: plain carts, cart-hack with
`support64dd=false`, Mario Tennis). If the in-tree fix for the guest post-load SP/RSP
deadlock fails, port the working Ares/Phobos N64DD + RSP + interrupt-delivery model from
`/home/garyb/LLM-Projects/phobos/ares/n64` (user explicitly authorized merging that code).
**No round cap — iterate until the game reaches the menu and runs with clean audio and no
major bugs remain.** doc/HANDOFF.md is ground truth; `.fzxwork/` is the scratch dir.

---

## 0. Ground truth (verified 2026-09-11, doc/HANDOFF.md round 64 is current)

- **Tree**: branch `dd-eos-watchdog-checkpoint`, HEAD `d56fc2143` (r64b). Working tree
  clean except untracked `.zcode/` — the watchdog/manifest/gradle work was already
  committed (r62b `9869e6567`, r63 `362281af9`). Housekeeping left: `.gitignore .zcode/`.
- **The measured freeze** (r63, `.fzxwork/r63/`): the guest loads an audio task
  (DMEM 0xFC0 = M_AUDTASK=2), starts it, sets SIG0 (`osSpTaskYield`),
  `sSpTaskState = SP_TASK_YIELDING`, and parks in
  `osRecvMesg(&gMainThreadMesgQueue, ...)` (`sys_main.c:343`) waiting on `EVENT_MESG_SP`
  forever. RSP runs 372 "healthy" slices (344 ms), then DMEM 0xFC0 is zeroed (entry 373)
  and IMEM is replaced by a copy of RDRAM 0x3c018 data (entry 374); 12,833 byte-identical
  no-op slices follow at SP_STATUS=0xC0 until the trace cap. No gfx task ever ran
  (`FRAMEPROTO gfx=0`, `rdpkick=0`).
- **The round-64 contract** (disassembled from the running ROM's `__osException` at
  `0x80746800`): the guest posts `EVENT_MESG_SP` **only if**
  `SP_STATUS & (SIG1|SIG2)` is set at the moment of the interrupt
  (`SP_STATUS_YIELDED = 0x100`, `SP_STATUS_TASKDONE = 0x200`, rsp_core.h:47-50);
  otherwise it posts `OS_EVENT_SP_BREAK` (event 11, which nothing in this ROM waits on)
  and consumes the interrupt (`SP_STATUS = 0x8008` = CLR_INTR|CLR_SIG3, which re-enters
  the RSP via `update_sp_status` → `do_SP_Task`).
  **Rule: raise `MI_INTR_SP` only when `SP_STATUS & (SIG1|SIG2)` is set.**
- **Current default DD model raises MI_INTR_SP at exactly two sites, both un-gated on
  SIG bits**: `rsp_core.c:1104` (`do_SP_Task` DD branch, running→halted transition) and
  `rsp_core.c:1284` (`rsp_dd_slice`, halt/broke after a slice).
- **But the raise-gate alone cannot cure**: in the frozen run the RSP never halted or
  broke, so no legal raise ever existed. Two open defects:
  1. **Who wrote IMEM** (content = RDRAM 0x3c018 data, not the guest's 0x00010001 fill)
     and **who zeroed DMEM 0xFC0..0xFFF** (the task header) at t=344 ms?
  2. **Why did the audio ucode never complete or yield** despite SIG0 set for 344 ms?
     (On hardware/ares it would process the AList and finish via SIG2+BREAK.)
- **Plugin facts that shape the fix** (verified):
  - The DD-route plugin DMA **already bank-wraps** (round 19, `cp0.cpp:2114-2126`:
    `dest_addr = wd_dbank | ((dest + j) & 0xFFC)` with `wd_dbank = dest & 0x1000`) — a
    DMEM-destined transfer cannot cross into IMEM in current code. So S1 must log
    **every** IMEM write site, not just the DMA loop.
  - The plugin **cannot queue SP events**: `CheckInterrupts = EmptyFunc` (plugin.c:675);
    its only outward channels are the aliased pointers `*RSP::rsp.SP_STATUS_REG` and
    `*cp0.irq` (= core MI_INTR bit 0). Delivery happens only when the core's consumers
    run (`rsp_core.c:1178-1183` consumer, `rsp_interrupt_event` 1457-1472).
  - Budgets: `parallel.cpp:1493-1501` sets per-slice unit budgets (DD default 256/512
    units for audio tasks); `RSP::SP_STATUS_TIMEOUT = 0x7fff` for DD (MFC0 poll count
    gate for the synthetic-yield block, cp0.cpp:~1490-1508), which requires
    `hdr_ok` (`ysize >= 0xc00`) — the EK audio task has `yield_data_size = 0xd8`, so
    that path is dead code for this task (r64 §6).
- **Authorized fallback reference** (measured in `/home/garyb/LLM-Projects/phobos/ares/n64`):
  - **No TASKDONE/YIELDED modeling at all.** Completion = `BREAK` with
    `interruptOnBreak` set → `mi.raise(MI::IRQ::SP)` → level bit → CPU IP2 pending,
    visible at the top of the next CPU instruction (`rsp/interpreter-ipu.cpp:54-66`,
    `mi/mi.cpp:19-53`, `cpu/cpu.cpp:127-157`).
  - **RSP is time-sliced by CPU cycle debt**: `RSP::main()` runs inside
    `CPU::synchronize()` on the same host thread; no cross-thread irq races, no separate
    scheduler. Start = SP_STATUS HALT-clear write + `cpu.forceSynchronize()`.
  - **SP DMA wraps per 4 KB bank** (`n12` pbus wrap, region bit 12 selects IMEM/DMEM;
    `ares/memory/writable.hpp:23` mask) — a DMA can wrap within a bank, never spill.
  - **DD**: PI bus device 2, ASIC regs 16-bit at 0x0500_0500; MECHA/BM IRQs OR'd into
    CPU::Interrupt::Cartridge (IP3), not MI; queue events for mecha response / BM
    request / motor; `.ndd` read via zone tables; IPL CIC derived from header.
  - **DD RTC**: Phobos seeds the DD RTC with host time in BCD because an erased/all-zero
    RTC makes F-Zero X EK show **"Error 48 — Date/Time not set"** on every boot
    (`dd/rtc.cpp:18-33`). This is a *known next blocker* once we reach the menu.

## 1. Standing rules — every round

1. **Gating**: every DD change behind `g_dev.dd.idisk != NULL` (core) /
   `IsDDPresent()` (plugin). Plain-route behavior byte-identical; the pre-ares
   `rsp_interrupt_event` path stays exact for plain carts.
2. **Regression gates per code round**:
   - (a) Mario Tennis (USA).zip plain cart (r59plain_run.sh pattern): animating
     screenshots, zero `wd_*` files, no tombstones;
   - (b) emumode=1 CI baseline: DD route via `wd_emumode1.flag` plus plain carts at
     `r4300Emulator=1`;
   - (c) `./gradlew :app:assembleDebug` green (GitHub CI is build-only).
3. **Build trap**: gradle silently skips native rebuilds when only C/C++ changed —
   remove the plugin's build intermediates first and verify a new marker string inside
   the packaged .so (`unzip -p ... lib/arm64-v8a/libmupen64plus-*.so | strings | grep`).
4. **Device**: RP6, serial `49016109` (`A="adb -s 49016109"`); package
   `org.mupen64plusae.turnip.pwnedbygary.debug`; pull via
   `adb shell "run-as $P cat $D/<file>"` (adb pull fails on app-uid files); sleeps
   ≤55 s (tool timeout); parens in paths break `run-as … sh -c` (push a script);
   locked keyguard parks the emulator (debug manifest overlay handles it);
   tombstone clearing needs the vendor "Run script as root" option.
5. **Bookkeeping**: end every round with a clean commit (code + doc/HANDOFF.md round section
   at top + force-add new durable `.fzxwork` tools if any) and
   `mnemon remember "<insight>" --cat context --imp 5 --tags branch:dd-eos-watchdog-checkpoint`.
   Commits stay local; push only on request.

## 2. Round 65 — S1: identify the IMEM/header writer and the ucode's non-completion

One instrumented device run. Diagnostics only — no behavior change.

1. **Code (DD-gated)**:
   - Grep **all** IMEM/DMEM write sites in `mupen64plus-rsp-parallel/upstream/`
     (rsp/cp0.cpp, parallel.cpp, rsp_jit.cpp, the ares-RSP store path) and all
     `sp->mem[...]` writes ≥0x1000 in the core's `do_sp_dma`. Verify whether
     `r19_imem_note`'s gate can even fire (r63 §4 candidate 3 — its file `wd_imem.txt`
     never appeared; check the gate before trusting its silence).
   - Add a one-line, `wd_deep.flag`-bounded log at **every** IMEM write site: chunk idx,
     source, source_addr, dest_addr, stored word, `rdram/imem/dmem` host pointers,
     SP_STATUS, guest pc. Same for any write into DMEM 0xFC0..0xFFF (who zeroed the
     header). Known signature to look for: writes crossing from dest < 0x1000, and
     content matching RDRAM 0x3c018 data.
   - Log **SIG1|SIG2 0→1 transitions** in the plugin (the ucode setting YIELDED/TASKDONE)
     plus a bounded MFC0-SP_STATUS poll log (value seen) — answers why the ucode never
     completed with SIG0 set.
2. **Run**: `.fzxwork/r65_run.sh` (r62_ab.sh skeleton: install, force-stop, clean wd_*,
   touch wd_trace+wd_deep, logcat -c, content:// launch of
   `F-Zero X (Japan).z64`, timed screenshots, wd_force dump, pull everything).
3. **Analyze offline** (`ram_tools.py` — MIPS64 mode; `ek_sym.py`): name the writer;
   extract the ucode's actual behavior at the SIG0 check; decide S2 scope.
4. Commit r65 + HANDOFF + mnemon.

## 3. Round 66 — S3+S2: contract-gated raises + containment, per R65 findings

1. **S3 raise rule** (rsp_core.c, both DD sites): gate the `MI_INTR_SP` raise on
   `sp->regs[SP_STATUS_REG] & (SP_STATUS_YIELDED | SP_STATUS_TASKDONE)` at ~1104 and on
   the `raise_rcp_interrupt` at ~1284. Keep `rsp_interrupt_event`'s forced TASKDONE
   (1462-1471) — it satisfies the contract for genuine completions. A/B flag
   `wd_nosiggate.flag` restores un-gated raises for one-run diffing.
2. **S2 containment** per R65's answer: fix whatever path actually wrote IMEM
   (candidates: an unwrapped plugin write site; the core `do_sp_dma` length on DD;
   the r14_wild-refusal path). Hardware property, not a heuristic: an SP DMA cannot
   leave its 4 KB bank. If R65 shows the writer is elsewhere (e.g., a JIT scratch
   path), fix that path instead.
3. **Device run + S4 read**: does the guest leave `SP_TASK_YIELDING` (stall dump),
   `FRAMEPROTO gfx>0`, `rdpkick != 0`, menu screenshot? Full regression suite (§1.2).
4. Commit r66 + HANDOFF + mnemon.

## 4. Round 67 — decision point

- **Menu reached** → polish track (§5).
- **Still frozen** → **port track** (user-authorized; a *reconciliation*, not a rewrite —
  the plugin's ares-derived RSP register aliasing is already correct, r64 §6).
  In order, one device A/B per step, all DD-gated:
  1. **Completion model**: ucode BREAK + INTR_BREAK → level SP raise (ares
     `rsp/interpreter-ipu.cpp:54-66` semantics), delivered via core
     `raise_rcp_interrupt` + `add_interrupt_event` (the plugin can't queue events),
     still SIG-gated per the round-64 contract.
  2. **Slice scheduling**: budget the RSP slice by emulated **CPU cycle debt** (ares
     runs the RSP inside `CPU::synchronize()`) instead of the wall-clock 256/512-unit
     heuristic.
  3. **Spin-behind-poll sync**: forceSynchronize equivalents on SP_STATUS/SP_SEMAPHORE
     polls (extend the existing MFC0-count gate in cp0.cpp).
  4. SP DMA bank containment if not already landed in r66.

## 5. Polish track (menu reached → goal closure)

1. **DD RTC**: port Phobos' host-time BCD seeding (`phobos/ares/n64/dd/rtc.cpp:18-33`)
   into our dd_controller to preempt "Error 48 — Date/Time not set" on EK boot
   (DD-gated).
2. **Audio**: audio tasks completing at the expected rate, AI DMA without underruns,
   clean logcat; final gate is user play-test on the RP6.
3. **Stability**: 10-min race run, no tombstones, watchdog silent, screenshots archived
   under `.fzxwork/rNN/`.
4. **Full regression**: Mario Tennis, MK64 Amped Up, OoT (plain), emumode=1 on both
   routes, cart-hack with `support64dd=false`; GitHub CI green.
5. **Closure**: final doc/HANDOFF.md round with measured evidence, mnemon remember, summary
   of remaining known issues.

## 6. Traps carried forward (do not re-learn)

- **Dump byte order** (cost two raw-scan mistakes 2026-09-11; also in `ram_tools.py`
  docstring trap 3): every dump file (`iplram_*.bin`, `ram_force.bin`, `wd_r29sp.bin`)
  stores each guest 32-bit word in **host little-endian order** — recover the guest's
  big-endian value with `struct.unpack_from('<I', raw, off)`, never `'>I'`. Signatures
  of having it backwards: an expected word (0x340a0fc0) reads as its bswap
  (0xc00f0a34); an opcode/mfc0 scan finds nothing; the 0x00010001 fill reads as
  0x01000100. Prefer `ram_tools.py` (unswaps once in `load()`) over raw scans.
- `wd_spw.txt`'s `st_after` is synthetic (`rsp_core.c:610`); `wd_r29sp.bin` is
  **IMEM-then-DMEM** (`parallel.cpp:769`), not a raw `sp->mem` image; RDRAM dumps are
  word-swapped inside each 32-bit word (use `ram_tools.py`); capstone must be
  `CS_MODE_MIPS64` (it silently stops on MIPS III ops otherwise).
- Byte-identity between an SP bank and an RDRAM region is what a *correct* DMA produces
  — not evidence of a bug (r64 §5).
- Settled, do not re-litigate: DD data path byte-correct (`ds_buf` == RDRAM == .ndd);
  guest clock = VI clock 48.68 MHz; DD ASIC progress identical across configs
  (`c_asic=10604`); pump/slice-budget/CPU-starvation ruled out (r57-r62); the plugin's
  SP_STATUS is the core's (aliased, r64 §6); the raise reaches the CPU
  (mi_controller.c:161 chain is fine — only the condition is wrong).
- SP write bits are the *write* numbering (`rcp.h:195-256`; SET_SIG0 = 1<<10), not the
  read bits.

## 7. Operational recipes

**Build** (from repo root):
```bash
rm -rf mupen64plus-rsp-parallel/build/intermediates/cxx mupen64plus-rsp-parallel/.cxx \
       app/build/intermediates/merged_native_libs app/build/intermediates/stripped_native_libs
GRADLE_USER_HOME=$PWD/.gradle_home ./gradlew :app:assembleDebug --offline   # ~8-13 s
unzip -p app/build/outputs/apk/debug/Mupen64PlusAE-debug.apk lib/arm64-v8a/libmupen64plus-*.so \
  | strings | grep <new marker>
```

**Deploy + run** (skeleton `.fzxwork/r62_ab.sh`):
```bash
P=org.mupen64plusae.turnip.pwnedbygary.debug
A="adb -s 49016109"; D=/data/data/$P/files
$A install -r app/build/outputs/apk/debug/Mupen64PlusAE-debug.apk
$A shell am force-stop $P
$A shell "run-as $P sh -c 'cd $D && rm -f wd_* iplram_*.bin'"
$A shell "run-as $P touch $D/wd_trace.flag $D/wd_deep.flag"
$A shell logcat -c
$A shell "am start -a android.intent.action.VIEW -d 'content://com.android.externalstorage.documents/tree/EBFF-F6C0%3AROMs%2Fn64/document/EBFF-F6C0%3AROMs%2Fn64%2FF-Zero%20X%20%28Japan%29.z64' $P"
sleep 30; $A exec-out screencap -p > $O/t030.png
$A shell "run-as $P touch $D/wd_force.flag"; sleep 8
$A shell "run-as $P cat $D/wd_stall.txt" > $O/t090_stall.txt
$A shell logcat -d > $O/logcat.txt
```
DD disk + IPL are attached via per-game prefs (game ID
`58D200D43620007314304F4E6C9E6528`, `support64dd=true`, `diskPath64dd=…/F-Zero X.ndd`).
Emulation profile: `files/Profiles/emulation.cfg` (`r4300Emulator=2`, Parallel video/RSP
profile, 8 MB pak).

**Offline analysis**:
```bash
RAM_DUMP=<dump> python3 .fzxwork/ram_tools.py dis 0x80746ad0 20   # MIPS64 disasm
python3 .fzxwork/ek_sym.py 80750384                               # decomp symbols
```

**Mnemon**: `mnemon remember "<text>" --cat context --imp 5 \
  --tags branch:dd-eos-watchdog-checkpoint --entities mupen64plus-ae-turnip`
(`mnemon search|recall|status` to read back.)
