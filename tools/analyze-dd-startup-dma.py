#!/usr/bin/env python3
"""Validate retained DD PI observations without claiming a native repair.

The input may be an adb ``threadtime`` log or a file containing the record lines
themselves.  DDPI2 is parsed as a wire protocol, not as a collection of hints:
bad framing, joins, caps, samples, or counters are reported explicitly.  The
normal result is deliberately incomplete because PI records do not provide the
required C4/C5 staging and publication evidence.
"""
from __future__ import print_function

import argparse
import json
import re
import sys

LINE_CAP_BYTES = 2048                 # formatter buffer, including its NUL
MAX_PAYLOAD_BYTES = LINE_CAP_BYTES - 1
U32 = re.compile(r"0x[0-9a-f]{8}\Z")
U64 = re.compile(r"0x[0-9a-f]{16}\Z")
DECIMAL = re.compile(r"(?:0|[1-9][0-9]*)\Z")
TRANSFER_ID = re.compile(r"E([0-9a-f]{8})-T([0-9a-f]{8})\Z")
THREADTIME = re.compile(
    r"^\d\d-\d\d\s+\d\d:\d\d:\d\d\.\d+\s+(\d+)\s+(\d+)\s+[A-Z]\s+[^:]*:\s?(.*)$")

SESSION_ORDER = (
    "reset_epoch", "obs_sequence", "pi_history_capacity", "sample_cap_bytes",
    "active_transfer_cap", "audio_window_cap", "dynamic_staging_window_cap",
    "staging_coverage", "publication_coverage", "early_flush_limit",
    "zero_audio_flush_limit", "line_cap_bytes")
TRANSFER_ORDER = (
    "phase", "reset_epoch", "obs_sequence", "transfer_sequence", "transfer_id",
    "direction", "status", "handler_class", "bus_region", "raw_pi_status",
    "raw_pi_cart_addr", "raw_pi_dram_addr", "raw_pi_length_register",
    "raw_pi_length", "decoded_request_length", "normalization_flags",
    "handler_cart_bus_addr", "handler_rdram_phys_addr", "handler_length",
    "handler_cart_bus_end_exclusive", "handler_rdram_phys_end_exclusive",
    "handler_cycles", "actual_copied_bytes", "watch_state", "watch_id",
    "overlap_rdram_start", "overlap_rdram_end_exclusive",
    "sample_requested_bytes", "before_sample_length", "after_sample_length",
    "sample_clipped", "before_sample", "after_sample", "sample_read_status",
    "completion_join")
SUMMARY_ORDER = (
    "reset_epoch", "obs_sequence", "flush_reason", "retained_transfers",
    "retained_transfer_cap", "overwritten_transfers", "observed_attempts",
    "rejected_busy", "rejected_unhandled", "suppressed_attempts",
    "other_audio_overlaps", "missing_completion_joins", "truncated_lines",
    "emitted_transfer_lines", "early_flushes", "early_flush_limit",
    "zero_audio_flushes", "zero_audio_flush_limit", "staging_coverage",
    "publication_coverage", "result_scope")
ORDERS = {"session": SESSION_ORDER, "transfer": TRANSFER_ORDER, "summary": SUMMARY_ORDER}

CONCRETE_HANDLERS = {"DD_DOM", "CART_ROM_DOM1", "CART_ROM_DOM3", "CART_SAVE_DOM2"}
REGIONS = {"DD_BUFFER", "DD_ROM", "CART_SAVE", "CART_ROM", "CART_DOM3", "UNKNOWN"}
FLAGS = ("CART_ALIGNMENT_MASKED", "DRAM_ALIGNMENT_MASKED", "ODD_LENGTH_ROUNDED",
         "SHORT_TRANSFER_DRAM_OFFSET_SUBTRACTED", "SHORT_TRANSFER_UNSIGNED_UNDERFLOW")


def _u32(value):
    return int(value, 16) if U32.match(value or "") else None


def _u64(value):
    return int(value, 16) if U64.match(value or "") else None


def _decimal(value):
    return int(value, 10) if DECIMAL.match(value or "") else None


def _sample(value):
    if value == "-":
        return b""
    if value and re.match(r"^(?:[0-9a-f]{2})+$", value):
        return bytes.fromhex(value)
    return None


def _context_and_payload(line):
    line = line.rstrip("\r\n")
    match = THREADTIME.match(line)
    if match:
        # Transfer identity is process-local.  A log tag's thread id is useful
        # provenance, but cannot split an otherwise same-PID asynchronous join.
        return ("pid:%s" % match.group(1), match.group(3))
    return ("plain", line)


def _tokens(payload):
    """Return strict DDPI2 tokens, or an explanatory error."""
    try:
        encoded = payload.encode("ascii")
    except UnicodeEncodeError:
        return None, "non-ASCII payload"
    if len(encoded) > MAX_PAYLOAD_BYTES:
        return None, "payload exceeds %d-byte cap" % MAX_PAYLOAD_BYTES
    parts = payload.split(" ")
    if not parts or any(not part or "=" not in part for part in parts):
        return None, "tokens must be single-space-separated key=value values"
    pairs = []
    seen = set()
    for part in parts:
        key, value = part.split("=", 1)
        if not key or not value or key in seen:
            return None, "duplicate or malformed key"
        if not re.match(r"^[A-Za-z_][A-Za-z0-9_]*$", key):
            return None, "malformed key %r" % key
        seen.add(key)
        pairs.append((key, value))
    return pairs, None


def _error(errors, line, context, code, message):
    errors.append({"line": line, "context": context, "code": code, "message": message})


