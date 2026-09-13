# N64DD DMA correction and audio-command investigation

## 1. Purpose and recommended execution size

This is an implementation **plan**, not a claim that the emulator has been fixed.
It is based on the completed DDSTART11 Android capture and the current source.
No emulator behavior is changed by these documents.

Read this set in order:

1. **This file:** evidence, architecture, decision gates and scope.
2. [Work packages](N64DD_DMA_AUDIO_WORK_PACKAGES.md): small assignments suitable
   for a local LLM, with dependencies and acceptance conditions.
3. [Validation runbook](N64DD_DMA_AUDIO_VALIDATION.md): concrete fixtures,
   native checks, release records and failure decisions.

### Recommendation

Do **not** ask a model to “fix DMA and audio in one shot” without checkpoints.
The DMA defect is demonstrated; the audio-command origin is not.

The smallest defensible overall sequence is:

1. Agree on the transfer semantics and implement/test the DD-only DMA correction.
2. Include narrowly scoped audio evidence in that first candidate.
3. Run the candidate on the actual Android device.
4. Use that result to decide whether an audio correction is needed and where.
5. Validate native gameplay/audio and independent cart/save behavior.

An experienced model can combine packages P00–P05 into one **coding engagement**,
but it must preserve their internal review gates and separate commits.
It cannot honestly combine the unobserved audio cause and native acceptance into
that same one-shot claim. A smaller local model should receive one package at a
time, not this entire investigation as an unconstrained coding request.

The numbered packages below are proposed work, not completed work.

## 2. Authoritative starting point

### Source and artifacts

| Item | Known baseline |
|---|---|
| Repository | `pwnedbygary/mupen64plus-ae-turnip` |
| Working publication branch | `dd-eos-watchdog-checkpoint` |
| Last implementation publication before this plan | `f66e63dea103377adc50cebbb1b6d78f4fcddfa3` |
| Subsequent native-analysis publication | `bd7aba4b756f63302c35512ab20ecac902fb79b1` |
| Last tested diagnostic APK | DDSTART11 |
| APK SHA-256 | `ffc6459fc6c7a5ae53a11f5cf54b7bc8f8c9aa880e53600a7a1633de15cb2df1` |
| Android package | `org.mupen64plusae.turnip.pwnedbygary.debug` |
| Signing certificate SHA-256 | `311f4e35e939256ae8df53ecbf15c43235d1ee97f8a0138d332839c5e53c2bfc` |
| Core ARM64 Build ID | `2222278b76f02ac42011a62e9e9f2934fc94534b` |
| Parallel-RSP ARM64 Build ID | `1ec6dc22de63e0070245fd4ff147af3ed9e40553` |

Recheck the live branch before starting. Do not reset it to these commits or
overwrite later work. An APK's displayed version string is not a substitute
for its hash and packaged library identities.

Primary evidence:

- `docs/DDSTART11_NATIVE_ANALYSIS.md`
- `docs/DDSTART11_IMEM_DMA_PROVENANCE.md`
- The latest relevant sections of `docs/HANDOFF_NEW.md`
- `replit.md` for project constraints

Raw capture:
`ddstart10-rsp.ZJNdWU_1789303426018.zip`, SHA-256
`a89cf94d4357780f54fb77551020f08c71d5938ca4eca71ae620e616957bccd3`.
Its old filename prefix is a helper convention, not the APK identity.
Raw uploads are local evidence, **not material to commit**.

### Portable reference handling

The earlier task names `doc/N64DD_REFERENCE_REVIEW.md`,
`doc/N64DD_WEB_REFERENCES.md`, `doc/N64DD_DEVICE_VALIDATION.md` and
`doc/N64DD_DISPATCH_DIAGNOSTICS.md`. These historical paths were absent from the
checkout inspected for this plan. Locate them read-only in local history; do not
restore an experimental tree wholesale.

