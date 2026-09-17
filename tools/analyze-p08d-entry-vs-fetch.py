#!/usr/bin/env python3
"""Correlate P08c-writer DDSTART12 entry hashes with the P08b fetch records.

Purpose (the pre-registered decision rule recorded in
docs/P08_CHECKPOINT.md, "P08c-writer checkpoint (part 2)"):
  * an entry whose command buffer is ALL ZERO at submission means the buffer
    was already empty when the guest submitted the task -> the guest's build
    path is the subject;
  * an entry whose buffer is NON-ZERO at submission, followed by a fetch of
    the same buffer that reads zeros, means the clearing happened between
    submission and the fetch -> the RSP/plugin write paths are the subject.
    This tool counts that branch only when EVERY fetch attributed to the entry
    reads zeros (the stricter reading of "the fetch reads zeros"); the summary
    prints the count, and a looser any-fetch reading can be checked by hand
    from the per-entry payload list.

How the correlation is made, without a shared counter: the DDSTART12 line
names the buffer (data_ptr/data_size) and the P08b fetch records name the
address they read (raw_dma_dram) plus the offset inside the buffer
(fetch_buffer_offset), so a fetch belongs to the most recent entry whose
range contains it.  The DDSTART12 record carries no generation at all, and
the plugin's fetch generation is its own counter, so no explicit join key
exists for these two record families and none is invented here.

Usage: analyze-p08d-entry-vs-fetch.py <logcat-file> [...]
Prints a per-entry table and a summary.  Exits 0 when the capture parses
(whatever it says), 2 when no capture was readable.
"""

import re
import sys

ENTRY = re.compile(
    r'DDSTART12 RSP cmd_entry data_ptr=(0x[0-9a-f]+) data_size=(0x[0-9a-f]+) '
    r'hash=(0x[0-9a-f]+) nonzero_words=(\d+) words=(\d+)')
FETCH = re.compile(
    r'fetch_generation=(\d+) fetch_buffer_offset=(\d+)')
FETCH_ADDR = re.compile(r'raw_dma_dram=(0x[0-9a-f]+)')
FETCH_HASH = re.compile(r'payload_hash=(0x[0-9a-f]+)')
FETCH_WORDS = re.compile(r'payload_word_count=(\d+)')
FETCH_ALIGN = re.compile(r'aligned_length=(\d+)')
FETCH_ROWS = re.compile(r'transfer_count=(\d+)')
FETCH_SKIP = re.compile(r'skip=(\d+)')
FETCH_TRIGGER = re.compile(r'trigger=(\d+)')


def parse(path):
    """Return (entries, fetches, skipped) for one capture.

    `skipped` counts record-shaped lines that could not be decoded in full, so
    a capture is never silently thinned by a format change."""
    entries = []   # (line_no, time, ptr, size, hash, nonzero, words)
    fetches = []   # (line_no, time, dram, gen, offset, hash, word_count)
    skipped = []
    with open(path, 'r', errors='replace') as handle:
        for number, line in enumerate(handle, 1):
            stamp = line.split()[1] if len(line.split()) > 1 else '?'
            if 'DDSTART12 RSP cmd_entry' in line:
                m = ENTRY.search(line)
                if not m:
                    skipped.append((number, 'DDSTART12'))
                    continue
                entries.append((number, stamp, int(m.group(1), 16),
                                int(m.group(2), 16), m.group(3),
                                int(m.group(4)), int(m.group(5))))
            elif 'fetch_watched=1' in line:
                fields = [FETCH_ADDR.search(line), FETCH.search(line),
                          FETCH_HASH.search(line), FETCH_WORDS.search(line),
                          FETCH_ALIGN.search(line), FETCH_ROWS.search(line),
                          FETCH_SKIP.search(line), FETCH_TRIGGER.search(line)]
                if any(f is None for f in fields):
                    skipped.append((number, 'watched-fetch'))
                    continue
                a, g, h, w = fields[0], fields[1], fields[2], fields[3]
                fetches.append((number, stamp, int(a.group(1), 16),
                                int(g.group(1)), int(g.group(2)),
                                h.group(1), int(w.group(1)),
                                fields[4].group(1), fields[5].group(1),
                                fields[6].group(1), fields[7].group(1)))
    return entries, fetches, skipped


def zero_hash_for_words(count):
    """FNV of `count` zero words (the payload hash of an all-zero read)."""
    h = 0xcbf29ce484222325
    for _ in range(count):
        h = (h * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return "0x%016x" % h


def attribute(entries, fetches):
    """Group each fetch under the most recent entry BEFORE it in line order
    whose range contains the address it read; report unattributed fetches
    separately rather than guessing."""
    groups = [{'entry': e, 'fetches': []} for e in entries]
    unattributed = []
    for fetch in fetches:
        line_no, dram = fetch[0], fetch[2]
        placed = False
        for group in reversed(groups):
            entry_line, _, ptr, size = group['entry'][0:4]
            if entry_line >= line_no:
                continue  # this entry had not been submitted yet
            if ptr <= dram < ptr + size:
                group['fetches'].append(fetch)
                placed = True
                break
        if not placed:
            unattributed.append(fetch)
    return groups, unattributed


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    total_entries = total_fetches = 0
    for path in sys.argv[1:]:
        try:
            entries, fetches, skipped = parse(path)
        except OSError as exc:
            print("== %s: cannot read (%s)" % (path, exc), file=sys.stderr)
            return 2
        print("== %s: %d entry line(s), %d fetch record(s),"
              " %d undecodable record-shaped line(s)" %
              (path, len(entries), len(fetches), len(skipped)))
        if skipped:
            print("   undecodable lines (line number, family): %s"
                  % ', '.join('%d/%s' % item for item in skipped[:10]))
        if not entries:
            print("   no DDSTART12 lines: this capture predates the"
                  " entry-hash observer (nothing to correlate)")
            continue
        total_entries += len(entries)
        total_fetches += len(fetches)
        zero_at_entry = 0
        cleared_after = 0
        groups, unattributed = attribute(entries, fetches)
        for group in groups:
            _, stamp, ptr, size, hash_value, nonzero, words = group['entry']
            zero = nonzero == 0
            if zero:
                zero_at_entry += 1
            fetch_hashes = [f[5] for f in group['fetches']]
            fetch_all_zero = bool(fetch_hashes) and all(
                h == zero_hash_for_words(f[6])
                for h, f in zip(fetch_hashes, group['fetches']))
            if not zero and fetch_all_zero:
                cleared_after += 1
            print("   %s entry ptr=0x%08x size=0x%08x hash=%s nonzero=%d (%s)"
                  " -> %d fetch(es)%s" %
                  (stamp, ptr, size, hash_value, nonzero,
                   'ALL ZERO at submission' if zero else 'non-zero',
                   len(group['fetches']),
                   '' if not fetch_hashes else
                   ' payloads=' + ','.join(h[2:10] for h in fetch_hashes)))
        if unattributed:
            print("   %d fetch(es) not attributable to any preceding entry"
                  % len(unattributed))
        print("   summary: %d entr(ies) with an all-zero buffer at submission,"
              " %d entr(ies) non-zero at submission but read as zeros"
              % (zero_at_entry, cleared_after))
    if total_entries:
        print("== overall: %d entr(ies), %d fetch record(s)" %
              (total_entries, total_fetches))
    return 0



if __name__ == '__main__':
    sys.exit(main())
