#!/usr/bin/env python3
"""Independent verifier for the P07/P07-R/P07-C evidence claims.

Run it on a directory that holds the raw capture files (the verification
bundle keeps them under raw/):

    python3 verify-p07-captures.py [dir]

Every check prints PASS or FAIL with the expected and computed value, and
optional groups that cannot run print SKIP; both failure and skip counts are
reported in the RESULT line and the exit status is nonzero if any check
fails. Nothing is written.

Dump files store guest words in host byte order (little endian); all
multi-byte reads below use '<'. The P05 observer's hash is the
multiply-then-xor FNV variant from rsp/cp0.cpp (DMA_FNV_OFFSET/PRIME).
"""

import hashlib
import pathlib
import struct
import sys

FNV_PRIME = 0x100000001B3
FNV_OFFSET = 0xCBF29CE484222325
MASK64 = (1 << 64) - 1

DMEM, IMEM = 0x04000000, 0x04001000          # CPU-side RSP windows
ASPMAIN, CURTASK = 0x768E60, 0x771D68        # RSP image, gCurAudioTask
SLOT0, SLOT1 = 0x6EEAA0, 0x6EEAF0            # AudioTask rspTask[0..1]
CMDBUF0, CMDBUF1 = 0x411910, 0x4132D0        # data_ptr buffers
ZERO_LO, ZERO_HI = 0x3DA9EF, 0x6ECA10        # documented zero run

FAILURES = []
SKIPPED = []


def check(name, got, want):
    ok = got == want
    print(("PASS  " if ok else "FAIL  ") + name)
    if not ok:
        print("        expected: %s" % (want,))
        print("        got:      %s" % (got,))
        FAILURES.append(name)
    return ok


def fnv_words(words):
    h = FNV_OFFSET
    for w in words:
        h = ((h * FNV_PRIME) & MASK64) ^ w
    return h


def fnv_region(buf, off, words=1024):
    return fnv_words(struct.unpack_from("<%dI" % words, buf, off))