The supplied Phobos archive was named
`attached_assets/phobos-master_1789226371619.zip`. A local clone may not contain
it. If available, record its checksum and inspect it in a separate directory.
Relevant archive-relative paths are:

- `ares/n64/rsp/rsp.hpp`
- `ares/n64/rsp/io.cpp`
- `ares/n64/rsp/dma.cpp`
- `ares/n64/rsp/rsp.cpp`

If unavailable, obtain and pin the corresponding reference source revision
before making unresolved semantic choices. Record the exact revision used.
Do not silently substitute an unpinned modern implementation, treat a document
summary as a hardware specification, or make `/tmp`/uploaded assets a build
dependency. Any reference code copied into the project requires license review;
synthetic tests and independently described behavior are preferable.

## 3. Evidence ledger: what is and is not established

### Directly observed

The capture completed successfully with explicit DD support and Dynamic
Recompiler. Its internal RSP DMA event records:

| Field | Value |
|---|---|
| RSP entry | 1512 |
| Entry-time task type / flags | Audio, type 2 / zero |
| Initial SP memory address | `0x0fb0`, in DMEM |
| Initial RDRAM source | `0x00000000` |
| Read-length register | `0xffffffff` |
| Requested/aligned row length | 4096 bytes |
| Current plugin's effective row length | 80 bytes |
| Rows | 256 |
| Current plugin's decoded skip | 4095 bytes |
| Total copied words | 5120 |
| IMEM word writes, including repeated locations | 3052 |

Before/after samples demonstrate IMEM mutation. Later JIT compile inputs contain
those changed words. Both native stack samples lie in allocations compiled after
the transfer: records 99 and 97, with IMEM starts `0x200` and `0xf60`.
The emulation thread accumulated about 44.64 CPU seconds over about 45 seconds.

Thus the **internal RSP DMA is an observed IMEM writer** in this failure.
This is stronger than the earlier sampled-PC evidence.

### Supported by source/reference comparison

Current Parallel-RSP clamps each row at a 4 KiB boundary and then advances through
a 13-bit SP destination mask, permitting the bank to change.

The supplied Ares model latches the bank separately and advances a 12-bit offset
inside that selected bank. It retains the full decoded row length.

The captured request directly exercises this difference. It justifies a bounded
bank/row correction. It does **not** establish agreement among every emulator:
CXD4 and the current core are not identical corroborating models.

### Static reconstruction, not a new exact runtime PC observation

Compiled microcode supports this path:

1. IMEM offset `0xf4c` calls a helper at `0xa6c`.
2. That helper derives size from `(k0 >> 12) & 0xff0`, and source from
   `t9 & 0x00ffffff`.
3. The block at `0xf54` selects `at = s7`, calls the DMA helper at `0xad4`,
   and decrements the size in the call's delay slot.
4. The helper programs the DMA registers and launches the read.

This explains how an extracted zero size becomes `0xffffffff`.
It does not prove all bits of `k0` or `t9` were zero.
DDSTART11 explicitly reports the issuing site as unknown.

### Not established

- Which audio command index supplied those fields.
- Whether zero size/source is legal for that command or an earlier condition
  should have selected a different path.
- Whether the command buffer was created incorrectly, reused too early,
  corrupted after creation, fetched incorrectly, or decoded incorrectly.
- Whether a reference-correct DMA alone makes the game work.
- Whether audio has an independently audible defect after the overwrite stops.
- Exact hardware behavior for every low skip bit and visible completion register.
- Native DD gameplay/audio and cart/save acceptance after a correction.

Do not describe “the audio bug” as a proven CPU producer defect. It is currently
an **unresolved audio-command observation**.

## 4. Non-negotiable constraints

1. Keep DDSTART9's demonstrated block-boundary correction.
2. Require **explicit per-game DD activation** for every new runtime policy and
   diagnostic. WritableROM, a filename, a game title, IPL presence, or a logging
   callback is not that authorization.