def _validate_flags(value):
    if value in ("NONE", "NOT_RUN"):
        return True
    bits = value.split(",")
    return bool(bits) and all(bit in FLAGS for bit in bits) and bits == sorted(bits, key=FLAGS.index) and len(bits) == len(set(bits))


def _check_u32(fields, names, errors, line, context, unknown=()):
    for name in names:
        value = fields[name]
        if name in unknown and value == "UNKNOWN":
            continue
        if _u32(value) is None:
            _error(errors, line, context, "BAD_U32", "%s is not a lowercase u32" % name)


def _check_u64(fields, names, errors, line, context, unknown=()):
    for name in names:
        value = fields[name]
        if name in unknown and value == "UNKNOWN":
            continue
        if _u64(value) is None:
            _error(errors, line, context, "BAD_U64", "%s is not a lowercase u64" % name)


def _check_decimal(fields, names, errors, line, context, maximum=(1 << 64) - 1):
    for name in names:
        value = _decimal(fields[name])
        if value is None or value > maximum:
            _error(errors, line, context, "BAD_COUNTER", "%s is not an in-range unsigned decimal counter" % name)


def _validate_transfer(f, errors, line, context):
    _check_u32(f, ("reset_epoch", "transfer_sequence", "raw_pi_status", "raw_pi_cart_addr",
                   "raw_pi_dram_addr", "raw_pi_length", "sample_requested_bytes",
                   "before_sample_length", "after_sample_length", "overlap_rdram_start"),
               errors, line, context, unknown=("overlap_rdram_start",))
    _check_u32(f, ("decoded_request_length", "handler_cart_bus_addr",
                   "handler_rdram_phys_addr", "handler_length", "handler_cycles"),
               errors, line, context, unknown=("decoded_request_length", "handler_cart_bus_addr",
               "handler_rdram_phys_addr", "handler_length", "handler_cycles"))
    _check_u64(f, ("obs_sequence", "handler_cart_bus_end_exclusive",
                   "handler_rdram_phys_end_exclusive", "overlap_rdram_end_exclusive"),
               errors, line, context, unknown=("handler_cart_bus_end_exclusive",
               "handler_rdram_phys_end_exclusive", "overlap_rdram_end_exclusive"))
    if _u32(f["reset_epoch"]) == 0 or _u32(f["transfer_sequence"]) == 0 or _u64(f["obs_sequence"]) == 0:
        _error(errors, line, context, "ZERO_IDENTITY", "epoch, observation, and transfer sequences are nonzero")
    mid = TRANSFER_ID.match(f["transfer_id"])
    if not mid or mid.group(1) != f["reset_epoch"][2:] or mid.group(2) != f["transfer_sequence"][2:]:
        _error(errors, line, context, "TRANSFER_ID", "transfer_id does not match epoch/sequence")
    if f["phase"] not in {"BEGIN", "HANDLER_RETURN", "PI_COMPLETE", "REJECTED_BUSY", "REJECTED_UNHANDLED"}:
        _error(errors, line, context, "ENUM", "unsupported phase")
    if f["direction"] not in {"CART_TO_RDRAM", "RDRAM_TO_CART"} or f["status"] not in {"ACCEPTED", "REJECTED_BUSY", "REJECTED_UNHANDLED"}:
        _error(errors, line, context, "ENUM", "unsupported direction or status")
    if f["handler_class"] not in CONCRETE_HANDLERS | {"UNHANDLED", "NOT_SELECTED"} or f["bus_region"] not in REGIONS:
        _error(errors, line, context, "ENUM", "unsupported handler_class or bus_region")
    if f["watch_id"] not in {"AUDIO0", "AUDIO1", "NONE", "UNKNOWN"}:
        _error(errors, line, context, "ENUM", "unsupported watch_id")
    if f["raw_pi_length_register"] not in {"PI_WR_LEN", "PI_RD_LEN"}:
        _error(errors, line, context, "ENUM", "unsupported length register")
    elif f["direction"] in {"CART_TO_RDRAM", "RDRAM_TO_CART"} and (
            (f["direction"] == "CART_TO_RDRAM") != (f["raw_pi_length_register"] == "PI_WR_LEN")):
        _error(errors, line, context, "DIRECTION_REGISTER", "direction and PI length register disagree")
    if not _validate_flags(f["normalization_flags"]):
        _error(errors, line, context, "FLAGS", "normalization flags are malformed or unordered")
    if f["actual_copied_bytes"] != "UNKNOWN":
        _error(errors, line, context, "COPIED_BYTES", "actual_copied_bytes must be UNKNOWN")
    if f["sample_clipped"] not in {"0", "1"}:
        _error(errors, line, context, "SAMPLE", "sample_clipped must be 0 or 1")
    if f["sample_read_status"] not in {"NOT_REQUESTED", "VALID", "SHORT_RDRAM", "INVALID_WINDOW"}:
        _error(errors, line, context, "SAMPLE", "unsupported sample read status")
    before, after = _sample(f["before_sample"]), _sample(f["after_sample"])
    if before is None or after is None:
        _error(errors, line, context, "SAMPLE", "samples must be lowercase hex or -")
    else:
        before_length, after_length = _u32(f["before_sample_length"]), _u32(f["after_sample_length"])
        if len(before) != before_length or len(after) != after_length:
            _error(errors, line, context, "SAMPLE_LENGTH", "declared sample length does not match sample bytes")

    accepted = f["phase"] in {"BEGIN", "HANDLER_RETURN", "PI_COMPLETE"}
    rejected_busy = f["phase"] == "REJECTED_BUSY"
    rejected_unhandled = f["phase"] == "REJECTED_UNHANDLED"
    if accepted:
        if f["status"] != "ACCEPTED" or f["handler_class"] not in CONCRETE_HANDLERS:
            _error(errors, line, context, "PHASE_STATUS", "accepted phase lacks selected concrete handler")
        for name in ("decoded_request_length", "handler_cart_bus_addr", "handler_rdram_phys_addr",
                     "handler_length", "handler_cart_bus_end_exclusive", "handler_rdram_phys_end_exclusive"):
            if f[name] == "UNKNOWN":
                _error(errors, line, context, "MISSING_ACCEPTED_VALUE", "%s is unknown for accepted transfer" % name)
        raw_len, decoded = _u32(f["raw_pi_length"]), _u32(f["decoded_request_length"])
        raw_cart, raw_dram = _u32(f["raw_pi_cart_addr"]), _u32(f["raw_pi_dram_addr"])
        if raw_len is not None and decoded is not None and decoded != (raw_len & 0x00ffffff) + 1:
            _error(errors, line, context, "DECODED_LENGTH", "decoded request length does not derive from raw register")
        cart, dram, hlen = _u32(f["handler_cart_bus_addr"]), _u32(f["handler_rdram_phys_addr"]), _u32(f["handler_length"])
        if cart is not None and raw_cart is not None and cart != (raw_cart & 0xfffffffe):
            _error(errors, line, context, "CART_ALIGNMENT", "handler cart address is not normalized raw cart address")
        if dram is not None and raw_dram is not None and dram != (raw_dram & 0x00fffffe):
            _error(errors, line, context, "DRAM_ALIGNMENT", "handler RDRAM address is not normalized raw DRAM address")
        if cart is not None and hlen is not None and _u64(f["handler_cart_bus_end_exclusive"]) != cart + hlen:
            _error(errors, line, context, "HANDLER_END", "cart exclusive end is incorrect")
        if dram is not None and hlen is not None and _u64(f["handler_rdram_phys_end_exclusive"]) != dram + hlen:
            _error(errors, line, context, "HANDLER_END", "RDRAM exclusive end is incorrect")
        # These are the exact PI controller operations, including unsigned
        # modulo-32 subtraction for a short incoming transfer.
        if raw_len is not None and raw_cart is not None and raw_dram is not None:
            expected_flags = []
            if raw_cart & 1:
                expected_flags.append("CART_ALIGNMENT_MASKED")
            if raw_dram != (raw_dram & 0x00fffffe):
                expected_flags.append("DRAM_ALIGNMENT_MASKED")
            expected_length = (raw_len & 0x00ffffff) + 1
            if expected_length >= 0x7f and expected_length & 1:
                expected_length += 1
                expected_flags.append("ODD_LENGTH_ROUNDED")
            if f["direction"] == "CART_TO_RDRAM" and expected_length <= 0x80:
                expected_length = (expected_length - (raw_dram & 0x00fffffe & 7)) & 0xffffffff
                expected_flags.append("SHORT_TRANSFER_DRAM_OFFSET_SUBTRACTED")
                if expected_length > ((raw_len & 0x00ffffff) + 1):
                    expected_flags.append("SHORT_TRANSFER_UNSIGNED_UNDERFLOW")
            emitted_flags = [] if f["normalization_flags"] == "NONE" else f["normalization_flags"].split(",")
            if f["normalization_flags"] == "NOT_RUN" or emitted_flags != expected_flags:
                _error(errors, line, context, "NORMALIZATION",
                       "flags do not match PI direction/alignment/odd/short semantics")
            if hlen is not None and hlen != expected_length:
                _error(errors, line, context, "HANDLER_LENGTH",
                       "handler length does not match PI odd/short arithmetic")
        expected_regions = {"DD_DOM": {"DD_BUFFER", "DD_ROM"}, "CART_ROM_DOM1": {"CART_ROM"},
                            "CART_ROM_DOM3": {"CART_DOM3"}, "CART_SAVE_DOM2": {"CART_SAVE"}}
        if f["handler_class"] in expected_regions and f["bus_region"] not in expected_regions[f["handler_class"]]:
            _error(errors, line, context, "REGION_HANDLER", "bus_region is incompatible with selected handler")
    elif rejected_busy:
        if f["status"] != "REJECTED_BUSY" or f["handler_class"] != "NOT_SELECTED" or f["bus_region"] != "UNKNOWN":
            _error(errors, line, context, "REJECTED_BUSY", "busy record routing provenance is invalid")
        for name in ("decoded_request_length", "handler_cart_bus_addr", "handler_rdram_phys_addr",
                     "handler_length", "handler_cart_bus_end_exclusive", "handler_rdram_phys_end_exclusive", "handler_cycles"):
            if f[name] != "UNKNOWN":
                _error(errors, line, context, "REJECTED_BUSY", "%s must be UNKNOWN" % name)
        if f["normalization_flags"] != "NOT_RUN":
            _error(errors, line, context, "REJECTED_BUSY", "busy normalization must not run")
    elif rejected_unhandled:
        if f["status"] != "REJECTED_UNHANDLED" or f["handler_class"] != "UNHANDLED":
            _error(errors, line, context, "REJECTED_UNHANDLED", "unhandled provenance is invalid")
        for name in ("decoded_request_length", "handler_cart_bus_addr", "handler_rdram_phys_addr"):
            if f[name] == "UNKNOWN":
                _error(errors, line, context, "REJECTED_UNHANDLED",
                       "%s must retain the pre-routing decoded/normalized value" % name)
        raw_cart, raw_dram = _u32(f["raw_pi_cart_addr"]), _u32(f["raw_pi_dram_addr"])
        if raw_cart is not None and raw_dram is not None:
            decoded, handler_cart, handler_dram = (_u32(f["decoded_request_length"]),
                _u32(f["handler_cart_bus_addr"]), _u32(f["handler_rdram_phys_addr"]))
            raw_length = _u32(f["raw_pi_length"])
            if raw_length is not None and decoded is not None and decoded != (raw_length & 0x00ffffff) + 1:
                _error(errors, line, context, "DECODED_LENGTH",
                       "unhandled decoded request length does not derive from raw register")
            if handler_cart is not None and handler_cart != (raw_cart & 0xfffffffe):
                _error(errors, line, context, "CART_ALIGNMENT",
                       "unhandled cart address is not normalized raw cart address")
            if handler_dram is not None and handler_dram != (raw_dram & 0x00fffffe):
                _error(errors, line, context, "DRAM_ALIGNMENT",
                       "unhandled RDRAM address is not normalized raw DRAM address")
            expected = []
            if raw_cart & 1:
                expected.append("CART_ALIGNMENT_MASKED")
            if raw_dram != (raw_dram & 0x00fffffe):
                expected.append("DRAM_ALIGNMENT_MASKED")
            emitted = [] if f["normalization_flags"] == "NONE" else f["normalization_flags"].split(",")
            if emitted != expected:
                _error(errors, line, context, "NORMALIZATION",
                       "unhandled request must retain its performed alignment flags")
    if not accepted or f["direction"] == "RDRAM_TO_CART":
        zeroes = ("sample_requested_bytes", "before_sample_length", "after_sample_length")
        if any(_u32(f[n]) != 0 for n in zeroes) or f["before_sample"] != "-" or f["after_sample"] != "-" or f["sample_clipped"] != "0":
            _error(errors, line, context, "NON_SAMPLING", "reverse/rejected transfer contains samples")
        if f["watch_state"] != "NOT_APPLICABLE" or f["sample_read_status"] != "NOT_REQUESTED":
            _error(errors, line, context, "NON_SAMPLING", "reverse/rejected watch must be NOT_APPLICABLE")
    elif f["watch_state"] == "AUDIO_OVERLAP":
        requested = _u32(f["sample_requested_bytes"])
        start, end = _u32(f["overlap_rdram_start"]), _u64(f["overlap_rdram_end_exclusive"])
        if f["watch_id"] not in {"AUDIO0", "AUDIO1"} or start is None or end is None or end <= start:
            _error(errors, line, context, "OVERLAP", "audio overlap fields are invalid")
        if f["phase"] == "PI_COMPLETE":
            if (requested != 0 or f["before_sample"] != "-" or f["after_sample"] != "-"
                    or _u32(f["before_sample_length"]) != 0 or _u32(f["after_sample_length"]) != 0
                    or f["sample_read_status"] != "NOT_REQUESTED"):
                _error(errors, line, context, "PHASE_SAMPLE",
                       "PI_COMPLETE has no newly sampled bytes and is NOT_REQUESTED")
        elif requested is None or not 1 <= requested <= 64:
            _error(errors, line, context, "OVERLAP", "audio overlap sample request is invalid")
        if f["phase"] == "BEGIN" and (f["after_sample"] != "-" or _u32(f["after_sample_length"]) != 0):
            _error(errors, line, context, "PHASE_SAMPLE", "BEGIN cannot contain an after sample")
        relevant_lengths = ([_u32(f["before_sample_length"])] if f["phase"] == "BEGIN"
                            else [_u32(f["before_sample_length"]), _u32(f["after_sample_length"])]
                            if f["phase"] == "HANDLER_RETURN" else [])
        if f["phase"] in {"BEGIN", "HANDLER_RETURN"}:
            if f["sample_read_status"] == "VALID" and (requested is None or any(length != requested for length in relevant_lengths)):
                _error(errors, line, context, "SAMPLE_STATUS",
                       "VALID requires complete relevant before/after samples")
            if f["sample_read_status"] == "SHORT_RDRAM" and (requested is None or not any(
                    length is not None and length < requested for length in relevant_lengths)):
                _error(errors, line, context, "SAMPLE_STATUS",
                       "SHORT_RDRAM requires a shorter relevant before/after sample")
            if f["sample_read_status"] in {"NOT_REQUESTED", "INVALID_WINDOW"}:
                _error(errors, line, context, "SAMPLE_STATUS",
                       "an AUDIO_OVERLAP begin/return must report VALID or SHORT_RDRAM")
        if f["phase"] != "PI_COMPLETE" and start is not None and end is not None and dram is not None and hlen is not None:
            handler_end = dram + hlen
            if start < dram or end > handler_end:
                _error(errors, line, context, "OVERLAP", "overlap is outside normalized handler range")
            if requested is not None and requested != min(end - start, 64):
                _error(errors, line, context, "OVERLAP", "requested sample bytes do not match overlap prefix")
            if f["sample_clipped"] != ("1" if end - start > 64 else "0"):
                _error(errors, line, context, "OVERLAP", "sample_clipped does not match overlap length")
    else:
        if f["watch_state"] not in {"NO_AUDIO_WATCH", "NO_OVERLAP", "AUDIO_WATCH_INVALID"}:
            _error(errors, line, context, "WATCH", "unsupported watch state")
        if (any(_u32(f[n]) != 0 for n in ("sample_requested_bytes", "before_sample_length", "after_sample_length"))
                or f["before_sample"] != "-" or f["after_sample"] != "-" or f["sample_clipped"] != "0"
                or f["sample_read_status"] != "NOT_REQUESTED"):
            _error(errors, line, context, "NON_OVERLAP_SAMPLE", "a non-overlap cannot contain a sample")
    if f["completion_join"] != ({"BEGIN": "OPEN", "HANDLER_RETURN": "HANDLER_RETURNED",
                                  "PI_COMPLETE": "JOINED_SAME_EPOCH"}.get(f["phase"], "NOT_APPLICABLE")):
        _error(errors, line, context, "JOIN_STATE", "completion_join does not match phase")


