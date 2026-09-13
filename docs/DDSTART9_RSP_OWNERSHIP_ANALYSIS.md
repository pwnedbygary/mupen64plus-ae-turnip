# DDSTART9 completed metadata: Parallel-RSP generated-code ownership

## Evidence integrity

Completed archive: `ddstart9-completed.aPTn3s_1789273997678.zip`.
SHA-256: `6b014da65071a0d2b57091471d58197b46f19efb00b64b8d01810c8557441ee5`.
Raw uploaded files remain local, not published.

This is the completed `capture.g1vOZS` previously retrieved while still running.
Its own final state is now `complete`, ending at 00:29:46 UTC−04:00 on
2026-09-13. Both debuggerd calls returned 0. Both map reads and all recorded
thread-metadata reads report success.

The process is PID 26570, start time 437212. The emulation-service thread is
TID 26612, start time 437249, unchanged between accounting samples. CLK_TCK
was measured as 100 rather than assumed.

Sample 1 enumerated 32 threads; sample 2 enumerated 34. The later stack dumps
contain 34 and 35 threads respectively. Enumeration and dumping are not atomic;
these differences must not be described as an identity mismatch by themselves.
Each metadata sweep took about 41–44 seconds on this device. Thus the two-second
inter-group sleep is not the interval between a given thread's counter readings.

## Mapping the sampled code

Both snapshots put `ServiceStartArg` TID 26612 in anonymous mapping
`70ae01a000`, at:

| Sample | Relative PC | Absolute PC |
|---|---|---|
| 1 | `0x7219c` | `0x70ae08c19c` |
| 2 | `0x742e8` | `0x70ae08e2e8` |

Both process-map snapshots show:

```text
70ae01a000-70ae090000 --xp
70ae090000-70ee01a000 ---p
```

The committed execute-only region is `0x76000` bytes. Together with its
immediately adjacent inaccessible remainder, the reservation is exactly
`0x40000000` bytes (1 GiB).

This matches `mupen64plus-rsp-parallel/upstream/jit_allocator.cpp`:
the ARM64 allocator reserves 1 GiB of anonymous PROT_NONE address space,
page-aligns allocations, writes generated instructions through temporarily
writable pages, and changes the committed pages to execute-only.

The CPU dynarec cache is independently visible elsewhere:

```text
7118d9d000-711ad9d000 rwxp [anon:.bss]
```

That is exactly 32 MiB. Source/symbol review matches it to ARM64 `extra_memory`,
at core BSS base `711849c000` plus `0x901000`. Neither sampled PC falls there.

The combined source and mapping evidence identifies the sampled code as
**Parallel-RSP JIT output, not R4300 CPU-dynarec output**.

## CPU accounting

For TID 26612:

| Reading | First | Second | Difference |
|---|---:|---:|---:|
| `stat` timestamp | 00:28:41 | 00:29:26 | about 45 s |
| User ticks | 14,859 | 19,319 | 4,460 = 44.60 s |
| System ticks | 71 | 71 | 0 |
| `schedstat` runtime, ns | 149,520,583,034 | 194,162,797,381 | 44,642,214,347 |

Both sampled thread states are `R`; both `wchan` values are `0`. Accounting
shows approximately one CPU's worth of execution over the interval, rather
than a thread spending that interval blocked in a mutex or graphics-driver wait.
Timestamp resolution and the non-atomic sweep do not support a precise CPU
percentage; scheduler runtime independently corroborates the tick delta.

This does not prove that every instruction throughout the interval was RSP
code, or that one RSP invocation never returned. The two stack snapshots identify
RSP code at their sample instants; task entry/return provenance is still needed.

## What is and is not established

Established:

- The enhanced root capture completed successfully.
- The sampled native addresses belong to Parallel-RSP's generated-code allocator.
- The emulation-service thread is consuming CPU, not simply sleeping throughout
  the accounting interval.

Not established:

- The exact RSP IMEM block, instruction, or guest loop represented by these PCs.
- Whether the cause is compiler output, valid microcode looping on unexpected
  input, an emulation-semantic problem, or task-state corruption.
- A context-corrupting writer or improper compiled-code reuse.
- The cause of corrupt text/texture data, or successful native DD gameplay/audio.

Do not alter DDSTART9's CPU block-boundary correction based on this result.
Static ELF symbolization cannot recover the RSP guest block from these anonymous
offsets. Existing `get_jit_block()` caching retains function pointers keyed by
IMEM PC/region hash, but current logs do not supply the host-range provenance
needed for this correlation.

## Next diagnostic

Prepare a bounded, explicit-per-game-DD-only RSP provenance observer:
host code range to IMEM PC/count/region hash, with task/IMEM change context.
Correlate its logs to a fresh native sample from the same process and build.
No generic root-permission troubleshooting or identical map-only repeat is needed.
Do not introduce a timing workaround, instruction-semantic change, or speculative
RSP correction before the relevant guest code and task state are established.

The overall task remains in progress; DD-disabled and independently enabled
WritableROM behavior, plain-cart persistence, native DD gameplay and audio
remain acceptance requirements.