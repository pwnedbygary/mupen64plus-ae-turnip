#!/usr/bin/env python3
"""Cheap source-level invariants for the DD dispatcher probe.

These tests intentionally do not launch the emulator or depend on a ROM.  They
protect the few easy-to-regress properties that make the trace interpretable:
recursive delay-slot coverage, publication locking, provenance reconciliation,
and architectural high/low word order.
"""
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
R4300 = ROOT / "src" / "device" / "r4300"


class N64DDDispatchDiagTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.pure = (R4300 / "pure_interp.c").read_text()
        cls.cached = (R4300 / "cached_interp.c").read_text()
        cls.diag = (R4300 / "n64dd_dispatch_diag.c").read_text()
        cls.dynarec = (R4300 / "new_dynarec" / "new_dynarec.c").read_text()

    def test_interpreter_branches_trace_recursive_delay_slots(self):
        for source in (self.pure, self.cached):
            self.assertIn("N64DD_DISPATCH_DIAG_DELAY_PRE", source)
            self.assertIn("N64DD_DISPATCH_DIAG_DELAY_POST", source)
            self.assertIn("const uint32_t n64dd_delay_pc", source)
            self.assertIn("if (!likely || take_jump)", source)

    def test_ring_publication_and_dump_share_lock(self):
        self.assertIn("pthread_mutex_t dd_lock", self.diag)
        self.assertGreaterEqual(self.diag.count("pthread_mutex_lock(&dd_lock)"), 5)
        self.assertGreaterEqual(self.diag.count("pthread_mutex_unlock(&dd_lock)"), 5)
        self.assertIn("DD_STORE_DIRECT", self.diag)
        self.assertIn("DD_STORE_SHADOW", self.diag)
        self.assertIn("dd_reconcile_audio_shadow", self.diag)

    def test_dynarec_does_not_claim_stale_eret_source_pc(self):
        marker = "n64dd_dispatch_diag_eret(r4300, UINT32_C(0xffffffff)"
        self.assertIn(marker, self.dynarec)
        self.assertIn("generated jump_eret ABI", self.dynarec)

    def test_after_words_are_architectural_high_then_low(self):
        self.assertIn(
            "store->after_hi = width > 4 ? dd_word(physical_address) : 0;",
            self.diag,
        )
        self.assertIn(
            "store->after_lo = width > 4 ? dd_word(physical_address + 4)",
            self.diag,
        )


if __name__ == "__main__":
    unittest.main()