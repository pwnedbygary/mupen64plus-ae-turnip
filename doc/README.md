# Project documentation

Project-owned docs live here; `README.md` stays in the repository root for GitHub.
Vendored trees (`mupen64plus-core/upstream`, `ndkLibs/`, plugin `upstream/` dirs, …)
keep their own documentation — do not move those.

| file | what it is |
|---|---|
| [HANDOFF.md](HANDOFF.md) | **Ground truth.** Living campaign log for the 64DD / F-Zero X EK boot work: what was tested and measured each round on the RP6, newest round at the top. Read its newest section before touching DD code. |
| [PLAN.md](PLAN.md) | Current actionable plan (rounds 65+): objective, verified ground truth, standing rules, next rounds, fallback port, traps, operational recipes. |
| [TODO.md](TODO.md) | Older task list. |
| [EK_SP_FORENSICS_REPORT.md](EK_SP_FORENSICS_REPORT.md) | Read-only forensics of the Expansion Kit boot freeze (`$sp = 0x800d4203`). |
| [HANDOFF-2026-08-29.md](HANDOFF-2026-08-29.md) | Archived session handoff snapshot from 2026-08-29 (first successful boot through the 64DD disk mount). |

Scratch work (dumps, traces, run scripts, analysis tools) lives in `.fzxwork/`, which is
gitignored except for the two durable tools `ram_tools.py` and `ek_sym.py`.
