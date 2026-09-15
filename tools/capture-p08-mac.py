#!/usr/bin/env python3
"""Capture the P08 native DD stall from a Mac host.

This is deliberately a host-side, read-only orchestrator.  It does not install
an APK, clear application data, change preferences, or stop the emulator.  The
launch path is the exported SplashActivity from AndroidManifest.xml followed
by exact, unambiguous UIAutomator targets.  GalleryActivity's source calls the
second target "Start" with ``doRestart=true``; this script never selects
"Resume" and never fabricates GameActivity intent extras (GameActivity is not
exported).

The optional memory path is intentionally conservative.  It uses the
debuggable app's same-UID ``run-as`` only, validates a freshly read aligned
allocation and known guest anchors, and calls the result verified only after
short-read, process-state, pointer-coherence, and resume checks pass.
"""

from __future__ import print_function

import argparse
import datetime
import hashlib
import os
import platform
import re
import shlex
import subprocess
import struct
import sys
import tempfile
import threading
import time
import traceback
import uuid
import zipfile
from dataclasses import dataclass
from xml.etree import ElementTree


DEFAULT_ADB = os.path.expanduser("~/Downloads/platform-tools/adb")
DEFAULT_PACKAGE = "org.mupen64plusae.turnip.pwnedbygary.debug"
SPLASH_ACTIVITY = "paulscode.android.mupen64plusae.SplashActivity"
EMULATION_SUFFIX = ":EmulationProcess"
TARGET_GAME = "F-ZERO X (J)"
TARGET_START = "Start"
UI_DUMP_PATH = "/sdcard/p08-uiautomator.xml"

MEM_BASE_ALIGNMENT = 0x10000
# Match the existing P07 gate (500 MB decimal) while retaining the stricter
# layout-end check below.
MEM_MIN_MAPPING_BYTES = 500_000_000
RDRAM_WINDOW_BYTES = 8 * 1024 * 1024
RSP_WINDOW_BYTES = 8 * 1024
RSP_MEM_OFFSET = 0x04000000
CURTASK_OFFSET = 0x771D68
UCODE_RDRAM_OFFSET = 0x768E60
UCODE_WORD = 0x340A0FC0
UCODE_IMEM_OFFSET = 0x1000
WATCHDOG_SECONDS = 30
MEMORY_PHASE_SECONDS = 20
MEMORY_CLEANUP_RESERVE_SECONDS = 5
MEMORY_GLOBAL_SECONDS = WATCHDOG_SECONDS - MEMORY_CLEANUP_RESERVE_SECONDS
DESCRIPTOR_BYTES = 64
MAX_DESCRIPTOR_DATA_BYTES = RDRAM_WINDOW_BYTES


class CaptureFailure(RuntimeError):
    """A fail-closed capture or launch error."""


class MemoryFailure(CaptureFailure):
    """The optional memory capture could not be verified."""


class CaptureCleanupFailure(CaptureFailure):
    """A stopped-process/watchdog cleanup failed and is top-level fatal."""


class UiTargetNotFound(CaptureFailure):
    """The requested exact UI text is not currently present."""

class UiDumpUnavailable(CaptureFailure):
    """UI automation has not produced a usable fresh hierarchy yet."""


class UiTargetAmbiguous(CaptureFailure):
    """More than one exact visible UI target was found."""


@dataclass
class UiNode:
    attributes: dict
    parent: object = None


@dataclass
class UiTarget:
    text: str
    bounds: tuple
    attributes: dict