3. Preserve DD-disabled behavior and independently enabled WritableROM.
4. Dynarec is the required native target. Cached interpreter is an optional
   diagnostic comparison, not an acceptable final workaround.
5. Preserve saves, writable-cart data, disk overlays, package identity and signing.
6. Do not restore watchdogs, fake task completion/yield bits, force interrupts,
   skip suspicious commands, suppress transfers from address zero, or return early
   solely because the request is `0xffffffff`.
7. Do not patch the user's ROM/IPL/disk image to hide an emulator fault.
8. Do not broaden the first correction to CPU-originated SP DMA or RSP DMA writes
   merely because those implementations look similar.
9. No invented exact writer PCs. Sampled/block PCs must remain labeled as such.
10. No unbounded tracing, arbitrary memory scans, unsafe reads, or public raw logs.
11. Host tests and a successful APK build are not native acceptance.
12. No release claim until the tested APK is the one being delivered.

## 5. Two independent tracks

### Track A: correct the demonstrated transfer defect

Target candidate invariant, to approve with the complete policy in P01:

> A corrected DD-enabled internal RSP DMA retains its initial memory bank,
> wraps its address within that bank, and executes the approved full row geometry.

A DMEM-started transfer must not dirty or modify IMEM, even if its encoded request
is surprising. An IMEM-started transfer must still write/invalidate IMEM correctly.
Fixing bank selection while retaining 80-byte clamped rows is insufficient.

### Track B: explain the command that requested the transfer

Work backwards through these boundaries:

`DMA launch operands ← decoded RSP command ← actual fetched command bytes
← task command-buffer bytes ← producer / later writer / reuse`

Stop at the first demonstrated divergence. Do not instrument all boundaries
simultaneously. Do not choose a CPU fix merely because a command contains zeros.
A valid producer plus a faulty consumer is a distinct outcome.

### Interaction

Track A is justified without knowing Track B's ultimate cause.
Track B must survive Track A's success: a corrected transfer will no longer
trigger an “actual IMEM overwrite only” observer.

The first candidate should therefore include a bounded trigger for the suspect
audio condition or the legacy crossing geometry, with explicit **actual IMEM
write count**, not just the old overwrite trigger.

## 6. Transfer-policy design and decisions

### 6.1 Separate policy from diagnostics

The current optional `DdStartupDiagnosticsSetCallback` bridge controls observation.
The core's diagnostic gate also depends on a debug callback. Do not use either
callback presence or the remaining log budget as the hardware-policy flag.

Introduce an explicit, default-off DD runtime policy state and optional plugin
capability bridge, or an equally clear existing configuration path after verifying
it really represents explicit DD activation. The API name is an implementation
choice; no new symbol is claimed to exist by this plan.

Required properties:

- Frontend selection is applied before the first RSP task.
- DD enabled + diagnostics disabled still uses corrected semantics.
- DD disabled + a debug callback present still uses legacy semantics.
- Reset/restart of the same DD session retains the selected policy.
- ROM close, failed launch, plugin detach and a later DD-off launch clear it.
- Savestate handling follows the currently authorized session policy; state
  loading cannot silently enable DD for an ordinary cart.
- Mode changes are serialized with emulation, not raced against an active DMA.
- Optional-symbol absence is detected and reported. Do not break old plugin ABI
  or claim an older plugin implements the correction.

Prove policy state through real propagation/readback or exercised transfer behavior,
not merely a frontend selection or a diagnostic message. If the selected plugin
lacks the new capability, corrected-policy acceptance is **BLOCKED**. Test plugin
attachment after selection and failed attachment as well as normal startup.

### 6.2 Complete the decision table before changing the copy loop

For every row, record: old behavior, selected new behavior, reference revision/
source location, justification, and fixture ID.

