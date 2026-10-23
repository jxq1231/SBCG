"""Receive SBCG v1 UDP observations; host results never imply hardware PASS."""
from dataclasses import dataclass
from datetime import datetime, timezone
import argparse
import ipaddress
import json
from pathlib import Path
import socket
import struct
import sys
import time

WIRE = struct.Struct("!4sBBHIIIII")
MASK = 0xFFFFFFFF
HALF = 0x80000000


@dataclass(frozen=True)
class Packet:
    sequence: int
    tick_ms: int
    uid_words: tuple


def decode_packet(data):
    if len(data) != WIRE.size:
        raise ValueError(f"payload length {len(data)} != {WIRE.size}")
    magic, version, kind, length, sequence, tick_ms, *uid = WIRE.unpack(data)
    if (magic, version, kind, length) != (b"SBCG", 1, 1, WIRE.size):
        raise ValueError("unsupported magic/version/type/length")
    return Packet(sequence, tick_ms, tuple(uid))


class SequenceTracker:
    """One UID and one boot per bounded observation window, not an ACK protocol."""
    def __init__(self, max_unique=100000):
        self.max_unique = max_unique
        self.seen = {}
        self.uid = self.first = self.highest = self.high_tick = None
        self.received = self.duplicates = self.reordered = self.before_baseline = 0
        self.cadence_samples = 0
        self.tick_step_min = self.tick_step_max = None

    def observe(self, packet):
        if self.first is None:
            self.uid = packet.uid_words
            self.first = self.highest = packet.sequence
            self.high_tick = packet.tick_ms
        if packet.uid_words != self.uid:
            raise ValueError("UID changed; start a separate observation window")
        diff = (packet.sequence - (self.highest & MASK)) & MASK
        if diff == HALF:
            raise ValueError("ambiguous half-range sequence jump")
        absolute = self.highest + (diff if diff < HALF else diff - (MASK + 1))
        if absolute - self.first >= HALF:
            raise ValueError("sequence span exceeds supported experiment window")
        tick_diff = (packet.tick_ms - self.high_tick) & MASK
        if (absolute > self.highest and tick_diff >= HALF) or (
                absolute < self.highest and 0 < tick_diff < HALF):
            raise ValueError("sequence/tick disagree; possible reset, restart monitor")
        if absolute in self.seen and packet.tick_ms != self.seen[absolute]:
            raise ValueError("same sequence has different tick; possible reset, restart monitor")
        if absolute >= self.first and absolute not in self.seen and len(self.seen) >= self.max_unique:
            raise ValueError("unique packet limit reached; start another capture")
        self.received += 1
        if absolute < self.first:
            self.before_baseline += 1
            return "before_baseline"
        if absolute in self.seen:
            self.duplicates += 1
            return "duplicate"
        event = "in_order"
        if absolute < self.highest:
            self.reordered += 1
            event = "reordered"
        self.seen[absolute] = packet.tick_ms
        if absolute > self.highest:
            if absolute == self.highest + 1:
                self.cadence_samples += 1
                self.tick_step_min = tick_diff if self.tick_step_min is None else min(self.tick_step_min, tick_diff)
                self.tick_step_max = tick_diff if self.tick_step_max is None else max(self.tick_step_max, tick_diff)
            self.highest, self.high_tick = absolute, packet.tick_ms
        return event

    def summary(self):
        expected = 0 if self.first is None else self.highest - self.first + 1
        missing = expected - len(self.seen)
        return {"received": self.received, "unique": len(self.seen),
                "first_sequence": self.first, "highest_sequence":
                None if self.highest is None else self.highest & MASK,
                "sequence_span": expected, "missing": missing,
                "gap_percent": 100 * missing / expected if expected else None,
                "duplicates": self.duplicates, "reordered": self.reordered,
                "before_baseline": self.before_baseline, "cadence_samples": self.cadence_samples,
                "tick_step_ms_min": self.tick_step_min, "tick_step_ms_max": self.tick_step_max}