def _validate_session(f, errors, line, context):
    _check_u32(f, ("reset_epoch",), errors, line, context)
    _check_u64(f, ("obs_sequence",), errors, line, context)
    _check_decimal(f, ("pi_history_capacity", "sample_cap_bytes", "active_transfer_cap",
                       "audio_window_cap", "dynamic_staging_window_cap", "early_flush_limit",
                       "zero_audio_flush_limit", "line_cap_bytes"), errors, line, context,
                   maximum=(1 << 32) - 1)
    expected = {"pi_history_capacity": 16, "sample_cap_bytes": 64, "active_transfer_cap": 1,
                "audio_window_cap": 2, "dynamic_staging_window_cap": 0, "early_flush_limit": 7,
                "zero_audio_flush_limit": 1, "line_cap_bytes": LINE_CAP_BYTES}
    for name, value in expected.items():
        if _decimal(f[name]) != value:
            _error(errors, line, context, "SESSION_CAP", "%s must declare %d" % (name, value))
    if _u32(f["reset_epoch"]) == 0 or _u64(f["obs_sequence"]) == 0:
        _error(errors, line, context, "ZERO_IDENTITY", "session identity is zero")
    if f["staging_coverage"] != "BLOCKED_NOT_DELIVERED" or f["publication_coverage"] != "BLOCKED_NOT_DELIVERED":
        _error(errors, line, context, "COVERAGE_ENUM", "unexpected session coverage token")