def round_up(value, alignment=MEM_BASE_ALIGNMENT):
    """Return *value* rounded up to an integer alignment."""
    if value < 0 or alignment <= 0:
        raise ValueError("round_up requires non-negative value and positive alignment")
    return ((value + alignment - 1) // alignment) * alignment


def parse_bounds(value):
    """Parse Android UIAutomator bounds into (left, top, right, bottom)."""
    match = re.fullmatch(r"\[(\d+),(\d+)\]\[(\d+),(\d+)\]", value or "")
    if not match:
        raise ValueError("invalid UIAutomator bounds: %r" % value)
    left, top, right, bottom = (int(part) for part in match.groups())
    if right <= left or bottom <= top:
        raise ValueError("empty UIAutomator bounds: %r" % value)
    return left, top, right, bottom


def _ui_nodes(element, parent=None):
    node = UiNode(dict(element.attrib), parent)
    yield node
    for child in element:
        for descendant in _ui_nodes(child, node):
            yield descendant


def parse_ui_nodes(xml_bytes):
    """Parse a UIAutomator XML dump without relying on screen coordinates."""
    try:
        root = ElementTree.fromstring(xml_bytes)
    except (ElementTree.ParseError, TypeError) as exc:
        raise CaptureFailure("invalid UIAutomator XML: %s" % exc)
    return list(_ui_nodes(root))


def select_unique_ui_target(xml_bytes, exact_text):
    """Select one exact visible text node, or fail closed.

    If the text is a child of a clickable container, the container's bounds
    are returned.  The bounds still come from the current XML dump; no screen
    coordinate is hardcoded.
    """
    matches = []
    for node in parse_ui_nodes(xml_bytes):
        if node.attributes.get("text") != exact_text:
            continue
        if node.attributes.get("visible-to-user", "true").lower() == "false":
            continue
        try:
            parse_bounds(node.attributes.get("bounds"))
        except ValueError:
            continue
        matches.append(node)

    if not matches:
        raise UiTargetNotFound("exact visible UI text not found: %r" % exact_text)
    if len(matches) != 1:
        raise UiTargetAmbiguous(
            "exact visible UI text is ambiguous (%d matches): %r"
            % (len(matches), exact_text)
        )

    node = matches[0]
    click_node = node
    ancestor = node
    while ancestor is not None:
        if ancestor.attributes.get("clickable", "false").lower() == "true":
            click_node = ancestor
            break
        ancestor = ancestor.parent
    return UiTarget(
        exact_text,
        parse_bounds(click_node.attributes.get("bounds")),
        dict(click_node.attributes),
    )


def parse_pidof_output(output):
    """Return integer PIDs from a pidof result."""
    return [int(value) for value in (output or "").split() if value.isdigit()]


def parse_ps_pids(output, process_name):
    """Find exact process-name PIDs in ``ps -A -o PID,NAME`` output."""
    pids = []
    for line in (output or "").splitlines():
        fields = line.split()
        if not fields:
            continue
        for index, field in enumerate(fields):
            if field != process_name:
                continue
            for prior in reversed(fields[:index]):
                if prior.isdigit():
                    pids.append(int(prior))
                    break
            break
    return sorted(set(pids))


def select_unique_pid(pidof_output, ps_output, process_name):
    """Cross-check pidof and ps, refusing zero/multiple/disagreeing PIDs."""
    pidof_pids = sorted(set(parse_pidof_output(pidof_output)))
    ps_pids = parse_ps_pids(ps_output, process_name)
    if len(pidof_pids) > 1 or len(ps_pids) > 1:
        raise CaptureFailure("multiple %s processes; refusing to guess" % process_name)
    if pidof_pids and ps_pids and pidof_pids[0] != ps_pids[0]:
        raise CaptureFailure(
            "pidof/ps process identity disagrees (%s vs %s)"
            % (pidof_pids[0], ps_pids[0])
        )
    if pidof_pids:
        return pidof_pids[0]
    if ps_pids:
        return ps_pids[0]
    return None


@dataclass
class MemoryMapping:
    start: int
    end: int
    permissions: str
    description: str
    raw: str

    @property
    def size(self):
        return self.end - self.start


_MAP_RE = re.compile(
    r"^\s*([0-9a-fA-F]+)-([0-9a-fA-F]+)\s+(\S+)\s+\S+\s+\S+\s+\S+\s*(.*)$"
)


def parse_maps(maps_text):
    """Parse Linux maps lines while preserving the source line."""
    mappings = []
    for line in (maps_text or "").splitlines():
        match = _MAP_RE.match(line)
        if not match:
            continue
        start, end = int(match.group(1), 16), int(match.group(2), 16)
        if end <= start:
            continue
        mappings.append(
            MemoryMapping(start, end, match.group(3), match.group(4).strip(), line)
        )
    return mappings


def select_memory_mapping(maps_text, minimum=MEM_MIN_MAPPING_BYTES):
    """Select exactly one full-memory Scudo mapping, or fail closed."""
    candidates = [
        mapping
        for mapping in parse_maps(maps_text)
        if mapping.permissions.startswith("rw-p")
        and "[anon:scudo:secondary]" in mapping.description
        and mapping.size >= minimum
    ]
    if len(candidates) != 1:
        raise MemoryFailure(
            "expected one qualifying anonymous full-memory mapping, found %d"
            % len(candidates)
        )
    mapping = candidates[0]
    base = round_up(mapping.start)
    rsp_end = base + RSP_MEM_OFFSET + RSP_WINDOW_BYTES
    if base < mapping.start or rsp_end > mapping.end:
        raise MemoryFailure(
            "aligned memory base does not cover full RSP window: "
            "map=%x-%x base=%x rsp_end=%x"
            % (mapping.start, mapping.end, base, rsp_end)
        )
    return mapping, base


def decode_u32_le(data, label):
    if len(data) != 4:
        raise MemoryFailure("%s short read: expected 4 bytes, got %d" % (label, len(data)))
    # The native core's host-layout RDRAM/RSP buffers are little-endian on the
    # Android ABIs used by the existing P07 verifier.  These are host bytes,
    # not a wire-format N64 ROM word.
    return int.from_bytes(data, byteorder="little", signed=False)


def normalize_guest_pointer(value, limit=RDRAM_WINDOW_BYTES):
    """Normalize physical, KSEG0, or KSEG1 pointers into the RAM window."""
    if value < 0:
        raise MemoryFailure("negative guest pointer")
    if 0x80000000 <= value < 0xA0000000:
        physical = value - 0x80000000
    elif 0xA0000000 <= value < 0xC0000000:
        physical = value - 0xA0000000
    elif value < limit:
        physical = value
    else:
        raise MemoryFailure("unsupported guest pointer: 0x%08x" % value)
    if physical >= limit:
        raise MemoryFailure(
            "guest pointer outside captured RAM: 0x%08x -> 0x%x"
            % (value, physical)
        )
    return physical


def validate_memory_anchors(pointer_bytes, ucode_bytes):
    """Validate the guest task pointer and known aspMain IMEM word."""
    pointer = decode_u32_le(pointer_bytes, "gCurAudioTask")
    pointer_physical = normalize_guest_pointer(pointer)
    if pointer == 0 or pointer_physical % 4:
        raise MemoryFailure("implausible gCurAudioTask pointer: 0x%08x" % pointer)
    ucode = decode_u32_le(ucode_bytes, "aspMain IMEM")
    if ucode != UCODE_WORD:
        raise MemoryFailure(
            "known ucode anchor mismatch: got 0x%08x expected 0x%08x"
            % (ucode, UCODE_WORD)
        )
    return pointer, ucode


def validate_audio_descriptor(rdram, curtask_pointer):
    """Validate the captured type-2 task and its bounded pointed-to regions."""
    pointer_physical = normalize_guest_pointer(curtask_pointer)
    if pointer_physical % 4 or pointer_physical + DESCRIPTOR_BYTES > len(rdram):
        raise MemoryFailure("audio descriptor is outside captured RAM")
    words = struct.unpack_from("<16I", rdram, pointer_physical)
    if words[0] != 2:
        raise MemoryFailure("active task is not type 2 audio: type=%d" % words[0])

    fields = (
        ("ucode_boot", words[2], words[3]),
        ("ucode", words[4], words[5]),
        ("ucode_data", words[6], words[7]),
        ("command", words[12], words[13]),
    )
    normalized = {}
    for name, pointer, size in fields:
        if size == 0:
            raise MemoryFailure("%s descriptor size is zero" % name)
        if size < 4:
            raise MemoryFailure("%s descriptor size is too short" % name)
        if size > MAX_DESCRIPTOR_DATA_BYTES:
            raise MemoryFailure("%s descriptor size is unbounded: 0x%x" % (name, size))
        physical = normalize_guest_pointer(pointer)
        if physical + size > len(rdram):
            raise MemoryFailure(
                "%s descriptor range is outside captured RAM: 0x%x+0x%x"
                % (name, physical, size)
            )
        normalized[name] = (physical, size)

    ucode_physical, _ = normalized["ucode"]
    ucode_word = struct.unpack_from("<I", rdram, ucode_physical)[0]
    if ucode_word != UCODE_WORD:
        raise MemoryFailure(
            "descriptor ucode word mismatch: got 0x%08x expected 0x%08x"
            % (ucode_word, UCODE_WORD)
        )
    return {
        "pointer": curtask_pointer,
        "physical": pointer_physical,
        "type": words[0],
        "words": words,
        "ranges": normalized,
        "ucode_word": ucode_word,
    }


def validate_memory_read(data, expected, label, returncode=0):
    """Apply the exact read/return-code gate used by memory capture."""
    if returncode != 0:
        raise MemoryFailure("%s command failed with rc=%s" % (label, returncode))
    if len(data) != expected:
        raise MemoryFailure(
            "%s short read: expected %d bytes, got %d" % (label, expected, len(data))
        )
    return data


def make_launch_command(adb, package):
    """Return the source-verified exported SplashActivity launch command."""
    return [
        adb,
        "shell",
        "am",
        "start",
        "-W",
        "-n",
        "%s/%s" % (package, SPLASH_ACTIVITY),
        "-a",
        "android.intent.action.MAIN",
        "-c",
        "android.intent.category.LAUNCHER",
    ]


class Adb:
    """Small serialized adb runner; all command output is retained by callers."""

    def __init__(self, path, bundle):
        self.path = path
        self.bundle = bundle
        self.lock = threading.Lock()

    def run(self, args, timeout=30):
        command = [self.path] + list(args)
        try:
            with self.lock:
                result = subprocess.run(
                    command,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    timeout=timeout,
                    check=False,
                )
        except (OSError, subprocess.TimeoutExpired) as exc:
            raise CaptureFailure("adb command failed: %s: %s" % (" ".join(command), exc))
        return result

    def shell(self, *args, **kwargs):
        # adb joins shell arguments into remote shell text, not a preserved argv.
        # Quote each token so nested sh -c programs remain a single argument.
        return self.run(["shell", " ".join(shlex.quote(str(a)) for a in args)], **kwargs)

    def exec_out(self, *args, **kwargs):
        return self.run(["exec-out", " ".join(shlex.quote(str(a)) for a in args)], **kwargs)


class Bundle:
    def __init__(self, output_dir):
        timestamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
        base = os.path.join(output_dir, "p08-capture-%s" % timestamp)
        self.directory = tempfile.mkdtemp(
            prefix="p08-capture-%s-run-" % timestamp, dir=output_dir
        )
        self.zip_path = base + ".zip"
        suffix = 1
        while os.path.exists(self.zip_path):
            self.zip_path = "%s-%d.zip" % (base, suffix)
            suffix += 1
        self._ui_counter = 0

    def path(self, name):
        return os.path.join(self.directory, name)

    def write(self, name, data, mode="w"):
        with open(self.path(name), mode) as stream:
            stream.write(data)

    def append_line(self, name, line):
        with open(self.path(name), "a") as stream:
            stream.write(line.rstrip("\n") + "\n")

    def save_result(self, name, result):
        with open(self.path(name), "wb") as stream:
            stream.write(result.stdout or b"")
        with open(self.path(name + ".stderr"), "wb") as stream:
            stream.write(result.stderr or b"")
        self.write(name + ".rc", "%s\n" % result.returncode)

    def save_ui(self, label, xml_bytes):
        self._ui_counter += 1
        safe_label = re.sub(r"[^A-Za-z0-9_.-]+", "_", label)
        name = "ui-%03d-%s.xml" % (self._ui_counter, safe_label)
        with open(self.path(name), "wb") as stream:
            stream.write(xml_bytes)

    def archive(self):
        with open(self.path("sha256sums.txt"), "w") as hashes:
            for name in sorted(os.listdir(self.directory)):
                if name == "sha256sums.txt":
                    continue  # A manifest cannot contain its own completed hash.
                path = self.path(name)
                if not os.path.isfile(path):
                    continue
                digest = hashlib.sha256()
                with open(path, "rb") as stream:
                    for block in iter(lambda: stream.read(1024 * 1024), b""):
                        digest.update(block)
                hashes.write("%s  %s\n" % (digest.hexdigest(), name))
        with zipfile.ZipFile(
            self.zip_path, "w", compression=zipfile.ZIP_DEFLATED
        ) as archive:
            for root, _, files in os.walk(self.directory):
                for name in sorted(files):
                    path = os.path.join(root, name)
                    archive.write(path, os.path.relpath(path, os.path.dirname(self.directory)))
        return self.zip_path


class CpuSampler(threading.Thread):
    def __init__(self, adb, bundle, package, pid, interval=5):
        super().__init__(name="p08-cpu-sampler")
        self.daemon = True
        self.adb = adb
        self.bundle = bundle
        self.package = package
        self.pid = pid
        self.interval = interval
        self.stop_event = threading.Event()
        self.index = 0
        self.errors = []

    def stop(self):
        self.stop_event.set()

    def _save(self, stem, result):
        name = "%s-%03d.txt" % (stem, self.index)
        with open(self.bundle.path(name), "wb") as stream:
            stream.write(result.stdout or b"")
            if result.stderr:
                stream.write(b"\n--- stderr ---\n")
                stream.write(result.stderr)
        self.bundle.append_line(
            "cpu-deltas.txt",
            "%s index=%d rc=%d bytes=%d"
            % (time.time(), self.index, result.returncode, len(result.stdout or b"")),
        )

    def run(self):
        while not self.stop_event.is_set():
            try:
                cpu = self.adb.shell("dumpsys", "cpuinfo", timeout=20)
                self._save("cpuinfo", cpu)
                stat = self.adb.shell(
                    "run-as",
                    self.package,
                    "cat",
                    "/proc/%d/stat" % self.pid,
                    timeout=10,
                )
                self._save("proc-stat", stat)
            except CaptureFailure as exc:
                self.errors.append(str(exc))
                self.bundle.append_line("cpu-errors.txt", str(exc))
            self.index += 1
            if self.stop_event.wait(self.interval):
                break


def _result_text(result):
    return (result.stdout or b"").decode("utf-8", "replace")


def _result_error(result):
    return (result.stderr or b"").decode("utf-8", "replace")


def discover_pid(adb, package):
    process_name = package + EMULATION_SUFFIX
    pidof = adb.shell("pidof", process_name, timeout=10)
    ps = adb.shell("ps", "-A", "-o", "PID,NAME", timeout=10)
    pid = select_unique_pid(
        _result_text(pidof), _result_text(ps), process_name
    )
    return pid


def process_state(stat_text):
    """Return /proc/PID/stat's state, accounting for a parenthesized comm."""
    close = (stat_text or "").rfind(")")
    if close < 0:
        return None
    fields = stat_text[close + 1 :].split()
    return fields[0] if fields else None


def bounded_memory_command(adb, method, args, deadline, label, cleanup=False, limit=30):
    """Run one device command before a phase/global deadline expires."""
    remaining = deadline - time.monotonic()
    if remaining <= 0:
        error_type = CaptureCleanupFailure if cleanup else MemoryFailure
        raise error_type("%s deadline expired" % label)
    try:
        result = method(*args, timeout=min(limit, remaining))
    except CaptureFailure as exc:
        error_type = CaptureCleanupFailure if cleanup else MemoryFailure
        raise error_type("%s failed: %s" % (label, exc))
    if time.monotonic() > deadline:
        error_type = CaptureCleanupFailure if cleanup else MemoryFailure
        raise error_type("%s exceeded deadline" % label)
    return result


def ensure_memory_deadline(deadline, label):
    if time.monotonic() > deadline:
        raise MemoryFailure("%s exceeded deadline" % label)


def device_memory_read(adb, package, pid, address, count, label, bundle, deadline):
    result = bounded_memory_command(
        adb,
        adb.exec_out,
        (
        "run-as",
        package,
        "dd",
        "if=/proc/%d/mem" % pid,
        "iflag=skip_bytes,count_bytes",
        "skip=0x%x" % address,
        "count=%d" % count,
        ),
        deadline,
        "memory read %s" % label,
    )
    with open(bundle.path("memory-read-%s.stderr" % label), "wb") as stream:
        stream.write(result.stderr or b"")
    ensure_memory_deadline(deadline, "memory read %s save" % label)
    return validate_memory_read(result.stdout or b"", count, label, result.returncode)


def parse_watchdog_pid(output):
    pids = [int(line.strip()) for line in (output or "").splitlines()
            if line.strip().isdigit()]
    if len(pids) != 1:
        raise CaptureCleanupFailure("device watchdog did not return one PID")
    return pids[0]


def bounded_process_state(
    adb, package, pid, label, bundle, deadline, cleanup=False
):
    result = bounded_memory_command(
        adb,
        adb.shell,
        ("run-as", package, "cat", "/proc/%d/stat" % pid),
        deadline,
        label,
        cleanup=cleanup,
        limit=5,
    )
    bundle.save_result(label + ".txt", result)
    state = process_state(_result_text(result))
    return result, state


def process_starttime(stat_text):
    """Return /proc/PID/stat field 22 after the comm/state prefix."""
    close = (stat_text or "").rfind(")")
    if close < 0:
        return None
    fields = stat_text[close + 1 :].split()
    return fields[19] if len(fields) > 19 else None


def read_watchdog_identity(adb, package, watchdog_pid, bundle, deadline, label):
    cmdline = bounded_memory_command(
        adb,
        adb.shell,
        ("run-as", package, "cat", "/proc/%d/cmdline" % watchdog_pid),
        deadline,
        "watchdog %s cmdline" % label,
        cleanup=True,
        limit=3,
    )
    stat = bounded_memory_command(
        adb,
        adb.shell,
        ("run-as", package, "cat", "/proc/%d/stat" % watchdog_pid),
        deadline,
        "watchdog %s starttime" % label,
        cleanup=True,
        limit=3,
    )
    bundle.save_result("memory-watchdog-identity-%s-cmdline.txt" % label, cmdline)
    bundle.save_result("memory-watchdog-identity-%s-stat.txt" % label, stat)
    starttime = process_starttime(_result_text(stat))
    if (
        cmdline.returncode != 0
        or not cmdline.stdout
        or stat.returncode != 0
        or not starttime
    ):
        raise CaptureCleanupFailure(
            "device watchdog %s identity unavailable" % label
        )
    return cmdline.stdout, starttime


def cancel_watchdog(
    adb,
    package,
    watchdog_pid,
    bundle,
    deadline,
    expected_cmdline,
    expected_starttime,
):
    current_cmdline, current_starttime = read_watchdog_identity(
        adb, package, watchdog_pid, bundle, deadline, "before-cancel"
    )
    if (
        current_cmdline != expected_cmdline
        or current_starttime != expected_starttime
    ):
        raise CaptureCleanupFailure(
            "watchdog identity mismatch; refusing TERM for PID %d" % watchdog_pid
        )
    kill = bounded_memory_command(
        adb,
        adb.shell,
        ("run-as", package, "/system/bin/kill", "-TERM", str(watchdog_pid)),
        deadline,
        "watchdog cancellation",
        cleanup=True,
        limit=3,
    )
    bundle.save_result("memory-watchdog-cancel.txt", kill)
    if kill.returncode != 0:
        raise CaptureCleanupFailure("watchdog cancellation failed")
    while True:
        check = bounded_memory_command(
            adb,
            adb.shell,
            ("run-as", package, "cat", "/proc/%d/stat" % watchdog_pid),
            deadline,
            "watchdog cancellation check",
            cleanup=True,
            limit=2,
        )
        if check.returncode != 0:
            bundle.save_result("memory-watchdog-cancel-check.txt", check)
            return
        bundle.save_result("memory-watchdog-cancel-check.txt", check)
        if deadline - time.monotonic() <= 0.1:
            raise CaptureCleanupFailure("watchdog remained alive after cancellation")
        time.sleep(0.1)


def resume_process(adb, package, pid, bundle, deadline):
    resume = bounded_memory_command(
        adb,
        adb.shell,
        ("run-as", package, "/system/bin/kill", "-CONT", str(pid)),
        deadline,
        "final process resume",
        cleanup=True,
        limit=3,
    )
    bundle.save_result("memory-resume.txt", resume)
    if resume.returncode != 0:
        raise CaptureCleanupFailure("final device resume failed")
    while True:
        state_result, state = bounded_process_state(
            adb,
            package,
            pid,
            "memory-state-resumed",
            bundle,
            deadline,
            cleanup=True,
        )
        if state_result.returncode == 0 and state not in ("T", "t"):
            return state
        if deadline - time.monotonic() <= 0.1:
            raise CaptureCleanupFailure(
                "process was not confirmed resumed (state=%s)" % (state or "unknown")
            )
        time.sleep(0.1)


def resume_then_cancel_watchdog(
    adb,
    package,
    pid,
    bundle,
    deadline,
    watchdog_pid,
    watchdog_cmdline,
    watchdog_starttime,
):
    """Only cancel the watchdog after CONT and a verified running state."""
    resumed_state = resume_process(adb, package, pid, bundle, deadline)
    cancel_watchdog(
        adb,
        package,
        watchdog_pid,
        bundle,
        deadline,
        watchdog_cmdline,
        watchdog_starttime,
    )
    return resumed_state


def capture_memory(adb, bundle, package, pid):
    """Capture and verify the bounded full-layout memory windows."""
    lines = []

    def note(line):
        lines.append(line)
        bundle.write("memory-metadata.txt", "\n".join(lines) + "\n")

    access = adb.shell("run-as", package, "id", timeout=15)
    bundle.save_result("memory-run-as-id.txt", access)
    if access.returncode != 0:
        raise MemoryFailure("run-as access denied: %s" % _result_error(access).strip())
    note("run_as_id=%s" % _result_text(access).strip())

    maps_result = adb.shell(
        "run-as", package, "cat", "/proc/%d/maps" % pid, timeout=20
    )
    bundle.save_result("memory-maps.txt", maps_result)
    if maps_result.returncode != 0:
        raise MemoryFailure("run-as maps read failed")
    maps_text = _result_text(maps_result)
    mapping, base = select_memory_mapping(maps_text)
    note("mapping=%s" % mapping.raw)
    note("mapping_size=%d" % mapping.size)
    note("mem_base=0x%x" % base)
    note("mem_base_alignment=%d" % MEM_BASE_ALIGNMENT)
    note("rdram_window_bytes=%d" % RDRAM_WINDOW_BYTES)
    note("rsp_mem=0x%x" % (base + RSP_MEM_OFFSET))
    note("rdram_ucode_offset=0x%x" % UCODE_RDRAM_OFFSET)
    note("rsp_ucode_offset=0x%x" % (RSP_MEM_OFFSET + UCODE_IMEM_OFFSET))

    state_result = adb.shell(
        "run-as", package, "cat", "/proc/%d/stat" % pid, timeout=10
    )
    bundle.save_result("memory-state-before-stop.txt", state_result)
    if state_result.returncode != 0:
        raise MemoryFailure("could not read process state before SIGSTOP")
    state_before = process_state(_result_text(state_result))
    note("state_before_stop=%s" % (state_before or "unknown"))
    if state_before in ("T", "t"):
        raise MemoryFailure("process was already stopped; refusing to alter its state")

    # A device-side watchdog is established before SIGSTOP.  It is independent
    # of the Mac host and resumes this PID after a bounded interval if the host
    # loses the adb connection or is interrupted.
    watchdog_command_start = time.monotonic()
    watchdog = adb.shell(
        "run-as",
        package,
        "sh",
        "-c",
        "(sleep %d; /system/bin/kill -CONT %d) >/dev/null 2>&1 & echo $!"
        % (WATCHDOG_SECONDS, pid),
        timeout=10,
    )
    bundle.save_result("memory-watchdog.txt", watchdog)
    if watchdog.returncode != 0:
        raise CaptureCleanupFailure("could not arrange device resume watchdog")
    watchdog_pid = parse_watchdog_pid(_result_text(watchdog))
    global_deadline = watchdog_command_start + MEMORY_GLOBAL_SECONDS
    phase_deadline = watchdog_command_start + MEMORY_PHASE_SECONDS
    note("device_watchdog_pid=%d" % watchdog_pid)
    note("device_watchdog_seconds=%d" % WATCHDOG_SECONDS)
    note("memory_global_deadline_seconds=%d" % MEMORY_GLOBAL_SECONDS)
    note("memory_read_deadline_seconds=%d" % MEMORY_PHASE_SECONDS)
    watchdog_cmdline, watchdog_starttime = read_watchdog_identity(
        adb, package, watchdog_pid, bundle, global_deadline, "initial"
    )
    note("device_watchdog_starttime=%s" % watchdog_starttime)
    note("device_watchdog_cmdline_bytes=%d" % len(watchdog_cmdline))

    stop_attempted = False
    stopped = False
    cleanup_errors = []
    try:
        # Mark this before issuing the signal: a timeout leaves the signal
        # delivery uncertain, so the finally block must still send CONT.
        stop_attempted = True
        stop = bounded_memory_command(
            adb,
            adb.shell,
            ("run-as", package, "/system/bin/kill", "-STOP", str(pid)),
            global_deadline,
            "SIGSTOP",
            limit=5,
        )
        bundle.save_result("memory-stop.txt", stop)
        if stop.returncode != 0:
            raise MemoryFailure("SIGSTOP failed: %s" % _result_error(stop).strip())

        state_stopped, state_value = bounded_process_state(
            adb, package, pid, "memory-state-stopped", bundle, phase_deadline
        )
        note("state_after_stop=%s" % (state_value or "unknown"))
        stopped = True
        if state_stopped.returncode != 0 or state_value not in ("T", "t"):
            raise MemoryFailure("process was not confirmed stopped")

        pointer_address = base + CURTASK_OFFSET
        rsp_address = base + RSP_MEM_OFFSET
        pointer_bytes = device_memory_read(
            adb, package, pid, pointer_address, 4, "curtask-before", bundle,
            phase_deadline,
        )
        ucode_rdram_bytes = device_memory_read(
            adb,
            package,
            pid,
            base + UCODE_RDRAM_OFFSET,
            4,
            "ucode-rdram-before",
            bundle,
            phase_deadline,
        )
        ucode_bytes = device_memory_read(
            adb,
            package,
            pid,
            rsp_address + UCODE_IMEM_OFFSET,
            4,
            "ucode-before",
            bundle,
            phase_deadline,
        )
        pointer, ucode_rdram = validate_memory_anchors(
            pointer_bytes, ucode_rdram_bytes
        )
        ucode = decode_u32_le(ucode_bytes, "aspMain IMEM")
        if ucode != UCODE_WORD:
            raise MemoryFailure(
                "known IMEM ucode anchor mismatch: got 0x%08x expected 0x%08x"
                % (ucode, UCODE_WORD)
            )
        note("curtask_address=0x%x" % pointer_address)
        note("curtask_before=0x%08x" % pointer)
        note("ucode_rdram_address=0x%x" % (base + UCODE_RDRAM_OFFSET))
        note("ucode_rdram_before=0x%08x" % ucode_rdram)
        note("ucode_address=0x%x" % (rsp_address + UCODE_IMEM_OFFSET))
        note("ucode_before=0x%08x" % ucode)

        rdram = device_memory_read(
            adb, package, pid, base, RDRAM_WINDOW_BYTES, "rdram-window", bundle,
            phase_deadline,
        )
        with open(bundle.path("rdram-window.bin"), "wb") as stream:
            stream.write(rdram)
        ensure_memory_deadline(phase_deadline, "RDRAM window save")
        rsp = device_memory_read(
            adb,
            package,
            pid,
            rsp_address,
            RSP_WINDOW_BYTES,
            "rspmem",
            bundle,
            phase_deadline,
        )
        with open(bundle.path("rspmem.bin"), "wb") as stream:
            stream.write(rsp)
        ensure_memory_deadline(phase_deadline, "RSP window save")

        pointer_after_bytes = device_memory_read(
            adb, package, pid, pointer_address, 4, "curtask-after", bundle,
            phase_deadline,
        )
        pointer_after = decode_u32_le(pointer_after_bytes, "gCurAudioTask after")
        note("curtask_after=0x%08x" % pointer_after)
        if pointer_after != pointer:
            raise MemoryFailure(
                "gCurAudioTask changed while stopped: 0x%08x -> 0x%08x"
                % (pointer, pointer_after)
            )
        final_state_result, final_state = bounded_process_state(
            adb, package, pid, "memory-state-final-stopped", bundle, phase_deadline
        )
        if final_state_result.returncode != 0 or final_state not in ("T", "t"):
            raise MemoryFailure(
                "process was not stopped for final coherence check: %s"
                % (final_state or "unknown")
            )
        descriptor = validate_audio_descriptor(rdram, pointer)
        note("descriptor_physical=0x%x" % descriptor["physical"])
        note("descriptor_type=%d" % descriptor["type"])
        for name, (physical, size) in descriptor["ranges"].items():
            note("descriptor_%s=0x%x+0x%x" % (name, physical, size))
        note("descriptor_ucode_word=0x%08x" % descriptor["ucode_word"])
        note("coherence=pointer_unchanged")
    finally:
        if stop_attempted:
            try:
                resumed_state = resume_then_cancel_watchdog(
                    adb,
                    package,
                    pid,
                    bundle,
                    global_deadline,
                    watchdog_pid,
                    watchdog_cmdline,
                    watchdog_starttime,
                )
                note("final_resumed_state=%s" % resumed_state)
                note("watchdog_cancelled=1")
            except CaptureCleanupFailure as exc:
                cleanup_errors.append(str(exc))
        if cleanup_errors:
            raise CaptureCleanupFailure("; ".join(cleanup_errors))

    if not stopped:
        raise MemoryFailure("process stop was not established")
    note("status=verified")


def dump_ui(adb, bundle, label):
    # Never reuse a previous hierarchy after a zero-exit-status Android error.
    token = os.urandom(8).hex()
    label = "%s-%s" % (label, token)
    remote = UI_DUMP_PATH.replace(".xml", "-%s.xml" % token)
    dump = adb.shell("uiautomator", "dump", "--compressed", remote, timeout=20)
    bundle.save_result("ui-dump-%s.txt" % label, dump)
    try:
        diagnostic = (dump.stdout or b"") + (dump.stderr or b"")
        if dump.returncode != 0 or b"ERROR:" in diagnostic or b"null root node" in diagnostic:
            raise UiDumpUnavailable(
                "uiautomator produced no usable hierarchy: %s"
                % diagnostic.decode("utf-8", errors="replace").strip()
            )
        xml = adb.exec_out("cat", remote, timeout=15)
        bundle.save_result("ui-read-%s.txt" % label, xml)
        if xml.returncode != 0 or not xml.stdout:
            raise UiDumpUnavailable("UIAutomator XML read failed")
        try:
            root = ElementTree.fromstring(xml.stdout)
        except ElementTree.ParseError as exc:
            raise UiDumpUnavailable(
                "UIAutomator returned non-XML data (%s): %r"
                % (exc, xml.stdout[:160])
            )
        if root.tag != "hierarchy":
            raise UiDumpUnavailable("UIAutomator root is not hierarchy")
        bundle.save_ui(label, xml.stdout)
        return xml.stdout
    finally:
        cleanup = adb.shell("rm", "-f", remote, timeout=5)
        bundle.save_result("ui-cleanup-%s.txt" % label, cleanup)


def wait_for_ui_target(adb, bundle, exact_text, label, timeout):
    deadline = time.monotonic() + timeout
    last_error = None
    while time.monotonic() < deadline:
        try:
            xml = dump_ui(adb, bundle, label)
            return select_unique_ui_target(xml, exact_text)
        except (UiTargetNotFound, UiDumpUnavailable) as exc:
            last_error = exc
        except UiTargetAmbiguous:
            # Ambiguity is not transient: guessing would risk launching the
            # wrong title or action.
            raise
        time.sleep(0.5)
    raise CaptureFailure(
        "timed out waiting for exact UI target %r (%s)"
        % (exact_text, last_error or "no dump")
    )


def click_ui_target(adb, bundle, target, label):
    left, top, right, bottom = target.bounds
    x, y = (left + right) // 2, (top + bottom) // 2
    result = adb.shell("input", "tap", str(x), str(y), timeout=15)
    bundle.save_result("ui-click-%s.txt" % label, result)
    if result.returncode != 0:
        raise CaptureFailure("UI target click failed: %s" % _result_error(result).strip())
    bundle.append_line(
        "ui-actions.txt",
        "%s text=%r bounds=%s tap=%d,%d"
        % (label, target.text, target.bounds, x, y),
    )


def write_initial_metadata(bundle, adb_path, package, duration, memory):
    lines = [
        "started_at=%s" % datetime.datetime.now().astimezone().isoformat(),
        "host=%s" % platform.platform(),
        "machine=%s" % platform.machine(),
        "python=%s" % sys.version.replace("\n", " "),
        "adb=%s" % adb_path,
        "package=%s" % package,
        "duration_seconds=%s" % duration,
        "memory_requested=%s" % int(memory),
        "launch_activity=%s" % SPLASH_ACTIVITY,
        "launch_contract=manifest-exported SplashActivity MAIN/LAUNCHER",
        "game_target_exact=%s" % TARGET_GAME,
        "action_target_exact=%s" % TARGET_START,
        "logcat_command=adb logcat -b all -T1 -v threadtime (no -c, no filters)",
    ]
    bundle.write("host-metadata.txt", "\n".join(lines) + "\n")


def capture_device_identity(adb, bundle, package):
    getprop = adb.shell("getprop", timeout=20)
    bundle.save_result("device-getprop.txt", getprop)
    serial = adb.run(["get-serialno"], timeout=15)
    bundle.save_result("device-serial.txt", serial)
    package_dump = adb.shell("dumpsys", "package", package, timeout=30)
    bundle.save_result("package-dumpsys.txt", package_dump)
    text = _result_text(package_dump)
    version_name = re.search(r"\bversionName=([^\s]+)", text)
    version_code = re.search(r"\bversionCode=([^\s]+)", text)
    bundle.append_line(
        "host-metadata.txt",
        "device_serial=%s" % _result_text(serial).strip(),
    )
    bundle.append_line(
        "host-metadata.txt",
        "package_versionName=%s"
        % (version_name.group(1) if version_name else "unavailable"),
    )
    bundle.append_line(
        "host-metadata.txt",
        "package_versionCode=%s"
        % (version_code.group(1) if version_code else "unavailable"),
    )


def ensure_device(adb, bundle):
    state = adb.run(["get-state"], timeout=15)
    bundle.save_result("adb-get-state.txt", state)
    if state.returncode != 0 or _result_text(state).strip() != "device":
        raise CaptureFailure(
            "adb has no ready device: %s" % _result_error(state).strip()
        )


def start_logcat(adb_path, bundle):
    output = open(bundle.path("logcat-threadtime.txt"), "wb")
    errors = open(bundle.path("logcat-stderr.txt"), "wb")
    process = subprocess.Popen(
        [adb_path, "logcat", "-b", "all", "-T1", "-v", "threadtime"],
        stdout=output,
        stderr=errors,
    )
    time.sleep(0.5)
    if process.poll() is not None:
        output.close()
        errors.close()
        raise CaptureFailure("logcat exited before launch")
    return process, output, errors


def wait_for_log_marker(path, marker, timeout=10):
    deadline = time.monotonic() + timeout
    encoded = marker.encode("utf-8")
    while time.monotonic() < deadline:
        try:
            with open(path, "rb") as stream:
                if encoded in stream.read():
                    return
        except OSError:
            pass
        time.sleep(0.1)
    raise CaptureFailure("unique logcat subscription marker was not observed")


def stop_logcat(process, output, errors):
    if process is None:
        return
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)
    output.close()
    errors.close()


