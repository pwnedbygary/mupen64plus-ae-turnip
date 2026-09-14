# P07 checkpoint — command evidence, frozen-memory capture, and the root-free recapture

Published from [HANDOFF_NEW.md](HANDOFF_NEW.md) (P07 and P07-R sections);
commits `f9ddf4bb5` … `ef8e0b783` plus the P07-R commit on
`dd-eos-watchdog-checkpoint`.

## Review record

- Every commit in this series was pushed only after an independent
  read-only review of the exact snapshot (per
  [DEVELOPMENT_PROCESS.md](DEVELOPMENT_PROCESS.md)); verdicts and
  reviewed-snapshot hashes are recorded in each commit message, the
  protocol-designated location.
- `ef8e0b783` (the offset-validation/scope doc delta) followed the full
  loop: first review NEEDS CHANGES (three LOW wording findings — a false
  capture-time chain-validation claim, inverted shift arithmetic, a loose
  "same guest offsets" phrase) -> findings resolved -> delta re-review
  PASS, with the new content-check word and all offset checks re-verified
  against the dump.
- The P07-R recapture (frozen IMEM verification and the DMEM traffic
  characterization) was performed root-free via `run-as` and
  `/proc/<pid>/mem`; its reviewed-snapshot hashes are recorded in the
  P07-R commit message.
- The snapshot table below is recomputed from the published commits
  themselves (`git show <commit>:<path>`), so it cannot drift from what
  was reviewed and committed. New-file rows show the file's first
  published state.

| Commit | Path | SHA-256 (git show recomputed) |
|---|---|---|
| `f9ddf4bb5` | `docs/HANDOFF_NEW.md` | `b2de914e91c73dfb2ffe42e5296acbbc3e055f7b04c5e5c9afc5de28db7e505c` |
| `f9ddf4bb5` | `docs/P07_COMMAND_EVIDENCE.md` | `85061581a27c9437d583d2c9619f23466c9db7728bda6aec4d415fbbacf52504` |
| `f9ddf4bb5` | `tools/p07-frozen-memdump.sh` | `8bb258cce61226b884317678bf7bd0d603e90c99311ab78abb81d87a5d0617e7` |
| `2543aa3dc` | `docs/HANDOFF_NEW.md` | `164674ad695cdcd213586f8d3ba1285c28d11e67c5cfb3481bf3426b5077ef49` |
| `2543aa3dc` | `docs/P07_COMMAND_EVIDENCE.md` | `4858cf3542114344928f84c349c380e8c4e10d25e65286eba3288c42a1f50f08` |
| `2543aa3dc` | `tools/launch-p07-memdump.sh` | `3b9731f28445421b0bc3debaf5d252a38609f1bb6f2d5965156afe85317aa935` |
| `36c8e091f` | `docs/HANDOFF_NEW.md` | `878c91019b3400a01acf3b085a743c05f8598de3505a3c9b7bd0b026711aa7a1` |
| `36c8e091f` | `tools/p07-frozen-memdump.sh` | `422e607be9489d2be5f1f3061cd6703d2dfbdf97a18525d3b63997fc6a84adb8` |
| `36c8e091f` | `tools/test-p07-memdump.sh` | `c9dc84a1c58531aa77c1f599cd79f8456f0d6900c95eefb2d3cc94840b0c2d8a` |
| `e5f8c9887` | `docs/HANDOFF_NEW.md` | `76351afbbf44d92dad310f45401f4f1a6cbd6f6ebfee277c8a4da8a3e8ab9b0d` |
| `e5f8c9887` | `tools/p07-frozen-memdump.sh` | `3a3d6767a258a5c36e30b2e518c6fc670e65370bbee51f0f75e2627492cdb50e` |
| `367710d40` | `docs/HANDOFF_NEW.md` | `6ea9bcb42519d2c5705d41f7dc360e41218896892f003502ef9bc91d631b3676` |
| `367710d40` | `docs/P07_COMMAND_EVIDENCE.md` | `beb27988f997d78a0d45678a343c5acc5051028781e99aafe4de14f9f359412e` |
| `367710d40` | `tools/p07-frozen-memdump.sh` | `6c0e919547fff90407d4e6ea3ca71f6bdb6cd185ab006cc850b3263307a8b3bc` |
| `367710d40` | `tools/test-p07-memdump.sh` | `3862199d2e5a8a40b486f84ea7b15725c142772c46283b102de0ba932f7d8cf5` |
| `f6b74c3ab` | `docs/HANDOFF_NEW.md` | `24005eb5577058fba7ba65fce89d0eb3e84e55f9ac6451f55881d98ea9d86298` |
| `f6b74c3ab` | `docs/P07_COMMAND_EVIDENCE.md` | `1d4a72ede2211392a02984703db53c8ea3cd47b605627f895f4dca13cf221830` |
| `f6b74c3ab` | `tools/p07-frozen-memdump.sh` | `6552f383b2104370a09bf46dc1a447b2e47782584c9b7c981cefb3e12116e8ef` |
| `f6b74c3ab` | `tools/test-p07-memdump.sh` | `7dd17e818b9680e8e109936fa428129188bea17a09ae6ffac40e13cb8846c37b` |
| `6b1b91968` | `docs/HANDOFF_NEW.md` | `922db5ff7a39f19841816a33656368961393187488c3e2ab953de851f2105f61` |
| `6b1b91968` | `docs/P07_COMMAND_EVIDENCE.md` | `6014ab6a825b3b7fa350c2d81bfe06c55b18bcb0c6f8367ec9f52f82b4217151` |
| `6b1b91968` | `tools/p07-frozen-memdump.sh` | `38eaba272b83738b251a89d4f294216b6e54a2d131686adbbfd3c09b595fe69c` |
| `6b1b91968` | `tools/test-p07-memdump.sh` | `51047f77829652275eaec59649ab7050395e728641c876331a63648f0adf575b` |
| `ef8e0b783` | `docs/HANDOFF_NEW.md` | `cffe47f7df55f26998b81c092cd56483ecc9b4a2f5dc702a4edecb64fad34c4e` |
| `ef8e0b783` | `docs/P07_COMMAND_EVIDENCE.md` | `b503f75d8e9cf703e3ecfd9ec4f1627f401014cb388161375b85c9ac068b93a8` |