def _validate_summary(f, errors, line, context):
    _check_u32(f, ("reset_epoch", "retained_transfers", "emitted_transfer_lines",
                   "early_flushes", "zero_audio_flushes"), errors, line, context)
    _check_u64(f, ("obs_sequence", "overwritten_transfers", "observed_attempts",
                   "rejected_busy", "rejected_unhandled", "suppressed_attempts",
                   "other_audio_overlaps", "missing_completion_joins", "truncated_lines"), errors, line, context)
    _check_decimal(f, ("retained_transfer_cap", "early_flush_limit", "zero_audio_flush_limit"),
                   errors, line, context, maximum=(1 << 32) - 1)
    if f["flush_reason"] not in {"ENTRY", "CALL", "ZERO_AUDIO", "UNSPECIFIED"} or f["result_scope"] != "RECENT_RING_ONLY":
        _error(errors, line, context, "SUMMARY", "summary enum/scope is invalid")
    if (_decimal(f["retained_transfer_cap"]) != 16 or _u32(f["retained_transfers"]) is not None and _u32(f["retained_transfers"]) > 16
            or _u32(f["emitted_transfer_lines"]) is not None and _u32(f["emitted_transfer_lines"]) > 49
            or _decimal(f["early_flush_limit"]) != 7 or _decimal(f["zero_audio_flush_limit"]) != 1
            or _u32(f["early_flushes"]) is not None and _u32(f["early_flushes"]) > 7
            or _u32(f["zero_audio_flushes"]) is not None and _u32(f["zero_audio_flushes"]) > 1):
        _error(errors, line, context, "SUMMARY_CAP", "summary exceeds or changes declared cap")
    if f["staging_coverage"] != "BLOCKED_NOT_DELIVERED" or f["publication_coverage"] != "BLOCKED_NOT_DELIVERED":
        _error(errors, line, context, "COVERAGE_ENUM", "unexpected summary coverage token")
    if _u32(f["reset_epoch"]) == 0 or _u64(f["obs_sequence"]) == 0:
        _error(errors, line, context, "ZERO_IDENTITY", "summary identity is zero")


