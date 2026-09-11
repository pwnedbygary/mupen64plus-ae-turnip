# N64DD web reference index

The user requested that the handoff's web resources remain part of the
investigation. `HANDOFF.md` names several resources without full HTTPS links;
the explicit DD wiki link list is in `PLAN.md`, with additional links in
`HANDOFF-2026-08-29.md`. This index consolidates both, rather than treating
an absent URL scheme as an absent reference. Links are references, not evidence
that the current build boots successfully. Live page contents were not
revalidated during this indexing pass.

## DD hardware, boot and disk documentation

- [LuigiBlood 64DD wiki](https://github.com/LuigiBlood/64dd/wiki)
- [Registers](https://github.com/LuigiBlood/64dd/wiki/Registers): ASIC status,
  masks and acknowledgements.
- [Commands](https://github.com/LuigiBlood/64dd/wiki/Commands): mechanical commands.
- [Memory Map](https://github.com/LuigiBlood/64dd/wiki/Memory-Map)
- [Micro Sequencer](https://github.com/LuigiBlood/64dd/wiki/Micro-Sequencer)
- [Disk Access Process](https://github.com/LuigiBlood/64dd/wiki/Disk-Access-Process)
- [64DD IPL](https://github.com/LuigiBlood/64dd/wiki/64DD-IPL): boot contract.
- [F-Zero X](https://github.com/LuigiBlood/64dd/wiki/F-Zero-X): expansion-kit
  rebooter and loaded-code lifecycle; named in HANDOFF's newest sections.
- [Games with libleo](https://github.com/LuigiBlood/64dd/wiki/Games-with-libleo)
- [Emulation Info](https://github.com/LuigiBlood/64dd/wiki/Emulation-Info):
  named in HANDOFF's libleo presence-check discussion.
- [Disk Image Formats](https://github.com/LuigiBlood/64dd/wiki/Disk-Image-Formats):
  logical versus physical format, explicitly linked in the older handoff.
- [64DD hardware schematics](https://github.com/ChrisPVille/64dd-schematics)
- [64dd.org](https://64dd.org/): general reference site.

## Game, graphics and emulator source references

- [Mr-Wiseguy/f3dex2](https://github.com/Mr-Wiseguy/f3dex2): named in HANDOFF
  for documented microcode disassembly; check actual license before reuse.
- [G-Diffuser](https://github.com/Zorkats/G-Diffuser): older handoff's F-Zero X
  PC-port reference.
- [Project repository](https://github.com/pwnedbygary/mupen64plus-ae-turnip)
  and [upstream](https://github.com/fzurita/mupen64plus-ae): named remotes, not
  evidence of the uploaded checkpoint's root commit.
- Ares/Phobos: use the supplied `phobos-master_1789161093912.zip`, not the
  unavailable `/home/garyb/LLM-Projects/phobos/ares/n64` path cited at the end
  of HANDOFF. See `N64DD_REFERENCE_REVIEW.md`. No web repository is inferred
  from that local path.
- HANDOFF also names parallel-n64 as a behavioral comparison, without a
  repository URL or pinned reference revision.

## How to apply these references

Prioritize the F-Zero X reboot/thread lifecycle and CPU/RSP context semantics
for the current post-load stall. Consult controller/disk documentation when
fresh traces implicate it; the existence of useful references is not a reason
to repeat the rejected stale-DD-interrupt or disk-layout experiments.
Historical assertions that another emulator ran a particular disk are not a
newly reproduced result or proof about the exact supplied reference snapshot.