def monitor(sock, expected_ip, source_port, expected_uid, duration_s, min_packets, emit):
    tracker = SequenceTracker()
    invalid = foreign = 0
    stop_reason = "duration"
    start = time.monotonic()
    deadline = start + duration_s
    emit({"event": "listening", "bind": sock.getsockname(), "source_ip": expected_ip,
          "source_port": source_port, "duration_s": duration_s})
    try:
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            sock.settimeout(min(remaining, 0.2))
            try:
                data, address = sock.recvfrom(65535)
            except socket.timeout:
                continue
            if address != (expected_ip, source_port):
                foreign += 1
                continue
            try:
                packet = decode_packet(data)
            except ValueError as exc:
                invalid += 1
                emit({"event": "invalid", "reason": str(exc), "bytes": len(data)})
                continue
            if expected_uid is not None and packet.uid_words != expected_uid:
                foreign += 1
                continue
            try:
                disposition = tracker.observe(packet)
            except ValueError as exc:
                stop_reason = str(exc)
                emit({"event": "stopped", "reason": stop_reason})
                break
            emit({"event": "packet", "utc": datetime.now(timezone.utc).isoformat(),
                  "elapsed_s": round(time.monotonic() - start, 6), "source": address,
                  "version": 1, "length": len(data), "sequence": packet.sequence,
                  "tick_ms": packet.tick_ms, "uid": [f"{w:08X}" for w in packet.uid_words],
                  "disposition": disposition, "missing": tracker.summary()["missing"]})
    except KeyboardInterrupt:
        stop_reason = "interrupted"
    summary = {"event": "summary", **tracker.summary(), "invalid": invalid, "foreign": foreign,
               "duration_s": round(time.monotonic() - start, 3), "stop_reason": stop_reason,
               "status": "REVIEW", "scope": "observed sequence interval only",
               "hardware_verified": False}
    if summary["unique"] >= min_packets and summary["missing"] == 0 and invalid == 0 and stop_reason == "duration":
        summary["status"] = "PASS"
    # PASS describes this receiver window. Only user-supplied board evidence
    # can establish that these datagrams came from the intended firmware.
    emit(summary)
    return summary


def ipv4(text):
    try:
        return str(ipaddress.IPv4Address(text))
    except ipaddress.AddressValueError as exc:
        raise argparse.ArgumentTypeError(str(exc)) from exc


def uid_words(text):
    try:
        parts = tuple(int(w, 16) for w in text.split(":"))
        if len(parts) != 3 or any(len(w) != 8 for w in text.split(":")) or any(w < 0 or w > MASK for w in parts):
            raise ValueError
        return parts
    except ValueError as exc:
        raise argparse.ArgumentTypeError("UID needs three 8-digit hex words separated by colons") from exc


def main(argv=None):
    parser = argparse.ArgumentParser(description="SBCG v1 UDP receiver and sequence-gap monitor")
    parser.add_argument("--bind", type=ipv4, default="192.168.10.100")
    parser.add_argument("--port", type=int, default=5005)
    parser.add_argument("--source", type=ipv4, default="192.168.10.1")
    parser.add_argument("--source-port", type=int, default=5005)
    parser.add_argument("--uid", type=uid_words, default=None,
                        help="W0:W1:W2 in hex; otherwise lock to the first valid packet UID")
    parser.add_argument("--duration", type=float, default=60)
    parser.add_argument("--min-packets", type=int, default=30)
    parser.add_argument("--log", type=Path, help="Optional JSONL path, for example Logs/UDP/baseline.jsonl")
    args = parser.parse_args(argv)
    if not (1 <= args.port <= 65535 and 1 <= args.source_port <= 65535):
        parser.error("ports must be 1..65535")
    if not (0 < args.duration <= 3600 and 1 <= args.min_packets <= 100000):
        parser.error("duration must be 0..3600 seconds, min-packets 1..100000")
    output = None
    try:
        if args.log:
            args.log.parent.mkdir(parents=True, exist_ok=True)
            # Preserve previous experiment data; choose a new filename per run.
            output = args.log.open("x", encoding="utf-8")

        def emit(record):
            line = json.dumps(record, ensure_ascii=False)
            print(line, flush=True)
            if output:
                output.write(line + "\n")
                output.flush()

        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.bind((args.bind, args.port))
            result = monitor(sock, args.source, args.source_port, args.uid,
                             args.duration, args.min_packets, emit)
        return 0 if result["status"] == "PASS" else 1
    except OSError as exc:
        print(f"UDP monitor error: {exc}", file=sys.stderr)
        return 2
    finally:
        if output:
            output.close()


if __name__ == "__main__":
    sys.exit(main())