def _parse_v2(payload, line, context, errors):
    pairs, problem = _tokens(payload)
    if problem:
        _error(errors, line, context, "FRAMING", problem)
        return None
    fields = dict(pairs)
    if len(pairs) < 3 or pairs[0] != ("DDPI2", "schema=2") or pairs[1] != ("schema", "2"):
        # DDPI2 schema=2 is intentionally two lexical tokens, not a key/value DDPI2 token.
        # _tokens sees it as DDPI2 schema=2 only when normalised below.
        pass
    return None


def _parse_ddpi2(payload, line, context, errors):
    # The framing has a bare leading DDPI2 token followed by key=value tokens.
    if not payload.startswith("DDPI2 "):
        return None
    try:
        if len(payload.encode("ascii")) > MAX_PAYLOAD_BYTES:
            _error(errors, line, context, "FRAMING", "payload exceeds %d-byte cap" % MAX_PAYLOAD_BYTES)
            return None
    except UnicodeEncodeError:
        _error(errors, line, context, "FRAMING", "non-ASCII payload")
        return None
    body = payload[6:]
    pairs, problem = _tokens(body)
    if problem:
        _error(errors, line, context, "FRAMING", problem)
        return None
    fields = dict(pairs)
    if fields.get("schema") != "2" or "record" not in fields:
        _error(errors, line, context, "SCHEMA", "expected DDPI2 schema=2 and record")
        return None
    kind = fields["record"]
    if kind not in ORDERS:
        _error(errors, line, context, "RECORD", "unknown record type")
        return None
    expected = ("schema", "record") + ORDERS[kind]
    actual = tuple(key for key, _ in pairs)
    if actual != expected:
        _error(errors, line, context, "FIELD_SCHEMA",
               "record keys/order do not match DDPI2 %s schema" % kind)
        return None
    del fields["schema"]
    del fields["record"]
    {"session": _validate_session, "transfer": _validate_transfer, "summary": _validate_summary}[kind](fields, errors, line, context)
    return {"kind": kind, "fields": fields, "line": line, "context": context}