| Policy field | Required disposition |
|---|---|
| Initial bank | Latch once from bit 12 for the corrected internal path |
| Within-bank address | Wrap low 12 bits without changing selected bank |
| Row length | Keep approved aligned decoded length; remove this path's boundary clamp |
| Count | Raw count plus one; `0xff` means 256 rows |
| SP alignment | Resolve 4-byte legacy versus 8-byte reference behavior |
| Initial DRAM alignment | State the selected rule explicitly |
| Skip low bits | Resolve raw `0xfff` versus reference-aligned `0xff8` |
| DRAM bus/storage address | Distinguish hardware address width from safe backing-memory mapping |
| Final SP address | Define bank, offset, alignment and exposed register bits |
| Final DRAM address | Define trailing-skip behavior and exposed register bits |
| Length/status registers | Define what is retained/read back; do not quietly add new behavior |
| Dirty flags | Mark actual IMEM destinations only, including wrapped regions |
| Completion/return behavior | Preserve current scheduling unless separately evidenced and scoped |

**Important:** Skip alignment affects the observed request, not just a hypothetical
edge case. If choosing the supplied Ares behavior, say so and test it. If evidence
is insufficient, stop at this decision gate or document a deliberately narrower
candidate and its limits. Do not silently mix convenient portions of several
references and call the result “hardware correct.”

### 6.3 Implementation shape

Prefer one small, reviewable policy seam around the existing production transfer:

- Snapshot raw operands once.
- Derive corrected or legacy geometry without changing the unselected policy.
- Execute copies through the actual production mechanism.
- Calculate dirty state and final registers under the selected policy.
- Observe raw, effective and final state separately.

An illustrative corrected destination equation is:

`destination = latched_bank | ((within_bank_offset + byte_offset) & 0x0fff)`

This is an invariant illustration, **not a complete implementation**:
alignment, row source advance, backing-memory mapping, endian representation
and register poststate still need the decision table.

Do not introduce a second production emulator implementation just to make tests
pass. A small independent test oracle is desirable; a parallel fake tested loop
while the real loop remains untested is not.

## 7. Audio investigation design

### Evidence questions, in order

1. Does corrected DMA preserve IMEM and allow the RSP task to return?
2. Does the zero-field command still occur?
3. What command opcode/flags and full words were actually consumed?
4. Where did those bytes originate, and which command index was consumed?
5. Is that input legal for this exact microcode/ABI revision and execution path?
6. If not, at which boundary do actual bytes first diverge from intended bytes?
7. Which concrete store, DMA, reuse or decode operation caused that divergence?

### Bounded observation contract

Use a compact, versioned record schema. Suggested defaults are design limits to
confirm during implementation, not existing constants:

- 128 recent command-consumption records in a fixed ring.
- 128 recent targeted producer-write records if that stage becomes necessary.
- At most four detailed trigger snapshots per session.
- A bounded task command-buffer snapshot, such as 4096 bytes maximum, only after
  validating the declared length and memory mapping.
- Explicit wrap, overwrite, dropped, truncated, skipped and exhausted counters.

Each useful event should identify:

- Session, task entry, task type, relevant task descriptor and generation.
- A monotonic sequence in its stream; do not pretend separate counters form one
  global ordering without an explicit shared ordering mechanism.
- Event kind and exact versus sampled/unknown source-site confidence.
- Raw command words, decoded opcode/fields, relevant operands and DMA registers.
- Command source location/index **only when the fetch path proves it**.
- Before/after values and byte masks for actual writes.
- Raw versus effective DMA geometry and final register state.
- Applicable memory/IMEM generation, model version and policy state.

Capture command operands before DMA changes DMEM. An after-the-fact memory read
is not necessarily the command originally consumed.

Keep bounded recent context in memory and flush on the narrow trigger where
practical; do not emit a log line for every command or every RDRAM store.
No trigger may alter guest execution or force a task to return.

### Source-location confidence

Finding an identical pair of zero words in the task buffer does **not** prove the
command index. Use the actual command fetch cursor or verified DMA-to-DMEM mapping.
Multiple equal words, overlays, wrapped loads, scalar/vector DMEM stores and
buffer reuse make naive value matching unreliable.