def screenshot(adb, bundle, name="screenshot-after-launch.png"):
    result = adb.exec_out("screencap", "-p", timeout=20)
    bundle.save_result(name + ".result", result)
    data = result.stdout or b""
    if result.returncode != 0 or not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise CaptureFailure("screenshot failed or was not PNG")
    with open(bundle.path(name), "wb") as stream:
        stream.write(data)


def capture_cpu_baseline(adb, bundle, package, pid):
    """Retain one immediate post-launch CPU snapshot before periodic samples."""
    cpu = adb.shell("dumpsys", "cpuinfo", timeout=20)
    bundle.save_result("cpu-baseline.txt", cpu)
    stat = adb.shell(
        "run-as", package, "cat", "/proc/%d/stat" % pid, timeout=10
    )
    bundle.save_result("proc-stat-baseline.txt", stat)


def select_game(adb, bundle, manual_start, timeout):
    if manual_start:
        bundle.append_line("host-metadata.txt", "launch_selection=manual; game_identity=unverified")
        print("Logging is active. On the handheld select F-ZERO X (J), then Start (NOT Resume).", flush=True)
        print("Waiting for the emulation process; capture will continue automatically.", flush=True)
        return
    tile = wait_for_ui_target(adb, bundle, TARGET_GAME, "gallery-tile", timeout)
    click_ui_target(adb, bundle, tile, "gallery-tile")
    start = wait_for_ui_target(adb, bundle, TARGET_START, "game-context-start", timeout)
    click_ui_target(adb, bundle, start, "game-context-start")


