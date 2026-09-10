#!/usr/bin/env python3
"""Minimal RSP (N64 Reality Signal Processor) disassembler.

Usage:  rspdis.py <rdram.bin> <rdram_offset> <length> [imem_base]

<rdram.bin> is RDRAM dumped as little-endian u32 per 4 bytes, which is this
tree's convention (wd_*ram*, iplram_force.bin): file offset A read as '<I'
yields the N64 WORD VALUE at RDRAM address A (the dumps byteswap).

[imem_base] is the IMEM address the dumped bytes were loaded at; when given,
branch/jump targets are printed as both the IMEM address and the pc value the
plugin reports (imem & 0xfff).

The RSP is a MIPS-I subset plus the vector unit.  The vector (COP2) opcodes are
decoded by name only; the integer subset -- which is what all control flow and
every DMA macro in a real ucode is written in -- is decoded fully.
"""
import struct
import sys

REG = ['r0', 'at', 'v0', 'v1', 'a0', 'a1', 'a2', 'a3',
       't0', 't1', 't2', 't3', 't4', 't5', 't6', 't7',
       's0', 's1', 's2', 's3', 's4', 's5', 's6', 's7',
       't8', 't9', 'k0', 'k1', 'gp', 'sp', 's8', 'ra']

# RSP vector element specifier (bits 21..25 of a vector op / bits 11..15 of a
# vector load-store).
elsep = ['', '[1]', '[2]', '[3]', '[4]', '[5]', '[6]', '[7]',
         '[0q]', '[1q]', '[2q]', '[3q]', '[4q]', '[5q]', '[6q]', '[7q]']

CP0 = {0: 'SP_MEM_ADDR', 1: 'SP_DRAM_ADDR', 2: 'SP_RD_LEN', 3: 'SP_WR_LEN',
       4: 'SP_STATUS', 5: 'SP_DMA_FULL', 6: 'SP_DMA_BUSY', 7: 'SP_SEMAPHORE',
       8: 'DPC_START', 9: 'DPC_END', 10: 'DPC_CURRENT', 11: 'DPC_STATUS',
       12: 'DPC_CLOCK', 13: 'DPC_BUSY', 14: 'DPC_PIPE_BUSY', 15: 'DPC_TMEM'}

# MIPS-I SPECIAL funct map.  NOTE: the arithmetic/bitwise block is 0x20..0x2B
# (add=0x20 addu=0x21 sub=0x22 subu=0x23 and=0x24 or=0x25 xor=0x26 nor=0x27
# slt=0x2A sltu=0x2B).  0x1C..0x1F are NOT add/addu/sub/subu -- an earlier
# revision of this table had them shifted by 4, which printed every `add` as
# `and` and every `and` as `?`.  Fixed in round 29; re-check any conclusion
# drawn from this tool before then.
SPECIAL = ['sll', '?', 'srl', 'sra', 'sllv', '?', 'srlv', 'srav',
           'jr', 'jalr', '?', '?', 'syscall', 'break', '?', 'sync',
           'mfhi', 'mthi', 'mflo', 'mtlo', '?', '?', '?', '?',
           'mult', 'multu', 'div', 'divu', '?', '?', '?', '?',
           'add', 'addu', 'sub', 'subu', 'and', 'or', 'xor', 'nor',
           '?', '?', 'slt', 'sltu', '?', '?', '?', '?',
           '?', '?', '?', '?', '?', '?', '?', '?']

REGIMM = {0: 'bltz', 1: 'bgez', 2: 'bltzl', 3: 'bgezl',
          16: 'bltzal', 17: 'bgezal', 18: 'bltzall', 19: 'bgezall'}


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


