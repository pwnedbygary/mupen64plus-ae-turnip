#!/usr/bin/env python3
"""Offline tools for the F-Zero X (64DD) stall dumps.

The emulator's full-RDRAM dumps (`iplram_force.bin`, `iplram_fault.bin`, ...) are
`g_mem_base` written raw, and **mupen64plus-ae keeps RDRAM byte-swapped inside each
32-bit word**.  So the guest's word at 0x80xxxxxx is `bswap32(file[off])` -- if you
read a dump straight, every ASCII string and every instruction looks like noise.
This module does that conversion once and gives you the questions the campaign
actually asks of a dump:

    ./ram_tools.py dis 0x80750384 24     disassemble guest code (capstone, mips32 BE)
    ./ram_tools.py rd 0x8074fee0 8       hexdump guest words
    ./ram_tools.py sym 0x80750384        symbolize via the vendored F-Zero X decomp
    ./ram_tools.py find 340a0fc0         which guest addresses hold this word?
    ./ram_tools.py str F-ZERO            find an ASCII string, report guest address
    ./ram_tools.py ucode 0x058 16        disassemble the audio ucode loaded at RDRAM 0x768e60
    ./ram_tools.py wrap                  verify the byte-swap on this dump

Dump path defaults to .fzxwork/r63/ram_force.bin (override with RAM_DUMP=...).
Symbols come from .fzxwork/ek_sym.py (fzerox-decomp symbol_addrs*.txt).

Physical addresses below 0x80000000 are also accepted (the ucode lives at RDRAM
0x768e60), because the dump is a flat image of RDRAM starting at offset 0.
"""
import os
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
DEFAULT_DUMP = os.path.join(ROOT, 'r63', 'ram_force.bin')
DUMP = os.environ.get('RAM_DUMP', DEFAULT_DUMP)
RDRAM_SIZE = 0x800000
UCODE_RDRAM = 0x768e60          # where osSpTaskLoad DMA'd the EK audio ucode from


def load(path=DUMP):
    """Return (guest_view, raw) 8 MB images.  guest_view[off] is the guest's byte."""
    raw = open(path, 'rb').read()[:RDRAM_SIZE]
    if len(raw) != RDRAM_SIZE:
        raise SystemExit("%s: short dump (%d bytes, want %d)" % (path, len(raw), RDRAM_SIZE))
    return bytes(raw[i ^ 3] for i in range(len(raw))), raw


GUEST = None


def off(addr):
    """Guest address (KSEG0 0x80xxxxxx or a physical RDRAM offset) -> file offset."""
    if addr >= 0x80000000:
        addr -= 0x80000000
    if not 0 <= addr < RDRAM_SIZE:
        raise SystemExit("address out of RDRAM: 0x%x" % addr)
    return addr


def w32(addr):
    o = off(addr & 0xFFFFFFFF)
    return int.from_bytes(GUEST[o:o + 4], 'big')


def mips():
    try:
        from capstone import Cs, CS_ARCH_MIPS, CS_MODE_MIPS32, CS_MODE_BIG_ENDIAN
    except ImportError:
        raise SystemExit("pip install capstone")
    return Cs(CS_ARCH_MIPS, CS_MODE_MIPS32 + CS_MODE_BIG_ENDIAN)


def dis(addr, n=16):
    md = mips()
    o = off(addr)
    for ins in md.disasm(GUEST[o:o + n * 4], addr):
        w = w32(ins.address)
        print("%08x  %08x  %-8s %s" % (ins.address, w, ins.mnemonic, ins.op_str))


def rd(addr, n=8):
    for k in range(n):
        a = addr + 4 * k
        if k % 4 == 0:
            sys.stdout.write("\n%08x:" % a)
        sys.stdout.write(" %08x" % w32(a))
    print()


def symbolize(addr):
    sys.path.insert(0, ROOT)
    try:
        import ek_sym
    except ImportError:
        print("(no .fzxwork/ek_sym.py -- cannot symbolize)")
        return
    name, base, delta = ek_sym.near(addr)
    print("%08x  %s+0x%x  (%s @ %08x)" % (addr, name, delta, name, base))


def find(value):
    hits = [a + 0x80000000 for a in range(0, RDRAM_SIZE, 4) if w32(a) == value]
    print("%d hit(s) for %08x:" % (len(hits), value))
    for a in hits[:24]:
        print("  %08x" % a)
    if len(hits) > 24:
        print("  ... %d more" % (len(hits) - 24))
    for a in hits[:8]:
        symbolize(a)


def find_str(text):
    needle = text.encode()
    hits = []
    i = GUEST.find(needle)
    while i >= 0 and len(hits) < 12:
        hits.append(i + 0x80000000)
        i = GUEST.find(needle, i + 1)
    print("%d hit(s) for %r: %s" % (len(hits), text, ' '.join('%08x' % h for h in hits)))


def ucode(pc=0, n=16):
    addr = UCODE_RDRAM + pc
    if w32(UCODE_RDRAM) == 0:
        raise SystemExit("no ucode at RDRAM %08x in this dump (IMEM already wiped?)" % UCODE_RDRAM)
    print("audio ucode: RDRAM %08x, IMEM PC %03x  (entry word = %08x)"
          % (UCODE_RDRAM, pc, w32(UCODE_RDRAM)))
    dis(addr, n)


def wrap_check():
    for s in ('F-ZERO', 'NINTENDO', 'libultra'):
        print("%-10s guest-view hit: %s" %
              (s, 'yes' if GUEST.find(s.encode()) >= 0 else 'no'))


def main(argv):
    global GUEST
    if not os.path.exists(DUMP):
        raise SystemExit("dump not found: %s (set RAM_DUMP=...)" % DUMP)
    if not argv:
        print(__doc__)
        return 1
    GUEST, _ = load()
    cmd, rest = argv[0], argv[1:]
    num = lambda s, d: int(s, 0) if s else d
    if cmd == 'dis':
        dis(num(rest[0], 0x80000400) if rest else 0x80000400, num(rest[1] if len(rest) > 1 else None, 16))
    elif cmd == 'rd':
        rd(num(rest[0], 0x80000000) if rest else 0x80000000, num(rest[1] if len(rest) > 1 else None, 8))
    elif cmd == 'sym':
        for a in rest or ['80750384']:
            symbolize(int(a, 16))
    elif cmd == 'find':
        find(int(rest[0], 16))
    elif cmd == 'str':
        find_str(rest[0])
    elif cmd == 'ucode':
        ucode(num(rest[0] if rest else None, 0), num(rest[1] if len(rest) > 1 else None, 16))
    elif cmd == 'wrap':
        wrap_check()
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
