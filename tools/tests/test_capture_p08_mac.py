#!/usr/bin/env python3
"""Synthetic host tests for the fail-closed P08 Mac capture helpers."""

import importlib.util
import pathlib
import struct
import subprocess
import hashlib
import zipfile
from contextlib import ExitStack
import sys
import tempfile
import unittest
from unittest import mock


SCRIPT = pathlib.Path(__file__).resolve().parents[1] / "capture-p08-mac.py"
SPEC = importlib.util.spec_from_file_location("capture_p08_mac", SCRIPT)
CAPTURE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = CAPTURE
SPEC.loader.exec_module(CAPTURE)


class UiSelectionTests(unittest.TestCase):
    def test_device_null_root_zero_exit_is_not_read_as_xml(self):
        adb, bundle = mock.Mock(), mock.Mock()
        adb.shell.return_value = mock.Mock(
            returncode=0, stdout=b"",
            stderr=b"ERROR: null root node returned by UiTestAutomationBridge.\n")
        with self.assertRaises(CAPTURE.UiDumpUnavailable):
            CAPTURE.dump_ui(adb, bundle, "gallery")
        adb.exec_out.assert_not_called()
        bundle.save_ui.assert_not_called()

    def test_zero_exit_cat_error_is_not_saved_as_xml(self):
        adb, bundle = mock.Mock(), mock.Mock()
        adb.shell.return_value = mock.Mock(returncode=0, stdout=b"", stderr=b"")
        adb.exec_out.return_value = mock.Mock(
            returncode=0, stdout=b"cat: /sdcard/p08-uiautomator.xml: No such file or directory\n",
            stderr=b"")
        with self.assertRaises(CAPTURE.UiDumpUnavailable):
            CAPTURE.dump_ui(adb, bundle, "gallery")
        bundle.save_ui.assert_not_called()

    def test_transient_dump_retries_then_selects_valid_target(self):
        xml = b'<hierarchy><node text="Start" bounds="[0,0][20,20]" /></hierarchy>'
        with mock.patch.object(CAPTURE, "dump_ui", side_effect=[
                CAPTURE.UiDumpUnavailable("null root"), xml]) as dump:
            with mock.patch.object(CAPTURE.time, "sleep"):
                self.assertEqual(CAPTURE.wait_for_ui_target(
                    mock.Mock(), mock.Mock(), "Start", "start", 10).text, "Start")
            self.assertEqual(dump.call_count, 2)

    def test_persistent_null_root_stops_at_deadline(self):
        with mock.patch.object(CAPTURE, "dump_ui",
                               side_effect=CAPTURE.UiDumpUnavailable("null root")):
            with mock.patch.object(CAPTURE.time, "monotonic", side_effect=[0, 0, 11]):
                with mock.patch.object(CAPTURE.time, "sleep"):
                    with self.assertRaisesRegex(CAPTURE.CaptureFailure, "null root"):
                        CAPTURE.wait_for_ui_target(
                            mock.Mock(), mock.Mock(), "Start", "start", 10)

    def test_exact_target_uses_current_xml_clickable_parent_bounds(self):
        xml = b"""
        <hierarchy>
          <node text="" clickable="false" bounds="[0,0][1000,1000]">
            <node text="F-ZERO X (J)" clickable="true"
                  visible-to-user="true" bounds="[12,34][412,534]" />
          </node>
        </hierarchy>
        """
        target = CAPTURE.select_unique_ui_target(xml, "F-ZERO X (J)")
        self.assertEqual(target.bounds, (12, 34, 412, 534))

    def test_exact_target_ambiguity_fails_closed(self):
        xml = b"""
        <hierarchy>
          <node text="F-ZERO X (J)" bounds="[0,0][100,100]" />
          <node text="F-ZERO X (J)" bounds="[100,0][200,100]" />
        </hierarchy>
        """
        with self.assertRaises(CAPTURE.UiTargetAmbiguous):
            CAPTURE.select_unique_ui_target(xml, "F-ZERO X (J)")

    def test_missing_target_is_not_substituted(self):
        with self.assertRaises(CAPTURE.UiTargetNotFound):
            CAPTURE.select_unique_ui_target(
                b'<hierarchy><node text="Resume" bounds="[0,0][1,1]" /></hierarchy>',
                "Start",
            )