The P07-R commit (this file's commit) touches
`docs/P07_COMMAND_EVIDENCE.md`, `docs/HANDOFF_NEW.md` and this file; its
reviewed-snapshot hashes and verdict are recorded in the commit message.

## P07 checkpoint (frozen-memory capture and conclusion)

Package: P07 — bounded, coherent frozen-memory capture of the stuck
  emulation process, to identify what the RSP consumed at the freeze.
Baseline / Reviewed snapshot: 2450922a2, clean tracked tree; device-side
  root helper pushed in stages as the probe was debugged.
Verified observations: the 11:39 capture at the (then unrounded) base
  contained [last 4 KiB of the RDRAM area][DMEM]; the active task chain is
  reachable and content-anchored (gCurAudioTask 0x806EEAA0 → descriptor
  type 2, ucode/ucode_data 0x80768e60/0x1000, words 12-13
  0x80411910/0x1a0); every word of the 0x1a0-byte buffer at guest 0x411910
  is zero; a bytewise zero run spans guest [0x3DA9EF, 0x6ECA10) (3,219,489
  bytes) ending exactly at gAudioCtx; the aspMain image at guest 0x768E60
  and DMEM are populated; boot-time PI DMA copies had written into
  0x400008+ hours earlier, so the region had content before the freeze.
Derived results and inputs: the consumed audio command is a genuine zero
  word; the microcode's zero size/source extraction, the 0xffffffff length
  register and the repeated DMA-helper calls are consequences of consuming
  a zero-filled command list, and the loop never terminates. The divergence
  boundary is the producer/loader side; the DMA correction remains
  necessary (it is why this no longer corrupts IMEM).
Remaining hypotheses: game-side heap clear without a completed rebuild; an
  emulated disk/cart load delivering zeros (DD-specific); an emulator-side
  RAM clear.
Independent reviewer and verdict: see "Review record" above.
Next eligible package and required inputs: P08 — bounded writer
  observation on [0x3DA9F0, 0x6ECA10) with generation tracking.

## P07-R checkpoint (root-free recapture; frozen loop characterized)

Package: P07-R — recapture the RSP window at the validated base (IMEM was
  the missing 4 KiB) and characterize the frozen loop, without a device
  run.
Baseline / Reviewed snapshot: 6b1b91968 (plus the separately reviewed
  `ef8e0b783` doc delta).
Verified observations: `/proc/<pid>/mem` and `/proc/<pid>/maps` are
  readable without root through `run-as` as the app uid; the base derives
  per run (mapping 0x6fc2c5f000 with a `---p` region immediately below it,
  buffer base 0x6fc2c60000 = start + 0x1000) and every read is
  content-anchored (gCurAudioTask word 0x806EEAA0). Frozen IMEM is the aspMain image
  bit-exact — FNV 0x3aaaf0f5f121410e equal to the RDRAM image at guest
  0x768E60 and to the P05 observer's in-process hashes, with its four
  sample offsets returning exactly the recorded samples. The window at
  mem_base + 0x04000000 is [DMEM][IMEM]. RDRAM is byte-identical across
  captures 22 minutes apart (guest-aligned 8 MiB diff: 0 bytes), so the
  heap zeroing is historic, not ongoing. The loop is active: ~100 ticks/s
  host CPU, IMEM never changes, DMEM is rewritten faster than a 62 ms
  interval (3.0-3.7 KB per interval between samples that both hold a
  block), with DMEM[0:752] equal to RDRAM[src:src+752] in 10 of the 32
  sampled states (752 = 0x2F0, the §8 call-site constant). Both task slots
  match the P05 task-word record except where noted (KSEG0-bit words 4, 6
  and 12); slot 0's words 12-13 are the record's 0x00411910/0x1a0 pair on
  that basis, while slot 1's are a different pair, 0x804132d0/0x1c0 — the
  buffer §8's table records at guest 0x4132d0.
Derived results and inputs: the freeze is a live loop reading RDRAM and
  rewriting DMEM while RDRAM and IMEM stay untouched; P08 can use the
  root-free method instead of (or alongside) an instrumented build.
Remaining hypotheses: producer-side zeroing (unchanged from P07); open:
  DMEM writer attribution (RSP vs emulated CPU) and the scattered src
  pattern.
Independent reviewer and verdict: see "Review record" above.
Next eligible package and required inputs: P08 — reproduce the freeze in
  one native run with the root-free sampler alongside; no emulator
  behavior change.