def wait_capture_interval(deadline):
    while time.monotonic() < deadline:
        time.sleep(min(0.5, max(0.0, deadline - time.monotonic())))


def run_capture(args):
    output_dir = os.path.abspath(os.path.expanduser(args.output_dir))
    if not os.path.isdir(output_dir):
        os.makedirs(output_dir)
    bundle = Bundle(output_dir)
    adb = Adb(os.path.abspath(os.path.expanduser(args.adb)), bundle)
    package = args.package
    logcat_process = None
    logcat_output = None
    logcat_errors = None
    sampler = None
    status = "failed"
    memory_status = "not-requested"
    failure = None

    write_initial_metadata(bundle, adb.path, package, args.duration, args.memory)
    try:
        if not os.path.isfile(adb.path) or not os.access(adb.path, os.X_OK):
            raise CaptureFailure("adb is not executable: %s" % adb.path)
        ensure_device(adb, bundle)
        capture_device_identity(adb, bundle, package)

        # Reject a live game rather than attempting to kill or restart it.
        existing_pid = discover_pid(adb, package)
        if existing_pid is not None:
            raise CaptureFailure(
                "emulation process already running (pid %d); refusing to kill or reuse it"
                % existing_pid
            )

        logcat_process, logcat_output, logcat_errors = start_logcat(adb.path, bundle)
        bundle.append_line("host-metadata.txt", "logcat_started_before_launch=1")
        marker = "P08_CAPTURE_READY_%s" % uuid.uuid4().hex
        marker_result = adb.shell("log", "-t", "P08_CAPTURE", marker, timeout=10)
        bundle.save_result("logcat-subscription-marker.txt", marker_result)
        if marker_result.returncode != 0:
            raise CaptureFailure("device logcat subscription marker failed")
        wait_for_log_marker(bundle.path("logcat-threadtime.txt"), marker)
        bundle.write("logcat-marker.txt", marker + "\n")

        launch = adb.run(
            make_launch_command(adb.path, package)[1:], timeout=max(30, args.ui_timeout)
        )
        bundle.save_result("launch-splash.txt", launch)
        launch_text = _result_text(launch)
        if launch.returncode != 0 or re.search(r"(?im)^\s*error:", launch_text):
            raise CaptureFailure("SplashActivity launch failed")

        select_game(adb, bundle, args.manual_start, args.ui_timeout)

        deadline = time.monotonic() + (120 if args.manual_start else args.duration)
        pid = None
        while time.monotonic() < deadline and pid is None:
            pid = discover_pid(adb, package)
            if pid is None:
                time.sleep(0.5)
        if pid is None:
            raise CaptureFailure("game did not produce a unique emulation process")
        # User-selection time is not part of the emulation observation interval.
        deadline = time.monotonic() + args.duration
        print("Emulator detected (PID %d). Capturing for %d seconds." % (pid, args.duration), flush=True)
        bundle.write("runtime-pid.txt", "%d\n" % pid)
        runtime_maps = adb.shell(
            "run-as", package, "cat", "/proc/%d/maps" % pid, timeout=20
        )
        bundle.save_result("runtime-maps.txt", runtime_maps)
        if runtime_maps.returncode != 0 or not runtime_maps.stdout:
            raise CaptureFailure("runtime maps could not be read")

        screenshot(adb, bundle)
        capture_cpu_baseline(adb, bundle, package, pid)
        sampler = CpuSampler(adb, bundle, package, pid)
        sampler.start()

        # Observe the full interval before taking the late-state memory snapshot.
        wait_capture_interval(deadline)
        screenshot(adb, bundle, "screenshot-end-of-interval.png")
        if args.memory:
            memory_status = "failed"
            try:
                memory_pid = discover_pid(adb, package)
                if memory_pid != pid:
                    raise MemoryFailure(
                        "runtime PID changed before memory capture: %s -> %s"
                        % (pid, memory_pid)
                    )
                bundle.append_line("runtime-pid.txt", "memory_pid=%d" % memory_pid)
                capture_memory(adb, bundle, package, pid)
                memory_status = "verified"
            except MemoryFailure as exc:
                memory_status = "failed"
                bundle.append_line("memory-metadata.txt", "status=failed")
                bundle.append_line("memory-metadata.txt", "failure=%s" % exc)
                bundle.append_line("orchestrator.log", "memory_status=failed: %s" % exc)
        status = "complete"
    except KeyboardInterrupt:
        status = "interrupted"
        failure = "capture interrupted"
        bundle.append_line("orchestrator.log", failure)
    except (CaptureFailure, OSError, ValueError) as exc:
        failure = str(exc)
        bundle.append_line("orchestrator.log", "failure=%s" % failure)
        bundle.append_line("orchestrator.log", traceback.format_exc())
    finally:
        if sampler is not None:
            sampler.stop()
            sampler.join(timeout=25)
            if sampler.is_alive():
                bundle.append_line("cpu-errors.txt", "sampler did not stop before finalization")
        stop_logcat(logcat_process, logcat_output, logcat_errors)
        bundle.append_line("orchestrator.log", "status=%s" % status)
        bundle.append_line("orchestrator.log", "memory_status=%s" % memory_status)
        try:
            zip_path = bundle.archive()
        except (OSError, zipfile.BadZipFile) as exc:
            zip_path = ""
            failure = failure or ("could not create ZIP: %s" % exc)
            status = "failed"
        print("CAPTURE_STATUS=%s" % status)
        print("MEMORY_STATUS=%s" % memory_status)
        print("CAPTURE_DIR=%s" % bundle.directory)
        if zip_path:
            print("CAPTURE_ZIP=%s" % zip_path)
        if failure:
            print("CAPTURE_ERROR=%s" % failure, file=sys.stderr)
    return 0 if status == "complete" and not failure else 1


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", default=DEFAULT_ADB)
    parser.add_argument("--package", default=DEFAULT_PACKAGE)
    parser.add_argument("--duration", type=int, default=60)
    parser.add_argument("--ui-timeout", type=int, default=45)
    parser.add_argument("--manual-start", action="store_true",
                        help="skip UIAutomator; allow 120 seconds for user to select Start while logging")
    parser.add_argument("--memory", action="store_true", help="attempt verified run-as memory capture")
    parser.add_argument(
        "--output-dir",
        default=os.path.expanduser("~/Downloads"),
        help="local bundle directory (default: ~/Downloads)",
    )
    args = parser.parse_args(argv)
    if args.duration <= 0 or args.duration > 600:
        parser.error("--duration must be between 1 and 600 seconds")
    if args.ui_timeout <= 0 or args.ui_timeout > 300:
        parser.error("--ui-timeout must be between 1 and 300 seconds")
    if not re.fullmatch(r"[A-Za-z0-9_.]+", args.package):
        parser.error("--package is not a valid Android package name")
    return args


def main(argv=None):
    return run_capture(parse_args(argv if argv is not None else sys.argv[1:]))


if __name__ == "__main__":
    sys.exit(main())