def sha256(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


class Caps:
    """Capture files, with the file-offset convention of each window."""

    def __init__(self, root):
        self.root = pathlib.Path(root)
        if (self.root / "raw").is_dir():
            self.root = self.root / "raw"

    def load(self, name):
        return (self.root / name).read_bytes()

    def have(self, name):
        return (self.root / name).is_file()

    # guest offset -> file offset (the 11:39 run predates the base rounding)
    @staticmethod
    def at(buf, guest, shift):
        return struct.unpack_from("<I", buf, guest + shift)[0]


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "."
    caps = Caps(root)
    need = ["rdram-window-113959.bin", "rspmem-113959.bin", "rspmem-rerun-1201.bin",
            "rdram-window-1202.bin", "rspmem-1202.bin", "rdram-window-1427.bin",
            "rspmem-1427.bin"]
    missing = [n for n in need if not caps.have(n)]
    if missing:
        print("missing capture files: %s" % ", ".join(missing))
        return 2

    rd39 = caps.load("rdram-window-113959.bin")
    rs39 = caps.load("rspmem-113959.bin")
    rs01 = caps.load("rspmem-rerun-1201.bin")
    rd02 = caps.load("rdram-window-1202.bin")
    rs02 = caps.load("rspmem-1202.bin")
    rd27 = caps.load("rdram-window-1427.bin")
    rs27 = caps.load("rspmem-1427.bin")

    print("== A. capture identities ==")
    for name, buf, want in (
            ("rdram-window-113959.bin", rd39, "ee15b35fbfee91a5b970b52d2e55d1d70305b2684041ec9dcd868689c7de88a6"),
            ("rspmem-113959.bin", rs39, "816ed66b8a8e86f31e827d81bf962ea13670589aa84c491f797e7249cedbe660"),
            ("rspmem-rerun-1201.bin", rs01, "07a77a17a87deef593038e3a4e3d4eb2a6e090ab2ccbd74f74c61cf06217f61e"),
            ("rdram-window-1202.bin", rd02, "7e1fbcddfdab8e3aa56ffbe51e82f082765d30868b6bfe60f2338871dbf267d4"),
            ("rspmem-1202.bin", rs02, "c64edefa1a1eef090794175c34f6049878f9e014a096e2e9edb603474da23250"),
            ("rdram-window-1427.bin", rd27, "7e1fbcddfdab8e3aa56ffbe51e82f082765d30868b6bfe60f2338871dbf267d4"),
            ("rspmem-1427.bin", rs27, "07a77a17a87deef593038e3a4e3d4eb2a6e090ab2ccbd74f74c61cf06217f61e")):
        check("sha256 %s" % name, hashlib.sha256(buf).hexdigest(), want)

    print("\n== B. window layout [DMEM][IMEM] ==")
    check("11:39 window starts 0x1000 low (its first 4 KiB are the RDRAM-area tail: all zero)",
          set(rs39[0x0000:0x1000]) == {0}, True)
    check("11:39 window's second half is DMEM (992 non-zero words)",
          sum(1 for w in struct.unpack("<1024I", rs39[0x1000:0x2000]) if w), 992)
    check("12:01/12:02/14:27 windows' first half is DMEM",
          [sum(1 for w in struct.unpack("<1024I", b[0:0x1000]) if w) for b in (rs01, rs02, rs27)],
          [0, 987, 0])

    print("\n== C. frozen IMEM is the aspMain image, bit-exact ==")
    imem_fnv = [fnv_region(b, 0x1000) for b in (rs01, rs02, rs27)]
    check("FNV(IMEM) in all three recaptures", ["0x%016x" % h for h in imem_fnv],
          ["0x3aaaf0f5f121410e"] * 3)
    check("FNV(RDRAM guest 0x768E60 image)", "0x%016x" % fnv_region(rd02, ASPMAIN),
          "0x3aaaf0f5f121410e")
    check("IMEM head words",
          ["%08x" % w for w in struct.unpack_from("<4I", rs27, 0x1000)],
          ["340a0fc0", "8d420018", "8d43001c", "40803800"])
    samples = [struct.unpack_from("<I", rs27, 0x1000 + off)[0] for off in (0x000, 0x3FC, 0xF60, 0xFFC)]
    check("IMEM observer sample offsets", ["%08x" % w for w in samples],
          ["340a0fc0", "4bfba08f", "8c260004", "00010001"])

    if caps.have("logcat-p06-route.txt"):
        log = (caps.root / "logcat-p06-route.txt").read_text(errors="replace")
        check("P06 log records the same IMEM hash 4x before + 4x after",
              (log.count("imem_before_hash=0x3aaaf0f5f121410e"),
               log.count("imem_after_hash=0x3aaaf0f5f121410e")), (4, 4))
        check("P06 log records the same IMEM sample words",
              log.count("imem_before_samples=[340a0fc0,4bfba08f,8c260004,00010001]"), 4)
        check("P06 log's audio task record (16 words)",
              log.count("task_words=[00000002,00000000,80768e60,00001000,00768e60,00001000,"
                        "00794e90,000002df,00000000,00000000,00000000,00000000,"
                        "00411910,000001a0,00000000,00000000]"), 4)
        check("P06 log's graphics task record shows ucode_boot at word 2 (rspboot)",
              log.count("task_words=[00000001,00000004,807504f0,000000d0,"), 7)
    else:
        SKIPPED.append("P06 logcat checks (logcat-p06-route.txt not present)")
        print("SKIP  P06 logcat checks (logcat-p06-route.txt not present)")

    print("\n== D. task chain and descriptor field map ==")
    check("gCurAudioTask word", "0x%08x" % caps.at(rd27, CURTASK, 0), "0x806eeaa0")
    for name, slot in (("rspTask[0]", SLOT0), ("rspTask[1]", SLOT1)):
        ws = struct.unpack_from("<16I", rd27, slot)
        check("%s words 0-7" % name, ["%08x" % w for w in ws[0:8]],
              ["00000002", "00000000", "80768e60", "00001000",
               "80768e60", "00001000", "80794e90", "000002df"])
        check("%s words 8-15" % name, ["%08x" % w for w in ws[8:16]],
              ["00000000", "00000000", "00000000", "00000000",
               "80411910", "000001a0", "00000000", "00000000"] if slot == SLOT0 else
              ["00000000", "00000000", "00000000", "00000000",
               "804132d0", "000001c0", "00000000", "00000000"])
    check("field map: words 2-3 ucode_boot, 4-5 ucode (aspMainTextStart), "
          "6-7 ucode_data (aspMainDataStart 0x80794e90)",
          [struct.unpack_from("<I", rd27, SLOT0 + 4 * i)[0] for i in (2, 4, 6, 12)],
          [0x80768E60, 0x80768E60, 0x80794E90, 0x80411910])

    print("\n== E. zero run and the two command buffers ==")
    lo, hi = ZERO_LO, ZERO_HI
    check("byte at run start - 1 is nonzero", rd27[lo - 1] != 0, True)
    check("byte at run end is nonzero", rd27[hi] != 0, True)
    check("bytes [0x%X, 0x%X) are all zero" % (lo, hi),
          set(rd27[lo:hi]) == {0}, True)
    check("run length", hi - lo, 3219489)
    for name, base, size in (("data_ptr buffer 0x411910", CMDBUF0, 0x1A0),
                             ("rspTask[1] buffer 0x4132D0", CMDBUF1, 0x1C0)):
        check("%s is inside the run and all zero" % name,
              (lo <= base and base + size <= hi, set(rd27[base:base + size]) == {0}),
              (True, True))

    print("\n== F. RDRAM contents equal across samples ==")
    check("12:02 and 14:27 windows byte-identical (full 8 MiB window)", rd02 == rd27, True)
    check("guest-aligned diff 11:39 vs 14:27 over guest 0..0x7FF000 "
          "(8 MiB - 4 KiB: the 11:39 file is shifted +0x1000, so the last 4 KiB is not covered)",
          sum(1 for g in range(0, 0x7FF000) if rd27[g] != rd39[g + 0x1000]), 0)

    print("\n== G. DMEM is rewritten; 752-byte block signature ==")
    check("DMEM(11:39) matches RDRAM guest 0x5EB98 for 752 bytes",
          prefix_len(rd02, rs39[0x1000:0x2000], 0x5EB98), 752)
    check("DMEM(12:02) matches RDRAM guest 0x44C00 for 752 bytes",
          prefix_len(rd02, rs02[0:0x1000], 0x44C00), 752)
    check("DMEM(12:02) non-zero word count",
          sum(1 for w in struct.unpack("<1024I", rs02[0:0x1000]) if w), 987)
    check("all-zero DMEM state hash",
          "0x%016x" % fnv_region(bytes(4096), 0), "0x51d88627df287325")

    series = sorted(caps.root.glob("dmem-series-*.bin"))
    rapid = sorted(caps.root.glob("dmem-rapid-*.bin"))
    if len(series) == 10 and len(rapid) == 20:
        states = [(f.name, f.read_bytes()) for f in series + rapid]
        exact, exact754, partial, copies, zeroed, lowent = [], [], [], [], 0, []
        for name, b in states:
            nz = sum(1 for w in struct.unpack("<1024I", b) if w)
            if nz == 0:
                zeroed += 1
                continue
            src, n = best_match(rd02, b)
            if nz <= 24:                      # sparse: a 4 KiB copy of a mostly-zero region
                copies.append((name, hex(src) if src is not None else "-", n))
            elif distinct_words(b) <= 24:     # a short repeating pattern (e.g. 0x00010001)
                lowent.append(name)
            elif n in (752, 754):
                (exact if n == 752 else exact754).append((name, hex(src), n))
            else:
                partial.append((name, hex(src) if src is not None else "-", n))
        check("exact 752-byte block matches among the 30 samples", len(exact), 7)
        check("their source addresses",
              sorted(s for _, s, _ in exact),
              sorted(["0x28c70", "0x82b08", "0xb6a38", "0xe8970", "0xfa928", "0x92ac8", "0xaca60"]))
        check("the one 754-byte match", [(s, n) for _, s, n in exact754], [("0x1068f8", 754)])
        check("sparse 4 KiB copies", sorted(s for _, s, _ in copies), ["0x85af8", "0x8ead8"])
        check("partial (in-flight) matches", len(partial), 4)
        check("low-entropy repeating states", len(lowent), 2)
        check("fully zeroed samples", zeroed, 14)
    else:
        SKIPPED.append("sample-series checks (dmem-series-*/dmem-rapid-* not present)")
        print("SKIP  sample-series checks (dmem-series-*/dmem-rapid-* not present)")

    print("\n== H. verifier self-check ==")
    check("FNV of the RDRAM aspMain image equals the observer's IMEM hash",
          fnv_region(rd02, ASPMAIN), fnv_region(rs27, 0x1000))

    print("\n%s: %d failure(s), %d skipped check group(s)" % ("RESULT", len(FAILURES), len(SKIPPED)))
    for f in FAILURES:
        print("  FAILED: %s" % f)
    for s in SKIPPED:
        print("  SKIPPED: %s" % s)
    return 1 if FAILURES else 0


def prefix_len(win, block, guest):
    """Longest prefix of block equal to win starting at guest (0 if none)."""
    n = 0
    while n < len(block) and win[guest + n] == block[n]:
        n += 1
    return n


def best_match(win, block):
    """(guest address, prefix length) of the best match of block in win.

    The probe is anchored at the first non-zero word so that sparse blocks
    (whose head can be all zeros) still attribute; all hits are tried and
    the longest prefix wins, so a coincidental short hit cannot shadow the
    real copy.
    """
    words = struct.unpack("<1024I", block)
    nz_idx = [i for i, w in enumerate(words) if w]
    if not nz_idx:
        return None, 0
    start = max(0, nz_idx[0] - 1)
    probe = block[start * 4:(start + 4) * 4]
    best = (None, 0)
    i = win.find(probe)
    while i != -1:
        base = i - start * 4
        if base >= 0:
            n = 0
            while n < len(block) and win[base + n] == block[n]:
                n += 1
            if n > best[1]:
                best = (base, n)
        i = win.find(probe, i + 1)
    return best


def distinct_words(b):
    return len(set(struct.unpack("<1024I", b)))


if __name__ == "__main__":
    sys.exit(main())