A provenance map based on a DMA must be invalidated by later overlapping writers.
If those writers are not covered, label the origin as a candidate, not exact.
Prefer a narrow verified fetch hook over implementing a general shadow-memory
system as the first step.

### Exact CPU producer evidence, only if needed

After identifying an affected command range, scope writer observation to it.
Derive the range from that task; do not permanently hardcode `0x00411910`.

Cover the actual store paths in use: fast and slow dynarec writes, cached and
uncached aliases, partial/unaligned stores, successful conditional stores, and
non-CPU DMA writers when relevant.

A generic memory callback may miss fast dynarec stores. Failed conditional stores
and faulting operations must not be reported as successful writes.
An exact PC must come from the emitting instruction/compiler site, including
delay slots, with required host register preservation. Otherwise report unknown.

## 8. Hypothesis decision table

| Observation | Next action | Not justified |
|---|---|---|
| IMEM stable, game/audio recover | Complete regressions; classify zero-field command against its ABI | Inventing an audio patch to satisfy a two-bug narrative |
| IMEM stable, task still spins | Inspect consumed command/control flow and task return evidence | Reintroducing watchdog or forced completion |
| Producer bytes already contain zero fields | Identify opcode, producer intent and exact generation/store | Assuming zero is illegal |
| Producer bytes valid; consumer bytes differ | Find intervening DMA/store/reuse or wrong fetch mapping | Patching the initial producer |
| Bytes match; decoded operands differ from reference | Test RSP decode/JIT/register/delay-slot behavior | Changing game data |
| Buffer belongs to a later task generation | Investigate lifetime/publication/reuse ordering | Masking a null pointer |
| Hardware-correct handling tolerates a legal zero case | Document no separate audio defect found | Calling the guest command malformed |
| Records missing/truncated/unknown | Repair observation coverage narrowly | Treating absence as proof no writer ran |

## 9. Package sequence and decision gates

| Package | Deliverable | Depends on |
|---|---|---|
| P00 | Reproducible baseline and evidence ledger | None |
| P01 | Signed-off DMA policy/reference decision table | P00 |
| P02 | Production-path failing/regression fixtures | P01 |
| P03 | Explicit DD policy propagation/lifecycle tests | P00, P01 |
| P04 | Minimal internal-read DMA correction | P02, P03 |
| P05 | Bounded post-correction audio/transfer evidence | P04 |
| P06 | Verified APK and first native decision | P04, P05 |
| P07 | Command identity, origin and validity analysis | P06, only if unresolved |
| P08 | Targeted exact producer/reuse observation | P07, only if needed |
| P09 | Evidence-specific audio correction or no-defect finding | P07/P08 |
| P10 | Combined native regression and persistence matrix | P06, P09 if needed |
| P11 | Diagnostic/performance hardening | P10 |
| P12 | Final verified release and handoff | P10, P11 |

P02 and P03 may run in parallel if file ownership is explicit.
Do not run P04 against a moving policy interface. P07–P09 are conditional, not
permission to implement speculative fixes while waiting for a device result.

## 10. Completion definition

The overall repair is complete only when:

- The DMA policy is documented and its production path passes the edge fixtures.
- DD-only policy and diagnostics are independently gated and lifecycle-tested.
- Native dynarec DD boot, menus, gameplay and audible output are verified.
- The suspect audio command is explained sufficiently to distinguish a valid
  input, corrected consumer defect or corrected producer/reuse defect.
- Plain carts, ordinary saves and independently enabled WritableROM persist.
- The final APK, source, symbols and test record identify the same code.
- Remaining limitations are explicit; no required test is silently “assumed pass.”

A successful DMA unit test, a build, or disappearance of one log message does not
satisfy these criteria. If the device or legally supplied assets are unavailable,
report a blocked native acceptance gate rather than marking the repair complete.