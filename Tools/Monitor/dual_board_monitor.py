"""Observe M2.3.1 A/B UDP identities in one bounded, receive-only window.

PASS covers only the received identities and sequence interval. It does not
establish MAC identity, hardware validation, or any form of clock synchronization.
No commands are sent and no host network configuration is changed.
"""
import argparse
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import socket
import sys
import time

if __package__:
    from .udp_monitor import SequenceTracker, decode_packet, ipv4
else:
    from udp_monitor import SequenceTracker, decode_packet, ipv4


BOARD_IDENTITIES = {
    "A": {"source_ip": "192.168.10.1", "uid_words": (0x001A002D, 0x30365105, 0x36363631)},
    "B": {"source_ip": "192.168.10.2", "uid_words": (0x00380050, 0x35385104, 0x37313532)},
}
SOURCE_PORT = 5005
MAX_DURATION_S = 3600
MAX_UNIQUE = 100000


def ascii_uid(words):
    """Represent all UID bits as three fixed-width ASCII hexadecimal words."""
    return [f"{word:08X}" for word in words]


class DualBoardTracker:
    """Keep independent bounded SequenceTrackers and sticky failure counters.

    A pre-baseline sequence can be a delayed packet or a reboot. Since v1 has no
    boot-session identifier, reject that ambiguity rather than silently rebase.
    Reordering inside an established sequence interval remains supported.
    """
    def __init__(self):
        self.trackers = {name: SequenceTracker(max_unique=MAX_UNIQUE) for name in BOARD_IDENTITIES}
        self.board_sequence_errors = {name: 0 for name in BOARD_IDENTITIES}
        self.invalid = self.foreign = self.identity_conflicts = self.sequence_errors = 0

    def observe(self, data, address):
        """Classify a datagram without IO; retain raw bytes even on rejection."""
        record = {"source": address, "length": len(data), "raw_hex": data.hex()}
        try:
            packet = decode_packet(data)
        except ValueError as exc:
            self.invalid += 1
            return {**record, "event": "invalid", "reason": str(exc)}
        record.update(version=1, sequence=packet.sequence, tick_ms=packet.tick_ms,
                      uid=ascii_uid(packet.uid_words))
        board = next((name for name, identity in BOARD_IDENTITIES.items()
                      if packet.uid_words == identity["uid_words"]), None)
        if board is None:
            self.foreign += 1
            return {**record, "event": "foreign", "reason": "unknown UID"}
        record["board"] = board
        expected = (BOARD_IDENTITIES[board]["source_ip"], SOURCE_PORT)
        if address != expected:
            self.identity_conflicts += 1
            return {**record, "event": "identity_conflict", "expected_source": expected,
                    "reason": "known UID does not match expected source IP/port"}
        tracker = self.trackers[board]
        try:
            disposition = tracker.observe(packet)
            if disposition == "before_baseline":
                raise ValueError("possible restart or pre-baseline packet; start a new observation window")
        except ValueError as exc:
            self.sequence_errors += 1
            self.board_sequence_errors[board] += 1
            return {**record, "event": "sequence_error", "reason": str(exc)}
        return {**record, "event": "packet", "disposition": disposition,
                "missing": tracker.summary()["missing"]}

    def summary(self, duration_s, min_packets, stop_reason):
        """Report window evidence without changing or resetting any tracker."""
        boards = {name: {**tracker.summary(), "source_ip": BOARD_IDENTITIES[name]["source_ip"],
                         "source_port": SOURCE_PORT, "uid": ascii_uid(BOARD_IDENTITIES[name]["uid_words"]),
                         "sequence_errors": self.board_sequence_errors[name]}
                  for name, tracker in self.trackers.items()}
        status = "REVIEW"
        if self.identity_conflicts or self.sequence_errors:
            status = "FAIL"
        elif (stop_reason == "duration" and self.invalid == 0 and self.foreign == 0 and
              all(board["unique"] >= min_packets and
                  all(board[counter] == 0 for counter in ("missing", "duplicates", "reordered", "before_baseline"))
                  for board in boards.values())):
            status = "PASS"
        return {"event": "summary", "boards": boards, "invalid": self.invalid,
                "foreign": self.foreign, "identity_conflicts": self.identity_conflicts,
                "sequence_errors": self.sequence_errors, "duration_s": round(duration_s, 6),
                "min_packets_per_board": min_packets, "stop_reason": stop_reason, "status": status,
                "scope": "network identity and observed sequence interval only; no synchronization claim",
                "hardware_verified": False}