def decode(w, addr):
    """Return (text, is_branch, target_addr_or_None, is_jr_ra)."""
    op = (w >> 26) & 0x3f
    rs = (w >> 21) & 0x1f
    rt = (w >> 16) & 0x1f
    rd = (w >> 11) & 0x1f
    sa = (w >> 6) & 0x1f
    fn = w & 0x3f
    imm = w & 0xffff
    simm = s16(imm)

    if w == 0:
        return 'nop', False, None, False

    if op == 0:                                     # SPECIAL
        n = SPECIAL[fn]
        if n == 'sll' and w == 0:
            return 'nop', False, None, False
        if n in ('sll', 'srl', 'sra'):
            if rs != 0:
                return '%s.%s %s,%s,%u' % (n, 'r' if rs == 1 else str(rs),
                                           REG[rd], REG[rt], sa), False, None, False
            return '%s %s,%s,%u' % (n, REG[rd], REG[rt], sa), False, None, False
        if n in ('sllv', 'srlv', 'srav'):
            return '%s %s,%s,%s' % (n, REG[rd], REG[rt], REG[rs]), False, None, False
        if n == 'jr':
            return 'jr %s' % REG[rs], True, None, (rs == 31)
        if n == 'jalr':
            return 'jalr %s,%s' % (REG[rd], REG[rs]), True, None, False
        if n in ('mult', 'multu', 'div', 'divu'):
            return '%s %s,%s' % (n, REG[rs], REG[rt]), False, None, False
        if n in ('add', 'addu', 'sub', 'subu', 'and', 'or', 'xor', 'nor',
                 'slt', 'sltu'):
            return '%s %s,%s,%s' % (n, REG[rd], REG[rs], REG[rt]), False, None, False
        if n in ('mfhi', 'mflo'):
            return '%s %s' % (n, REG[rd]), False, None, False
        if n in ('mthi', 'mtlo'):
            return '%s %s' % (n, REG[rs]), False, None, False
        if n == 'break':
            return 'break', False, None, False
        return '.word %08x  (special %u)' % (w, fn), False, None, False

    if op == 1:                                     # REGIMM
        n = REGIMM.get(rt)
        if n:
            return '%s %s,%04x' % (n, REG[rs], imm), True, addr + 4 + (simm << 2), False
        return '.word %08x' % w, False, None, False

    if op == 2:
        return 'j %04x' % ((addr & 0xf0000000) | ((w & 0x3ffffff) << 2)), True, \
               (addr & 0xf0000000) | ((w & 0x3ffffff) << 2), False
    if op == 3:
        return 'jal %04x' % ((addr & 0xf0000000) | ((w & 0x3ffffff) << 2)), True, \
               (addr & 0xf0000000) | ((w & 0x3ffffff) << 2), False

    if op == 4:
        return 'beq %s,%s,%04x' % (REG[rs], REG[rt], imm), True, addr + 4 + (simm << 2), False
    if op == 5:
        return 'bne %s,%s,%04x' % (REG[rs], REG[rt], imm), True, addr + 4 + (simm << 2), False
    if op == 6:
        return 'blez %s,%04x' % (REG[rs], imm), True, addr + 4 + (simm << 2), False
    if op == 7:
        return 'bgtz %s,%04x' % (REG[rs], imm), True, addr + 4 + (simm << 2), False

    if op == 8:
        return 'addi %s,%s,%d' % (REG[rt], REG[rs], simm), False, None, False
    if op == 9:
        return 'addiu %s,%s,%d' % (REG[rt], REG[rs], simm), False, None, False
    if op == 10:
        return 'slti %s,%s,%d' % (REG[rt], REG[rs], simm), False, None, False
    if op == 11:
        return 'sltiu %s,%s,%d' % (REG[rt], REG[rs], simm), False, None, False
    if op == 12:
        return 'andi %s,%s,%04x' % (REG[rt], REG[rs], imm), False, None, False
    if op == 13:
        return 'ori %s,%s,%04x' % (REG[rt], REG[rs], imm), False, None, False
    if op == 14:
        return 'xori %s,%s,%04x' % (REG[rt], REG[rs], imm), False, None, False
    if op == 15:
        return 'lui %s,%04x' % (REG[rt], imm), False, None, False

    if op == 16:                                    # COP0
        cf = (w >> 21) & 0x1f
        c0 = CP0.get(rd, 'cp0[%u]' % rd)
        if cf == 0:
            return 'mfc0 %s,%s(%u)' % (REG[rt], c0, rd), False, None, False
        if cf == 4:
            return 'mtc0 %s,%s(%u)' % (REG[rt], c0, rd), False, None, False
        return '.word %08x  (cop0 %u)' % (w, cf), False, None, False

    if op == 18:                                    # COP2 (vector unit)
        cf = (w >> 21) & 0x1f
        vn = ['vmulf', 'vmulu', 'vrndp', 'vmulq', 'vmudl', 'vmudm', 'vmudn',
              'vmudh', 'vmacf', 'vmacu', 'vrndn', 'vmacq', 'vmadl', 'vmadm',
              'vmadn', 'vmadh', 'vadd', 'vsub', 'vsut', 'vabs', 'vaddc',
              'vsubc', 'vaddb', 'vsubb', 'vaccb', 'vsud', 'vsum', 'vsaw',
              'vlt', 'veq', 'vne', 'vge', 'vcl', 'vch', 'vcr', 'vmrg',
              'vand', 'vnand', 'vor', 'vnor', 'vxor', 'vnxor', 'vrcp',
              'vrcpl', 'vrcph', 'vmov', 'vrsq', 'vrsql', 'vrsqh', 'vnop',
              'vextt', 'vextq', 'vextn', 'vins', 'vinsq', 'vinsn', 'vnull']
        if cf < 0x10:
            fn2 = w & 0x3f
            nm = vn[fn2] if fn2 < len(vn) else 'v?%u' % fn2
            return 'cop2 %-7s v%02u,%s,v%02u[%u]' % (
                nm, (w >> 6) & 0x1f, elsep[(w >> 21) & 0xf],
                (w >> 16) & 0x1f, (w >> 11) & 7), False, None, False
        if cf == 0x10:
            return 'mfc2 %s,v%02u[%u]' % (REG[rt], rd, sa & 7), False, None, False
        if cf == 0x14:
            return 'mtc2 %s,v%02u[%u]' % (REG[rt], rd, sa & 7), False, None, False
        if cf == 0x12:
            return 'cfc2 %s,vco' % REG[rt], False, None, False
        if cf == 0x16:
            return 'ctc2 %s,vco' % REG[rt], False, None, False
        return '.word %08x  (cop2 cf) ' % w, False, None, False

    if op == 50:                                    # LWC2
        return 'lwc2 %s,%d(%s)' % (elsep[(w >> 11) & 0xf], s16(w & 0xffff),
                                   REG[(w >> 21) & 0x1f]), False, None, False
    if op == 58:                                    # SWC2
        return 'swc2 %s,%d(%s)' % (elsep[(w >> 11) & 0xf], s16(w & 0xffff),
                                   REG[(w >> 21) & 0x1f]), False, None, False

    if op == 32:
        return 'lb %s,%d(%s)' % (REG[rt], simm, REG[rs]), False, None, False
    if op == 33:
        return 'lh %s,%d(%s)' % (REG[rt], simm, REG[rs]), False, None, False
    if op == 34:
        return 'lwl %s,%d(%s)' % (REG[rt], simm, REG[rs]), False, None, False
    if op == 35:
        return 'lw %s,%d(%s)' % (REG[rt], simm, REG[rs]), False, None, False
    if op == 36:
        return 'lbu %s,%d(%s)' % (REG[rt], simm, REG[rs]), False, None, False
    if op == 37:
        return 'lhu %s,%d(%s)' % (REG[rt], simm, REG[rs]), False, None, False
    if op == 38:
        return 'lwr %s,%d(%s)' % (REG[rt], simm, REG[rs]), False, None, False
    if op == 40:
        return 'sb %s,%d(%s)' % (REG[rt], simm, REG[rs]), False, None, False
    if op == 41:
        return 'sh %s,%d(%s)' % (REG[rt], simm, REG[rs]), False, None, False
    if op == 42:
        return 'swl %s,%d(%s)' % (REG[rt], simm, REG[rs]), False, None, False
    if op == 43:
        return 'sw %s,%d(%s)' % (REG[rt], simm, REG[rs]), False, None, False
    if op == 46:
        return 'swr %s,%d(%s)' % (REG[rt], simm, REG[rs]), False, None, False
    if op == 47:
        return 'cache %d,%d(%s)' % (rt, simm, REG[rs]), False, None, False

    return '.word %08x' % w, False, None, False


def main():
    path, off, ln = sys.argv[1], int(sys.argv[2], 0), int(sys.argv[3], 0)
    base = int(sys.argv[4], 0) if len(sys.argv) > 4 else None
    data = open(path, 'rb').read()
    for i in range(0, ln, 4):
        a = off + i
        if a + 4 > len(data):
            break
        w = struct.unpack_from('<I', data, a)[0]
        imem = None if base is None else base + i
        text, br, tgt, jrra = decode(w, imem if imem is not None else a)
        if imem is not None:
            t = ''
            if tgt is not None:
                t = ' -> imem %04x (pc %03x)%s' % (tgt, tgt & 0xfff,
                                                   '   [RETURN]' if jrra else '')
            elif jrra:
                t = '   [RETURN]'
            print('%06x  %04x  %08x  %-40s%s' % (a, imem, w, text, t))
        else:
            print('%06x  %08x  %s' % (a, w, text))


if __name__ == '__main__':
    main()
