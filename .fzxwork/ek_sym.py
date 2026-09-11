#!/usr/bin/env python3
"""Resolve guest addresses against the F-Zero X Expansion Kit (jp/ek) symbols."""
import re, sys, os, bisect
D = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'fzerox-decomp/linker_scripts/jp/ek')
SYMS = {}
for fn in ('symbol_addrs.txt', 'symbol_addrs_nlib_vars.txt', 'symbol_addrs_overlays.txt'):
    p = os.path.join(D, fn)
    if not os.path.exists(p): continue
    for ln in open(p, encoding='utf-8', errors='ignore'):
        m = re.match(r'\s*([A-Za-z_][\w.]*)\s*=\s*(0x[0-9A-Fa-f]+)\s*;', ln)
        if m:
            a = int(m.group(2), 16)
            SYMS.setdefault(a, m.group(1))
ADDRS = sorted(SYMS)
def near(a):
    i = bisect.bisect_right(ADDRS, a) - 1
    if i < 0: return ('?', 0, 0)
    base = ADDRS[i]
    return (SYMS[base], base, a - base)
def s(a):
    n, b, d = near(a)
    return "%s+0x%x" % (n, d) if d else n
if __name__ == '__main__':
    for arg in sys.argv[1:]:
        a = int(arg, 16)
        n, b, d = near(a)
        print("%08x  %-46s (+0x%x in %s @%08x)" % (a, n, d, n, b))
