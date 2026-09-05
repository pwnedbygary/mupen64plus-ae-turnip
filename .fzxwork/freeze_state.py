#!/usr/bin/env python3
"""Decode the frozen guest OS state from an iplram_wd*.bin watchdog dump.

The dump = 8MB raw RDRAM (host byte order; LE u32 read = guest-visible word,
i.e. instruction words read LE, u16s at guest offsets need >>16) followed by
the "WD pc=..." header and the DD trace ring.

F-Zero X EK kernel globals (verified 2026-08-30, freeze at guest 0x80746AFC):
  __osThreadTail sentinel @0x80771E10 {NULL,-1}
  __osRunQueue          @0x80771E18 (OSThread*)
  __osRunningThread     @0x80771E20 (OSThread*)
  __OSGlobalIntMask-ish @0x80771E60 (MI mask in upper 16 bits)
  event table           @0x807C3390 {OSMesgQueue*, OSMesg} per OS_EVENT_*
                         (a0 = event id * 8; 0x20=SP 0x18=COUNTER 0x10=CART
                          0x58=DP 0x38=VI 0x30=AI 0x40=PI 0x28=SI)
OSThread (stock libultra layout, state u16 at +0x10, ctx at +0x20,
ctx.sp=+0xD0 ctx.ra=+0xE0 ctx.sr=+0xF8 ctx.pc=+0xFC):
  next(0) prio(4) queue(8) tlnext(0xC) state(0x10) flags(0x12) id(0x14)
  fp(0x18) thprof(0x1C) context(0x20)
OSMesgQueue: mtqueue(0) fullqueue(4) validCount(8) first(0xC) msgCount(0x10) msg(0x14)
"""
import struct
import sys

def load(path):
    data = open(path, 'rb').read()
    return data[:0x800000], data[0x800000:]

def rd(ram, a):
    return struct.unpack('<I', ram[a - 0x80000000 : a - 0x80000000 + 4])[0]

def state_of(ram, t):
    """guest u16 at t+0x10 = (word >> 16)"""
    return (rd(ram, t + 0x10) >> 16) & 0xFFFF

def thread(ram, t):
    if t == 0 or t == 0xFFFFFFFF:
        return None
    return dict(addr=t, next=rd(ram, t), prio=rd(ram, t + 4) & 0xFF,
                queue=rd(ram, t + 8), tlnext=rd(ram, t + 0xC),
                state=state_of(ram, t), flags=rd(ram, t + 0x10) & 0xFFFF,
                id=rd(ram, t + 0x14) & 0xFF, fp=rd(ram, t + 0x18),
                sp=rd(ram, t + 0x20 + 0xD0), ra=rd(ram, t + 0x20 + 0xE0),
                sr=rd(ram, t + 0x20 + 0xF8), pc=rd(ram, t + 0x20 + 0xFC))

def mq(ram, a):
    return dict(addr=a, mtqueue=rd(ram, a), fullqueue=rd(ram, a + 4),
                valid=rd(ram, a + 8), first=rd(ram, a + 0xC),
                msgcount=rd(ram, a + 0x10), msg=rd(ram, a + 0x14))

def main():
    path = sys.argv[1] if len(sys.argv) > 1 else '.fzxwork/iplram_wd_loader.bin'
    ram, tail = load(path)
    i = tail.find(b'WD pc=')
    print(tail[i:i + 160].decode('latin1', errors='replace'))
    if i >= 0:
        for line in tail[i:i + 4096].split(b'\n'):
            if line.startswith(b'--- DD regs'):
                print(line.decode()); break
        ddregs = tail[tail.find(b'--- DD regs'):][:520].decode('latin1', errors='replace')
        print(ddregs[:500])

    runq = rd(ram, 0x80771E18)
    running = rd(ram, 0x80771E20)
    print(f"__osRunQueue={runq:#x} __osRunningThread={running:#x}")
    seen = set()
    print("== run queue chain ==")
    a = runq
    while a and a not in seen and a != 0xFFFFFFFF and a != 0x80771E10:
        seen.add(a)
        t = thread(ram, a)
        if t is None: break
        print(f"  @{a:#x} next={t['next']:#x} prio={t['prio']} state={t['state']:#x} "
              f"id={t['id']} ctx.pc={t['pc']:#x} ctx.sp={t['sp']:#x}")
        a = t['next']
    print("== all threads (tlnext from tail) ==")
    seen2 = set()
    a = 0x80771E10
    while a and a not in seen2:
        seen2.add(a)
        t = thread(ram, a)
        if t is None: break
        print(f"  @{a:#x} prio={t['prio']} queue={t['queue']:#x} tlnext={t['tlnext']:#x} "
              f"state={t['state']:#x} id={t['id']} fp={t['fp']} "
              f"ctx.pc={t['pc']:#x} ctx.sp={t['sp']:#x} ctx.ra={t['ra']:#x} ctx.sr={t['sr']:#x}")
        a = t['tlnext']
    print("== event table (mq, msg) ==")
    evnames = {0x10: 'CART', 0x18: 'COUNTER', 0x20: 'SP', 0x28: 'SI', 0x30: 'AI',
               0x38: 'VI', 0x40: 'PI', 0x48: 'DP', 0x50: 'PRENMI', 0x58: 'PRENMI?',
               0x60: '?', 0x70: '?'}
    for ev in range(16):
        base = 0x807C3390 + ev * 8
        m, msg = rd(ram, base), rd(ram, base + 4)
        if m or msg:
            q = mq(ram, m) if 0x80000000 <= m < 0x80800000 else None
            extra = ''
            if q:
                extra = f" valid={q['valid']}/{q['msgcount']} mtqueue={q['mtqueue']:#x} fullqueue={q['fullqueue']:#x}"
            print(f"  ev{ev} (a0={ev*8:#x} {evnames.get(ev*8,'?')}): mq={m:#x} msg={msg:#x}{extra}")
            if q and q['mtqueue'] not in (0, 0xFFFFFFFF, 0x80771E10):
                bt = thread(ram, q['mtqueue'])
                if bt:
                    print(f"      blocked thread @{q['mtqueue']:#x} prio={bt['prio']} id={bt['id']} "
                          f"state={bt['state']:#x} ctx.pc={bt['pc']:#x}")

if __name__ == '__main__':
    main()