class ProcessAndMemorySelectionTests(unittest.TestCase):
    def test_pid_selection_requires_unique_agreement(self):
        self.assertEqual(
            CAPTURE.select_unique_pid(
                "4123\n",
                "PID NAME\n4123 org.example:EmulationProcess\n",
                "org.example:EmulationProcess",
            ),
            4123,
        )
        with self.assertRaises(CAPTURE.CaptureFailure):
            CAPTURE.select_unique_pid(
                "4123 4124\n",
                "PID NAME\n4123 org.example:EmulationProcess\n",
                "org.example:EmulationProcess",
            )

    def test_mapping_alignment_and_layout(self):
        maps = (
            "1000-2000 rw-p 00000000 00:00 0 [anon:scudo:secondary]\n"
            "6fc546f000-6fe547e000 rw-p 00000000 00:00 0 "
            "[anon:scudo:secondary]\n"
        )
        mapping, base = CAPTURE.select_memory_mapping(maps)
        self.assertEqual(mapping.start, 0x6FC546F000)
        self.assertEqual(base, 0x6FC5470000)
        self.assertEqual(base % 0x10000, 0)

    def test_ambiguous_mapping_is_rejected(self):
        one = "100000000-120000000 rw-p 0 0:0 0 [anon:scudo:secondary]\n"
        two = "200000000-220000000 rw-p 0 0:0 0 [anon:scudo:secondary]\n"
        with self.assertRaises(CAPTURE.MemoryFailure):
            CAPTURE.select_memory_mapping(one + two)

    def test_anchor_validation_is_host_little_endian_and_strict(self):
        pointer, ucode = CAPTURE.validate_memory_anchors(
            bytes.fromhex("a0ea6e80"), bytes.fromhex("c00f0a34")
        )
        self.assertEqual(pointer, 0x806EEAA0)
        self.assertEqual(ucode, 0x340A0FC0)
        with self.assertRaises(CAPTURE.MemoryFailure):
            CAPTURE.validate_memory_anchors(
                bytes.fromhex("806eeaa0"), bytes.fromhex("00000000")
            )

    def test_descriptor_is_type2_bounded_and_ucode_anchored(self):
        rdram = bytearray(CAPTURE.RDRAM_WINDOW_BYTES)
        words = [
            2, 0,
            0x80020000, 4,
            0xA0020000, 4,
            0x80030000, 4,
            0, 0,
            0, 0,
            0xA0040000, 4,
            0, 0,
        ]
        struct.pack_into("<16I", rdram, 0x1000, *words)
        rdram[0x20000:0x20004] = bytes.fromhex("c00f0a34")
        descriptor = CAPTURE.validate_audio_descriptor(rdram, 0x80001000)
        self.assertEqual(descriptor["type"], 2)
        self.assertEqual(descriptor["ranges"]["command"], (0x40000, 4))

        bad = bytearray(rdram)
        struct.pack_into("<I", bad, 0x1000, 1)
        with self.assertRaises(CAPTURE.MemoryFailure):
            CAPTURE.validate_audio_descriptor(bad, 0x80001000)

        bad = bytearray(rdram)
        struct.pack_into("<I", bad, 0x1000 + 12 * 4, 0x90000000)
        with self.assertRaises(CAPTURE.MemoryFailure):
            CAPTURE.validate_audio_descriptor(bad, 0x80001000)

    def test_pointer_normalization_accepts_physical_kseg0_and_kseg1(self):
        self.assertEqual(CAPTURE.normalize_guest_pointer(0x001234), 0x1234)
        self.assertEqual(CAPTURE.normalize_guest_pointer(0x80001234), 0x1234)
        self.assertEqual(CAPTURE.normalize_guest_pointer(0xA0001234), 0x1234)

    def test_short_or_error_read_never_passes(self):
        with self.assertRaises(CAPTURE.MemoryFailure):
            CAPTURE.validate_memory_read(b"\x00" * 3, 4, "anchor")
        with self.assertRaises(CAPTURE.MemoryFailure):
            CAPTURE.validate_memory_read(b"\x00" * 4, 4, "anchor", returncode=1)


