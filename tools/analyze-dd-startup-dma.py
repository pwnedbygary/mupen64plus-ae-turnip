#!/usr/bin/env python3
"""Analyze a P09 DDSTART16 logcat capture.

P09 widens the existing DD load-history observer (dd_load_history.c) from DMA
writes alone to every PI-to-RDRAM transfer, and tags each record with its source
region -- ``dd_rom`` when the PI cart address fell in the DOM2 DD-ROM window or
``cart_rom`` for a real cartridge ROM. This script reads one adb logcat file
(the orchestrator's logcat-threadtime.txt) and answers P09's single question:

    Before the transition "clear" ran, did any valid resource bytes actually land
    in the staging window (the plan's working guess of ~0x51xxxxxx) or around the
    audio task buffers, and from which source?

The capture is diagnostic-only; this analyzer never touches production code. It
prints every DDSTART16 line unfiltered (so nothing is hidden by a window guess),
then a grouped summary that flags any transfer overlapping an optional --flag
window. The flag window defaults to OFF on purpose: dram_src is a guest RDRAM
address whose exact staging value must be verified against F-ZERO X before any
conclusion, and printing every line keeps the evidence visible regardless.

Usage:
    tools/analyze-dd-startup-dma.py <logcat-file> [--flag 0x518xxxx..0x52xxxxxx]
"""

from __future__ import print_function

import argparse
import re


def parse_line(line):
    """Return a dict of fields for one DDSTART16 completion line, else None."""
    if 'DDSTART16 PI DMA completion' not in line:
        return None
    fields = {}

    # Generic key=value extraction (values are whitespace-delimited tokens).
    for key, value in re.findall(r'(\w+)=(0x[0-9a-fA-F]+|\S+)', line):
        fields[key] = value

    # before/after byte samples are hex arrays: [de ad be ef ...], emitted under
    # the keys before_guest_order= / after_guest_order=. Capture whole.
    m_before = re.search(r'before_guest_order=\[([0-9a-fA-F ]+)\]', line)
    m_after = re.search(r'after_guest_order=\[([0-9a-fA-F ]+)\]', line)
    fields['before'] = _hex_array(m_before.group(1)) if m_before else None
    fields['after'] = _hex_array(m_after.group(1)) if m_after else None

    # Coerce the numeric guest addresses for window comparisons.
    for name in ('sequence', 'cart_addr', 'dram_src', 'requested_length'):
        raw = fields.get(name)
        if raw is not None:
            try:
                fields[name] = int(raw, 16 if str(raw).startswith('0x') else 10)
            except ValueError:
                return None

    region = fields.get('source_region')
    if region not in ('dd_rom', 'cart_rom'):
        return None  # Not a completed transfer record with a source tag.
    if 'dram_src' not in fields:
        return None  # Malformed completion line without a dram address.
    return fields


def _hex_array(text):
    """Turn the captured hex sample ``de ad be ef`` into [0xde, 0xad, ...]."""
    if not text:
        return []
    try:
        return [int(tok, 16) for tok in text.split()]
    except ValueError:
        return None


def _window(text):
    """Parse a CLI '0xlo..0xhi' flag window into (lo, hi) ints."""
    if not text:
        return None
    lo_str, _, hi_str = text.partition('..')
    try:
        lo = int(lo_str.strip(), 16) if str(lo_str).startswith('0x') else int(lo_str.strip())
        hi = int(hi_str.strip(), 16) if str(hi_str).startswith('0x') else int(hi_str.strip())
    except ValueError:
        raise argparse.ArgumentTypeError("bad --flag window %r (use 0xlo..0xhi)" % text)
    if hi < lo:
        raise argparse.ArgumentTypeError("--flag window reversed: %r" % text)
    return (lo, hi)


def _dram_span(rec):
    """Return the inclusive [src, dst] guest address span of a transfer."""
    length = rec.get('requested_length') or 0
    return rec['dram_src'], rec['dram_src'] + length


def window_overlap(span, window):
    lo, hi = span[0], span[1]
    win_lo, win_hi = window
    return not (hi < win_lo or lo > win_hi)


def analyze(records, flag_window):
    """Print the unfiltered table and grouped summary."""
    if not records:
        print("NO DDSTART16 completion records found in input.")
        print("(The capture predates F-ZERO X gameplay, or the DD gate is off.)")
        return

    print("=== Every DDSTART16 record (unfiltered, ordered by sequence) ===")
    for r in sorted(records, key=lambda x: x['sequence']):
        span = '0x%x..0x%x' % _dram_span(r)
        print("seq=%-4u region=%-8s dram_src=%s len=0x%-12x cart_addr=0x%08x"
              % (r['sequence'], r['source_region'], span, r.get('requested_length', 0), r.get('cart_addr', 0)))

    print("\n=== Summary grouped by source_region ===")
    by_region = {}
    for r in records:
        by_region.setdefault(r['source_region'], []).append(r)

    flagged = []
    for region, recs in sorted(by_region.items()):
        spans = [_dram_span(r) for r in recs]
        lo = min(s[0] for s in spans)
        hi = max(s[1] for s in spans)
        overlap_note = ""
        if flag_window:
            overlap = [r for r in recs if window_overlap(_dram_span(r), flag_window)]
            overlap_note = " FLAGGED-overlap=%d" % len(overlap)
            flagged.extend(overlap)
        print("region=%-8s records=%3d dram span=0x%x..0x%x%s"
              % (region, len(recs), lo, hi, overlap_note))

    if flag_window:
        print("\n=== FLAGGED transfers overlapping --flag window 0x%08x..0x%08x ===" % flag_window)
        if not flagged:
            print("NONE in the flagged window -- verify the real F-ZERO X staging")
            print("address and re-run with a corrected --flag window (every line above")
            print("is still visible, so no evidence was hidden by this guess).")
        for r in sorted(flagged, key=lambda x: x['sequence']):
            span = '0x%x..0x%x' % _dram_span(r)
            before = '' if not r.get('before') else ' before=%s' % ''.join('%02x' % b for b in r['before'][:8])
            after = '' if not r.get('after') else ' after=%s' % ''.join('%02x' % b for b in r['after'][:8])
            print("seq=%-4u region=%-8s dram_src=%s len=0x%08x%s%s"
                  % (r['sequence'], r['source_region'], span, r.get('requested_length', 0), before, after))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('logcat', help='adb logcat file (e.g. bundle/logcat-threadtime.txt)')
    parser.add_argument('--flag', type=_window, default=None, metavar='0xlo..0xhi',
                        help="optional staging/audio window to flag; defaults OFF "
                             "(every line is still printed)")
    args = parser.parse_args()

    with open(args.logcat, 'r', errors='replace') as stream:
        records = [parse_line(line) for line in stream]
    records = [r for r in records if r]
    analyze(records, args.flag)


if __name__ == '__main__':
    main()