def _parse_legacy(payload, line, context, errors):
    if "DDSTART16" not in payload:
        return None
    fields = dict(re.findall(r"([A-Za-z_][A-Za-z0-9_]*)=([^\s]+)", payload))
    source = fields.get("source")
    # The only trusted legacy incoming semantic is the established source token.
    if source == "DD_PI_CART_TO_RDRAM":
        return {"kind": "legacy", "fields": fields, "line": line, "context": context,
                "trust": "VALID_RETAINED_PI_RECORDS"}
    if source == "DD_PI_DMA_COMPLETION":
        _error(errors, line, context, "LEGACY_AMBIGUOUS_UNTRUSTED",
               "DD_PI_DMA_COMPLETION / decimal-prefixed hex is not corrected by guessing")
    return {"kind": "legacy", "fields": fields, "line": line, "context": context,
            "trust": "AMBIGUOUS_UNTRUSTED"}


def parse_line(line):
    """Compatibility helper for callers which only inspect a DDSTART16 line.

    It intentionally returns no numeric correction for an old decimal-looking
    address: only explicitly ``0x``-prefixed legacy scalars are converted.
    """
    context, payload = _context_and_payload(line)
    record = _parse_legacy(payload, 0, context, [])
    if not record:
        return None
    fields = dict(record["fields"])
    fields["legacy_trust"] = record["trust"]
    for name in ("sequence", "cart_addr", "dram_src", "requested_length"):
        if fields.get(name, "").startswith("0x"):
            try:
                fields[name] = int(fields[name], 16)
            except ValueError:
                return None
    return fields


def parse_capture(lines):
    """Parse lines into records and protocol errors; public for focused tests."""
    records, errors = [], []
    for number, raw in enumerate(lines, 1):
        context, payload = _context_and_payload(raw)
        index = payload.find("DDPI2 ")
        if index >= 0:
            record = _parse_ddpi2(payload[index:], number, context, errors)
        elif re.search(r"\bDDSTART1(?:\s|$)", payload):
            # DDSTART1 is retained only as evidence that an actual legacy
            # identity marker exists.  Its independent schema is not inferred.
            record = {"kind": "identity", "fields": {}, "line": number, "context": context}
        else:
            record = _parse_legacy(payload, number, context, errors)
        if record:
            records.append(record)
    return records, errors