class BoundedCaptureTests(unittest.TestCase):
    def test_deadline_and_watchdog_pid_fail_closed(self):
        with self.assertRaises(CAPTURE.MemoryFailure):
            CAPTURE.bounded_memory_command(
                None, lambda **_: None, (), 0, "expired"
            )
        with self.assertRaises(CAPTURE.CaptureCleanupFailure):
            CAPTURE.bounded_memory_command(
                None, lambda **_: None, (), 0, "expired cleanup", cleanup=True
            )
        self.assertEqual(CAPTURE.parse_watchdog_pid("1234\n"), 1234)
        with self.assertRaises(CAPTURE.CaptureCleanupFailure):
            CAPTURE.parse_watchdog_pid("1234\n5678\n")

    def test_state_parser_distinguishes_stopped_from_running(self):
        self.assertEqual(CAPTURE.process_state("12 (emu) T 1 2 3"), "T")
        self.assertEqual(CAPTURE.process_state("12 (emu) S 1 2 3"), "S")
        self.assertIsNone(CAPTURE.process_state("malformed"))
        stat = "12 (wd) S " + " ".join(["0"] * 18 + ["41"])
        self.assertEqual(CAPTURE.process_starttime(stat), "41")

    def test_watchdog_cancel_follows_confirmed_resume_only(self):
        events = []

        def resume(*args, **kwargs):
            events.append("resume")
            return "S"

        def cancel(*args, **kwargs):
            events.append("cancel")

        with mock.patch.object(CAPTURE, "resume_process", resume):
            with mock.patch.object(CAPTURE, "cancel_watchdog", cancel):
                state = CAPTURE.resume_then_cancel_watchdog(
                    None, "pkg", 12, None, 999, 44, b"wd\0", "41"
                )
        self.assertEqual(state, "S")
        self.assertEqual(events, ["resume", "cancel"])

        events[:] = []

        def failed_resume(*args, **kwargs):
            events.append("resume")
            raise CAPTURE.CaptureCleanupFailure("resume failed")

        with mock.patch.object(CAPTURE, "resume_process", failed_resume):
            with mock.patch.object(CAPTURE, "cancel_watchdog", cancel):
                with self.assertRaises(CAPTURE.CaptureCleanupFailure):
                    CAPTURE.resume_then_cancel_watchdog(
                        None, "pkg", 12, None, 999, 44, b"wd\0", "41"
                    )
        self.assertEqual(events, ["resume"])

    def test_watchdog_identity_mismatch_refuses_term(self):
        class FakeAdb:
            def __init__(self, cmdline=b"replacement\0", starttime=b"41"):
                self.calls = []
                self.cmdline = cmdline
                self.starttime = starttime

            def shell(self, *args, **kwargs):
                self.calls.append(args)
                if "cmdline" in args:
                    output = self.cmdline
                elif "stat" in args:
                    output = (
                        b"77 (wd) S "
                        + b" ".join([b"0"] * 18 + [self.starttime])
                    )
                else:
                    output = b""
                return CAPTURE.subprocess.CompletedProcess(
                    args, 0, output, b""
                )

        with tempfile.TemporaryDirectory() as directory:
            fake_adb = FakeAdb()
            bundle = CAPTURE.Bundle(directory)
            with self.assertRaises(CAPTURE.CaptureCleanupFailure):
                CAPTURE.cancel_watchdog(
                    fake_adb,
                    "pkg",
                    77,
                    bundle,
                    float("inf"),
                    b"original\0",
                    "41",
                )
            self.assertFalse(
                any("-TERM" in call for call in fake_adb.calls),
                "identity mismatch must not send TERM",
            )

            fake_adb = FakeAdb(cmdline=b"original\0", starttime=b"42")
            with self.assertRaises(CAPTURE.CaptureCleanupFailure):
                CAPTURE.cancel_watchdog(
                    fake_adb,
                    "pkg",
                    77,
                    bundle,
                    float("inf"),
                    b"original\0",
                    "41",
                )
            self.assertFalse(any("-TERM" in call for call in fake_adb.calls))

    def test_marker_must_be_observed_in_log_file(self):
        with tempfile.NamedTemporaryFile() as stream:
            stream.write(b"prefix P08_CAPTURE_READY_test suffix\n")
            stream.flush()
            CAPTURE.wait_for_log_marker(stream.name, "P08_CAPTURE_READY_test", timeout=0.01)
            with self.assertRaises(CAPTURE.CaptureFailure):
                CAPTURE.wait_for_log_marker(stream.name, "missing", timeout=0.01)