def monitor(sock, duration_s=60, min_packets=50, emit=print, *, clock=None):
    """Receive on the caller's socket; caller retains ownership and must close it.

    Inject a monotonic clock and a socket-shaped receiver for offline tests.
    Completion requires the full requested window, not an early packet quota.
    """
    validate_limits(duration_s, min_packets)
    clock = time.monotonic if clock is None else clock
    tracker = DualBoardTracker()
    start_s = clock()
    deadline_s = start_s + duration_s
    stop_reason = "duration"
    emit({"event": "listening", "bind": sock.getsockname(), "duration_s": duration_s,
          "min_packets_per_board": min_packets, "source_port": SOURCE_PORT,
          "identities": {name: {"source_ip": identity["source_ip"],
                                "uid": ascii_uid(identity["uid_words"])}
                         for name, identity in BOARD_IDENTITIES.items()},
          "hardware_verified": False})
    try:
        while True:
            remaining_s = deadline_s - clock()
            if remaining_s <= 0:
                break
            sock.settimeout(min(remaining_s, 0.2))
            try:
                data, address = sock.recvfrom(65535)
            except socket.timeout:
                continue
            received_s = clock()
            # A datagram returned at/after the deadline is outside this window.
            if received_s >= deadline_s:
                break
            record = tracker.observe(data, address)
            emit({**record, "utc": datetime.now(timezone.utc).isoformat(),
                  "elapsed_s": round(received_s - start_s, 6)})
    except KeyboardInterrupt:
        stop_reason = "interrupted"
    result = tracker.summary(clock() - start_s, min_packets, stop_reason)
    emit(result)
    return result


def validate_limits(duration_s, min_packets):
    """Reject nonfinite or unbounded windows and noninteger packet quotas."""
    if not math.isfinite(duration_s) or not 0 < duration_s <= MAX_DURATION_S:
        raise ValueError("duration must be finite and 0 < seconds <= 3600")
    if isinstance(min_packets, bool) or not isinstance(min_packets, int) or not 1 <= min_packets <= MAX_UNIQUE:
        raise ValueError("min-packets must be an integer in 1..100000 per board")


def main(argv=None):
    parser = argparse.ArgumentParser(description="Receive-only dual-board identity monitor; no clock synchronization")
    parser.add_argument("--bind", type=ipv4, default="192.168.10.100")
    parser.add_argument("--port", type=int, default=5005)
    parser.add_argument("--duration", type=float, default=60)
    parser.add_argument("--min-packets", type=int, default=50, help="minimum unique datagrams for EACH board")
    parser.add_argument("--log", type=Path, help="new JSONL path; existing files are never overwritten")
    args = parser.parse_args(argv)
    if not 1 <= args.port <= 65535:
        parser.error("port must be 1..65535")
    try:
        validate_limits(args.duration, args.min_packets)
    except ValueError as exc:
        parser.error(str(exc))
    output = None
    try:
        if args.log:
            args.log.parent.mkdir(parents=True, exist_ok=True)
            output = args.log.open("x", encoding="utf-8")

        def emit(record):
            line = json.dumps(record, ensure_ascii=True)
            print(line, flush=True)
            if output:
                output.write(line + "\n")
                output.flush()

        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.bind((args.bind, args.port))
            result = monitor(sock, args.duration, args.min_packets, emit)
        return 0 if result["status"] == "PASS" else 1
    except OSError as exc:
        print(f"Dual-board monitor error: {exc}", file=sys.stderr)
        return 2
    finally:
        if output:
            output.close()


if __name__ == "__main__":
    sys.exit(main())