def _cross_validate(records, errors):
    by_session, transfers = {}, {}
    observations, phase_keys, identities = {}, set(), set()
    transfer_numbers = {}
    for record in records:
        if record["kind"] not in {"session", "transfer", "summary"}:
            continue
        f, ctx, line = record["fields"], record["context"], record["line"]
        key = (ctx, f["reset_epoch"])
        sequence = _u64(f["obs_sequence"])
        previous = observations.get(key)
        if previous is not None and sequence is not None and sequence <= previous:
            _error(errors, line, ctx, "OBS_SEQUENCE", "obs_sequence is not strictly increasing in reset epoch")
        observations[key] = sequence if sequence is not None else previous
        if record["kind"] == "session":
            if key in by_session:
                _error(errors, line, ctx, "MIXED_OR_RESTARTED", "duplicate session epoch")
            by_session[key] = record
        elif record["kind"] == "transfer":
            tid = (ctx, f["reset_epoch"], f["transfer_sequence"])
            phase = (tid, f["phase"])
            if phase in phase_keys:
                _error(errors, line, ctx, "DUPLICATE_PHASE", "duplicate epoch/transfer/phase")
            phase_keys.add(phase)
            if tid in identities and f["phase"] == "BEGIN":
                _error(errors, line, ctx, "MIXED_OR_RESTARTED", "transfer identity was reused")
            identities.add(tid)
            transfers.setdefault(tid, []).append(record)
            if f["phase"] in {"BEGIN", "REJECTED_BUSY", "REJECTED_UNHANDLED"}:
                number = _u32(f["transfer_sequence"])
                prior = transfer_numbers.get(key)
                if prior is not None and number is not None and number <= prior:
                    _error(errors, line, ctx, "TRANSFER_SEQUENCE", "transfer sequence is not strictly increasing in reset epoch")
                transfer_numbers[key] = number if number is not None else prior
        elif record["kind"] == "summary":
            if _u64(f["overwritten_transfers"]) or _u64(f["suppressed_attempts"]):
                _error(errors, line, ctx, "RETENTION_EXHAUSTED",
                       "overwritten/suppressed records prevent a complete retained conclusion")
            if _u64(f["truncated_lines"]):
                _error(errors, line, ctx, "TRUNCATED_LINES",
                       "formatter/output truncation prevents a complete retained conclusion")
            if _u64(f["missing_completion_joins"]):
                _error(errors, line, ctx, "MISSING_COMPLETION_JOINS",
                       "PI completions were absent from the retained join evidence")
    for key in observations:
        if key not in by_session:
            _error(errors, 0, key[0], "MISSING_SESSION", "transfer/summary has no session for reset epoch")
    pending_flush_lines = {}
    for record in records:
        if record["kind"] == "transfer":
            key = (record["context"], record["fields"]["reset_epoch"])
            pending_flush_lines.setdefault(key, []).append(record)
            continue
        if record["kind"] != "summary":
            continue
        f, key = record["fields"], (record["context"], record["fields"]["reset_epoch"])
        attempts = [group for tid, group in transfers.items()
                    if tid[:2] == key and group[0]["fields"]["phase"] in
                    {"BEGIN", "REJECTED_BUSY", "REJECTED_UNHANDLED"}]
        busy = sum(group[0]["fields"]["phase"] == "REJECTED_BUSY" for group in attempts)
        unhandled = sum(group[0]["fields"]["phase"] == "REJECTED_UNHANDLED" for group in attempts)
        observed, rejected_busy, rejected_unhandled = (_u64(f["observed_attempts"]),
            _u64(f["rejected_busy"]), _u64(f["rejected_unhandled"]))
        if (observed is not None and observed < len(attempts)
                or rejected_busy is not None and rejected_busy < busy
                or rejected_unhandled is not None and rejected_unhandled < unhandled):
            _error(errors, record["line"], record["context"], "SUMMARY_COUNTER",
                   "lifetime counters are smaller than retained records")
        if (observed is not None and rejected_busy is not None and rejected_unhandled is not None
                and rejected_busy + rejected_unhandled > observed):
            _error(errors, record["line"], record["context"], "SUMMARY_COUNTER",
                   "rejected totals exceed observed attempts")
        flushed = pending_flush_lines.pop(key, [])
        emitted, retained = _u32(f["emitted_transfer_lines"]), _u32(f["retained_transfers"])
        flushed_ids = {(r["fields"]["reset_epoch"], r["fields"]["transfer_sequence"]) for r in flushed}
        if emitted is not None and emitted != len(flushed):
            _error(errors, record["line"], record["context"], "SUMMARY_FLUSH",
                   "emitted_transfer_lines does not equal this flush's transfer lines")
        if retained is not None and retained != len(flushed_ids):
            _error(errors, record["line"], record["context"], "SUMMARY_FLUSH",
                   "retained_transfers does not equal this flush's transfer identities")
        # other_audio_overlaps counts a second candidate audio window that was
        # not selected for sampling.  It has no reconstructible per-line
        # identity, so validate its width above but do not invent a count.
    for tid, group in transfers.items():
        phases = {r["fields"]["phase"]: r for r in group}
        first = group[0]
        if first["fields"]["phase"] in {"REJECTED_BUSY", "REJECTED_UNHANDLED"}:
            if len(group) != 1:
                _error(errors, first["line"], first["context"], "REJECTED_JOIN", "rejected transfer has extra phase")
            continue
        expected = {"BEGIN", "HANDLER_RETURN", "PI_COMPLETE"}
        if set(phases) != expected:
            _error(errors, first["line"], first["context"], "BAD_JOIN", "accepted transfer lacks exact BEGIN/RETURN/COMPLETE phases")
            continue
        phase_observations = [_u64(phases[name]["fields"]["obs_sequence"])
                              for name in ("BEGIN", "HANDLER_RETURN", "PI_COMPLETE")]
        if all(value is not None for value in phase_observations) and phase_observations != sorted(phase_observations):
            _error(errors, first["line"], first["context"], "PHASE_ORDER",
                   "BEGIN, HANDLER_RETURN, and PI_COMPLETE are out of observation order")
        immutable = ("reset_epoch", "transfer_sequence", "transfer_id", "direction", "status",
                     "handler_class", "bus_region", "raw_pi_status", "raw_pi_cart_addr",
                     "raw_pi_dram_addr", "raw_pi_length_register", "raw_pi_length",
                     "decoded_request_length", "normalization_flags", "handler_cart_bus_addr",
                     "handler_rdram_phys_addr", "handler_length", "handler_cart_bus_end_exclusive",
                     "handler_rdram_phys_end_exclusive", "watch_state", "watch_id",
                      "overlap_rdram_start", "overlap_rdram_end_exclusive")
        begin = phases["BEGIN"]["fields"]
        returned = phases["HANDLER_RETURN"]["fields"]
        if (returned["before_sample_length"] != begin["before_sample_length"]
                or returned["before_sample"] != begin["before_sample"]):
            _error(errors, phases["HANDLER_RETURN"]["line"], phases["HANDLER_RETURN"]["context"],
                   "BEFORE_SAMPLE_MISMATCH", "HANDLER_RETURN must retain BEGIN's before sample")
        for phase, record in phases.items():
            if any(record["fields"][name] != begin[name] for name in immutable):
                _error(errors, record["line"], record["context"], "JOIN_MISMATCH",
                       "%s immutable values differ from BEGIN" % phase)
    return by_session, transfers


