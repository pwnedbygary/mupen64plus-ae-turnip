# DDSTART9: first successful root native-stack captures

## Evidence and collection outcome

Source archive: `ddstart9-diagnostics.n9O9bY_1789271885567.zip`.
SHA-256: `8b7b9b36a8bc92fabbb7fd82d310a7185c00f1b55cd31dd4fd1cc2ae34cc7e75`.
Raw uploaded diagnostics remain local and are not published.

Both `capture.Q1XE7S` and `capture.DxiS50` completed successfully:

- Root UID 0; exactly one matching emulation process.
- PID 14559, process start-time tick 32478, verified before both samples in
  each attempt. Both attempts sampled the same process.
- Four `debuggerd -b` calls, all exit status 0.
- Each snapshot contains the same 31 thread IDs.
- Both status files and the latest completion marker report `complete`.
- Each stack file is 117,900 bytes. These are real backtraces, not the earlier
  root-permission errors.

The one-line menu entry successfully reached the helper and background worker.
The user's Terminal wait/download problem did not prevent capture. Its precise
cause is not established by this archive; do not label the native worker broken.
The earlier inference about line-oriented vendor execution remains an inference.

## Emulation-service thread

TID 14594 (`ServiceStartArg`) has one unwindable frame per sample:

| Capture / sample | Actual debuggerd timestamp (2026-09-12, UTC−04:00) | Anonymous-map relative PC |
|---|---|---|
| Q1XE7S / 1 | 23:53:39.493879500 | `0x72060` |
| Q1XE7S / 2 | 23:53:42.242040489 | `0x746dc` |
| DxiS50 / 1 | 23:54:25.194299848 | `0x72388` |
| DxiS50 / 2 | 23:54:27.988622138 | `0x73220` |

All four frames identify `<anonymous:70ab977000>`, across approximately
48.5 seconds. The PCs differ: this is not four observations of one fixed native
wait instruction. It does not measure continuous CPU use or prove guest progress.

Source review identifies the primary service handler thread through
`CoreService`'s `HandlerThread("ServiceStartArguments")`, synchronous `emuStart()`,
`CoreDoCommand(M64CMD_EXECUTE)`, `main_run()`, `run_device()`, and `run_r4300()`.
The other similarly named native threads inherit the parent's name; their
stacks identify them as video workers.

Anonymous executable code on the emulation-service thread is consistent with
generated-code execution. It is **not enough to assign CPU versus RSP ownership**:
Parallel-RSP also executes synchronously on this thread via `do_SP_Task()`.
Missing caller frames are compatible with generated code lacking unwind metadata,
not proof of a damaged native stack.

## Video and audio observations

Video-Parallel build ID in the capture:
`ec46070d8a937f09dba31840afcae98129b8d615`.
It matches the preserved DDSTART9 unstripped library, verified with
`llvm-readelf`. `llvm-addr2line` resolves:

- TID 14618, video PC `0x3c52a4`: fence/coherency worker's queue condition wait
  in `parallel-rdp/worker_thread.hpp:107`.
- TID 14619, video PC `0x3d4700`: deferred-pipeline worker's queue condition wait
  at the same source line.
- TID 14620, video PC `0x3bf5bc`: command-ring timed condition wait in
  `parallel-rdp/command_ring.cpp:106`.

Those workers are waiting for work at the sampled instants. Their waits are not
evidence of a renderer deadlock. A Turnip trace worker also waits on a condition.
The AudioTrack thread is in its Android condition wait, which does not identify
the cause of the reported music stopping. These observations do not establish
overall renderer correctness or explain the corrupted text.

No static core/RSP caller frame identifies the anonymous region. Do not feed its
relative offsets into a static core or RSP ELF and claim a guest instruction,
loop, exact writer, or stale-code reuse has thereby been identified.

## Minimal next evidence

Keep DDSTART9 and its block-boundary correction unchanged. Do not repeat an
identical stack-only capture or change rendering/timing settings.

1. If the same frozen process remains alive, validate its exact command line
   and start time, then collect `/proc/PID/maps`.
2. Collect the emulation thread's `stat`, `schedstat`, `status`, and `wchan`
   twice about two seconds apart. Compare CPU accounting deltas; report denied
   or unavailable fields explicitly.
3. Use map permissions, extent, and surrounding mappings to distinguish the
   source-described allocations: ARM64 CPU dynarec's 32 MiB RWX cache versus
   Parallel-RSP's executable pages in a 1 GiB reservation. A map layout is
   supporting evidence; if ambiguous, obtain an explicit allocator/range identity.
4. Only then choose an owner-specific host-range-to-guest-code observer, if needed.

If the process was restarted, do not correlate its new mappings with these old
addresses. Capture mappings and stacks together for the new process instead.
The proposed metadata collection is not a process-memory dump.

The archive does not log guest CPU-engine settings, cart/disk/IPL identity, or
the precise visible game phase. Do not infer those afresh from these stacks.
Dynarec was explicit in the earlier DDSTART9 logcat evidence; this capture
narrows the next diagnostic but does not finish the native DD investigation.
Gameplay/audio/text and plain-cart/WritableROM persistence acceptance remain open.