class LaunchContractTests(unittest.TestCase):
    def test_archive_manifest_excludes_itself_and_validates_all_payloads(self):
        with tempfile.TemporaryDirectory() as directory:
            bundle = CAPTURE.Bundle(directory)
            bundle.write("sample.txt", "retained evidence\n")
            archive = bundle.archive()
            with zipfile.ZipFile(archive) as z:
                manifest_path = next(n for n in z.namelist() if n.endswith("/sha256sums.txt"))
                prefix = manifest_path.rsplit("/", 1)[0] + "/"
                manifest = z.read(manifest_path).decode().splitlines()
                self.assertTrue(manifest)
                for line in manifest:
                    expected, name = line.split(None, 1)
                    self.assertNotEqual(name, "sha256sums.txt")
                    self.assertEqual(hashlib.sha256(z.read(prefix + name)).hexdigest(), expected)

    def test_orchestration_waits_before_memory_and_aborts_memory_on_wait_failure(self):
        for wait_fails in (False, True):
            with self.subTest(wait_fails=wait_fails), tempfile.TemporaryDirectory() as directory:
                events = []
                args = CAPTURE.parse_args(["--manual-start", "--memory", "--output-dir", directory])
                adb = mock.Mock()
                adb.path = "/fake/adb"
                result = mock.Mock(returncode=0, stdout=b"ok", stderr=b"")
                adb.run.return_value = result
                adb.shell.return_value = result
                sampler = mock.Mock()
                sampler.start.side_effect = lambda: events.append("sampler")
                sampler.is_alive.return_value = False

                def wait(deadline):
                    events.append("wait")
                    if wait_fails:
                        raise CAPTURE.CaptureFailure("test interval failure")

                def screen(*a, **kw):
                    events.append("end-screen" if len(a) > 2 else "start-screen")

                with ExitStack() as stack:
                    patches = {
                        "Adb": mock.Mock(return_value=adb),
                        "ensure_device": mock.Mock(),
                        "capture_device_identity": mock.Mock(),
                        "discover_pid": mock.Mock(side_effect=[None, 123, 123]),
                        "start_logcat": mock.Mock(return_value=(mock.Mock(), mock.Mock(), mock.Mock())),
                        "stop_logcat": mock.Mock(),
                        "wait_for_log_marker": mock.Mock(),
                        "select_game": mock.Mock(),
                        "screenshot": screen,
                        "capture_cpu_baseline": mock.Mock(),
                        "CpuSampler": mock.Mock(return_value=sampler),
                        "wait_capture_interval": wait,
                        "capture_memory": mock.Mock(side_effect=lambda *a: events.append("memory")),
                    }
                    for name, value in patches.items():
                        stack.enter_context(mock.patch.object(CAPTURE, name, value))
                    stack.enter_context(mock.patch.object(CAPTURE.os.path, "isfile", return_value=True))
                    stack.enter_context(mock.patch.object(CAPTURE.os, "access", return_value=True))
                    code = CAPTURE.run_capture(args)
                if wait_fails:
                    self.assertEqual(code, 1)
                    self.assertNotIn("memory", events)
                    self.assertNotIn("end-screen", events)
                else:
                    self.assertEqual(code, 0)
                    self.assertEqual(events, ["start-screen", "sampler", "wait", "end-screen", "memory"])

    def test_remote_shell_preserves_nested_program_argument(self):
        adb = CAPTURE.Adb("/unused/adb", mock.Mock())
        program = "(printf '%s' 'watchdog syntax'); printf ' %s' '$! preserved'"
        with mock.patch.object(adb, "run") as run:
            adb.shell("sh", "-c", program)
            remote = run.call_args.args[0]
        self.assertEqual(remote[0], "shell")
        result = subprocess.run(["sh", "-c", remote[1]], capture_output=True)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout, b"watchdog syntax $! preserved")

    def test_exec_out_preserves_argument_spaces(self):
        adb = CAPTURE.Adb("/unused/adb", mock.Mock())
        with mock.patch.object(adb, "run") as run:
            adb.exec_out("printf", "%s", "a b; not a command")
            remote = run.call_args.args[0]
        result = subprocess.run(["sh", "-c", remote[1]], capture_output=True)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout, b"a b; not a command")

    def test_interval_wait_does_not_return_early(self):
        with mock.patch.object(CAPTURE.time, "monotonic", side_effect=[0, 0, 59.5, 59.5, 60]):
            with mock.patch.object(CAPTURE.time, "sleep") as sleep:
                CAPTURE.wait_capture_interval(60)
        self.assertEqual(sleep.call_count, 2)

    def test_manual_start_bypasses_ui_and_records_unverified_identity(self):
        adb, bundle = mock.Mock(), mock.Mock()
        with mock.patch.object(CAPTURE, "wait_for_ui_target") as ui:
            with mock.patch.object(CAPTURE, "click_ui_target") as click:
                CAPTURE.select_game(adb, bundle, True, 45)
        ui.assert_not_called()
        click.assert_not_called()
        self.assertIn("game_identity=unverified", bundle.append_line.call_args[0][1])

    def test_automatic_start_still_selects_exact_targets(self):
        with mock.patch.object(CAPTURE, "wait_for_ui_target") as ui:
            with mock.patch.object(CAPTURE, "click_ui_target") as click:
                CAPTURE.select_game(mock.Mock(), mock.Mock(), False, 45)
        self.assertEqual([c.args[2] for c in ui.call_args_list], ["F-ZERO X (J)", "Start"])
        self.assertEqual(click.call_count, 2)

    def test_launch_is_manifest_route_without_guessed_extras(self):
        command = CAPTURE.make_launch_command(
            "/tmp/adb", "org.mupen64plusae.turnip.pwnedbygary.debug"
        )
        self.assertIn(
            "org.mupen64plusae.turnip.pwnedbygary.debug/"
            "paulscode.android.mupen64plusae.SplashActivity",
            command,
        )
        self.assertNotIn("--es", command)
        self.assertNotIn("--ez", command)
        self.assertNotIn("--ei", command)
        self.assertIn("android.intent.action.MAIN", command)
        self.assertIn("android.intent.category.LAUNCHER", command)


if __name__ == "__main__":
    unittest.main()