def analyze_records(records, initial_errors=None, records_only=False):
    """Return a JSON-serializable conclusion and an appropriate process status."""
    errors = list(initial_errors or [])
    sessions, transfers = _cross_validate(records, errors)
    summaries = [r for r in records if r["kind"] == "summary"]
    identities = [r for r in records if r["kind"] == "identity"]
    v2 = [r for r in records if r["kind"] in {"session", "transfer", "summary"}]
    missing = []
    if not v2:
        missing.append("DDPI2_RECORDS")
    if v2 and not sessions:
        missing.append("SESSION")
    if sessions and not summaries:
        missing.append("SUMMARY_OR_BUDGET")
    # A session can legitimately have no transfer, but it cannot establish
    # delivery.  Do not headline this as a successful zero-transfer capture.
    if sessions and not transfers:
        missing.append("RETAINED_TRANSFER_EVIDENCE")
    # A DDSTART1 prefix establishes only that a legacy marker was present; it
    # does not authenticate the complete capture identity.
    missing.append("FULL_IDENTITY_VERIFICATION")
    coverage_blocked = bool(sessions)  # every agreed v2 session advertises C4/C5 blocked
    legacy_trust = [r["trust"] for r in records if r["kind"] == "legacy"]
    # Records-only deliberately excludes non-PI launch identity and C4/C5
    # coverage, but never excludes schema, session, summary, budget, or joins.
    pi_missing = [item for item in missing if item != "FULL_IDENTITY_VERIFICATION"]
    safe_shape = bool(v2) and not errors and not pi_missing
    if records_only:
        status = "VALID_RETAINED_PI_RECORDS" if safe_shape else "INCOMPLETE"
        exit_code = 0 if safe_shape else 2
        scope = "LIMITED_RECORDS_ONLY"
    else:
        status = "INCOMPLETE"
        exit_code = 2
        scope = "RETAINED_PI_ONLY"
    return {
        "result": status,
        "exit_code": exit_code,
        "scope": scope,
        "native_repair": "NOT_ESTABLISHED",
        "raw_full_identity": "PRESENT_UNVALIDATED" if identities else "UNKNOWN",
        "records": {"ddpi2": len(v2), "session": len(sessions), "transfer_lines": len([r for r in records if r["kind"] == "transfer"]),
                    "summary": len(summaries), "legacy": len(legacy_trust)},
        "legacy": legacy_trust,
        "missing_required": missing,
        "coverage": {"C4_dynamic_staging": "BLOCKED_NOT_DELIVERED" if coverage_blocked else "UNKNOWN",
                     "C5_publication": "BLOCKED_NOT_DELIVERED" if coverage_blocked else "UNKNOWN"},
        "errors": errors,
    }


def _window(text):
    """Parse a half-open CLI ``0xlo..0xhi`` window."""
    lo_str, sep, hi_str = text.partition("..")
    if not sep:
        raise argparse.ArgumentTypeError("bad --flag window %r (use 0xlo..0xhi)" % text)
    try:
        lo, hi = int(lo_str.strip(), 0), int(hi_str.strip(), 0)
    except ValueError:
        raise argparse.ArgumentTypeError("bad --flag window %r (use 0xlo..0xhi)" % text)
    if hi <= lo:
        raise argparse.ArgumentTypeError("--flag must be a nonempty half-open window")
    return lo, hi


def window_overlap(span, window):
    """True iff two half-open ranges overlap (adjacent boundaries do not)."""
    return span[0] < window[1] and window[0] < span[1]


def flagged_transfer_ids(records, errors, window):
    """Return each valid incoming transfer once for a delivery-oriented flag."""
    flagged = []
    bad_lines = {error["line"] for error in errors if error["line"]}
    for record in records:
        if record["kind"] != "transfer":
            continue
        fields = record["fields"]
        if (record["line"] in bad_lines or fields["status"] != "ACCEPTED"
                or fields["direction"] != "CART_TO_RDRAM"):
            continue
        start, end = _u32(fields["handler_rdram_phys_addr"]), _u64(fields["handler_rdram_phys_end_exclusive"])
        if start is not None and end is not None and window_overlap((start, end), window):
            if fields["transfer_id"] not in flagged:
                flagged.append(fields["transfer_id"])
    return flagged


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logcat", help="logcat-threadtime.txt or plain fixture")
    parser.add_argument("--flag", type=_window, metavar="0xlo..0xhi",
                        help="compatibility flag window; half-open and reported only")
    parser.add_argument("--records-only", action="store_true",
                        help="validate safely retained PI records only; still fails parse/loss/join errors")
    parser.add_argument("--json", action="store_true", help="emit machine-readable JSON")
    args = parser.parse_args(argv)
    try:
        with open(args.logcat, "r", errors="replace") as stream:
            records, errors = parse_capture(stream)
    except OSError as exc:
        parser.error(str(exc))
    result = analyze_records(records, errors, args.records_only)
    if args.flag:
        result["flag_window_half_open"] = ["0x%x" % args.flag[0], "0x%x" % args.flag[1]]
        result["flag_scope"] = "VALID_ACCEPTED_CART_TO_RDRAM_HANDLER_RANGES_ONLY"
        result["flagged_transfer_ids"] = flagged_transfer_ids(records, result["errors"], args.flag)
    if args.json:
        print(json.dumps(result, sort_keys=True))
    else:
        print("RESULT=%s scope=%s native_repair=%s raw_full_identity=%s" %
              (result["result"], result["scope"], result["native_repair"], result["raw_full_identity"]))
        print("records ddpi2=%d session=%d transfer_lines=%d summary=%d legacy=%d" %
              (result["records"]["ddpi2"], result["records"]["session"], result["records"]["transfer_lines"],
               result["records"]["summary"], result["records"]["legacy"]))
        if args.flag:
            print("FLAG_SCOPE=%s FLAG_HALF_OPEN=%s..%s flagged_transfer_ids=%s" %
                  (result["flag_scope"], result["flag_window_half_open"][0], result["flag_window_half_open"][1],
                   ",".join(result["flagged_transfer_ids"]) or "NONE"))
        for error in result["errors"]:
            print("ERROR line=%s context=%s code=%s %s" %
                  (error["line"], error["context"], error["code"], error["message"]))
        if result["missing_required"]:
            print("MISSING_REQUIRED=" + ",".join(result["missing_required"]))
    return result["exit_code"]


if __name__ == "__main__":
    sys.exit(main())