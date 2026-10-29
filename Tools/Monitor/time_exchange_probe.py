"""Collect four-stamp UDP observations; PC timestamps are software observations.

This tool neither estimates a clock offset nor changes any clock. A PASS means
the bounded collection was clean; independent hardware evidence is still needed.
"""
import argparse
import json
import math
from pathlib import Path
import secrets
import socket
import struct
import sys
import time

from udp_monitor import ipv4, uid_words

WIRE = struct.Struct("!4sBBHIIIIIQ")
REPORT_WIRE = struct.Struct("!4sBBHIIIIIQIIII")
OBSERVATION_WIRE = struct.Struct("!4sBBHIIIIIQQQ")
NSEC_PER_SEC = 1000000000
COUNTERS = ("send_errors", "socket_errors", "invalid", "wrong_source", "wrong_key",
            "wrong_session", "wrong_uid", "wrong_sequence", "wrong_tick", "late",
            "duplicate_ack", "duplicate_report", "invalid_timestamps", "timeouts",
            "missing_ack", "missing_report")


class TimestampError(ValueError):
    """A correctly framed REPORT contains unusable board-domain times."""


def new_session():
    """Use a fresh nonzero 64-bit marker for each collection window."""
    session = 0
    while session == 0:
        session = secrets.randbits(64)
    return session


def encode_request(sequence, tick_ms, uid, session):
    if not (1 <= sequence <= 0xFFFFFFFF and 1 <= session <= 0xFFFFFFFFFFFFFFFF):
        raise ValueError("sequence and session must be nonzero and fit the wire widths")
    if len(uid) != 3 or not any(uid) or any(not 0 <= word <= 0xFFFFFFFF for word in uid):
        raise ValueError("target UID must contain three 32-bit words and be nonzero")
    return WIRE.pack(b"SBCG", 1, 4, WIRE.size, sequence, tick_ms, *uid, session)


def decode_message(data):
    if len(data) not in (WIRE.size, REPORT_WIRE.size):
        raise ValueError("ACK/REPORT payload must be exactly 36/52 bytes")
    magic, version, kind, length, sequence, tick_ms, *tail = WIRE.unpack_from(data)
    uid, session = tuple(tail[:3]), tail[3]
    expected_length = {5: WIRE.size, 6: REPORT_WIRE.size}.get(kind)
    if ((magic, version) != (b"SBCG", 1) or expected_length is None
            or length != expected_length or len(data) != expected_length):
        raise ValueError("unsupported magic/version/type/length")
    if not sequence or not session or not any(uid):
        raise ValueError("zero sequence, session or UID")
    packet = {"kind": kind, "sequence": sequence, "tick_ms": tick_ms,
              "uid": uid, "session": session}
    if kind == 6:
        rx_sec, rx_nsec, tx_sec, tx_nsec = struct.unpack_from("!IIII", data, WIRE.size)
        if rx_nsec >= NSEC_PER_SEC or tx_nsec >= NSEC_PER_SEC:
            raise TimestampError("board nanoseconds must be 0..999999999")
        rx_ns = rx_sec * NSEC_PER_SEC + rx_nsec
        tx_ns = tx_sec * NSEC_PER_SEC + tx_nsec
        if tx_ns < rx_ns:
            raise TimestampError("board TX precedes board RX")
        packet.update(rx_ns=rx_ns, tx_ns=tx_ns)
    return packet


def encode_observation(sequence, tick_ms, uid, session, t1_ns, t4_ns):
    """Submit the original request/first-ACK PC_SW times, never a new reading."""
    if not all(isinstance(value, int) for value in (sequence, tick_ms, session, t1_ns, t4_ns, *uid)):
        raise ValueError("observation key and PC timestamps must be integers")
    if not (0 <= t1_ns <= 0xFFFFFFFFFFFFFFFF and t1_ns <= t4_ns <= 0xFFFFFFFFFFFFFFFF):
        raise ValueError("PC timestamps must be unsigned 64-bit values with t4 at least t1")
    if tick_ms != ((t1_ns // 1000000) & 0xFFFFFFFF):
        raise ValueError("client tick must match the original t1 PC_SW timestamp")
    # Reuse the request identity validation without changing the legacy frame.
    encode_request(sequence, tick_ms, uid, session)
    return OBSERVATION_WIRE.pack(b"SBCG", 1, 7, OBSERVATION_WIRE.size,
                                 sequence, tick_ms, *uid, session, t1_ns, t4_ns)


def run_probe(sock, target, uid, count, interval_s, timeout_s, emit, submit_observation=False):
    """Keep one request pending and observe its entire fixed receive window.

    Continuing to its deadline after a sample permits duplicate detection even
    for the final request. Arrival of any packet never extends this deadline.
    Optional submission reports only local UDP send acceptance, never board receipt.
    """
    uid = tuple(uid)
    session = new_session()
    counters = dict.fromkeys(COUNTERS, 0)
    sent = samples = 0
    observations_submitted = observation_send_errors = 0
    rtt_ns_values = []
    previous_rx_ns = previous_tx_ns = None
    start_ns = next_send_ns = time.monotonic_ns()
    stop_reason = "count"
    identity = {"session": f"{session:016X}", "uid": [f"{word:08X}" for word in uid]}

    def anomaly(event, **details):
        counters[event] += 1
        emit({"event": event, **identity, **details})

    emit({"event": "ready", "bind": sock.getsockname(), "target": target,
          "count": count, "interval_s": interval_s, "timeout_s": timeout_s,
          "hardware_verified": False, **identity,
          **({"submit_observation": True} if submit_observation else {})})
    try:
        for sequence in range(1, count + 1):
            while True:
                remaining_ns = next_send_ns - time.monotonic_ns()
                if remaining_ns <= 0:
                    break
                time.sleep(min(0.05, remaining_ns / NSEC_PER_SEC))
            # Capture t1 before constructing the request, deriving its echoed
            # marker from this exact integer reading rather than a float clock.
            t1_ns = time.monotonic_ns()
            tick_ms = (t1_ns // 1000000) & 0xFFFFFFFF
            data = encode_request(sequence, tick_ms, uid, session)
            deadline_ns = t1_ns + int(timeout_s * NSEC_PER_SEC)
            next_send_ns = t1_ns + int(interval_s * NSEC_PER_SEC)
            try:
                if sock.sendto(data, target) != len(data):
                    raise OSError("incomplete UDP request send")
            except OSError as exc:
                anomaly("send_errors", sequence=sequence, reason=str(exc))
                continue
            sent += 1
            emit({"event": "request", "sequence": sequence, "client_tick_ms": tick_ms,
                  "t1_pc_sw_ns": str(t1_ns), "timestamp_source": "PC_SW", **identity})
            t4_ns = report = None
            sampled = False
            while True:
                remaining_ns = deadline_ns - time.monotonic_ns()
                if remaining_ns <= 0:
                    break
                sock.settimeout(min(0.1, remaining_ns / NSEC_PER_SEC))
                try:
                    reply, address = sock.recvfrom(65535)
                    arrival_ns = time.monotonic_ns()
                except socket.timeout:
                    continue
                except OSError as exc:
                    anomaly("socket_errors", sequence=sequence, reason=str(exc))
                    break
                # Even a socket returning just after its timeout cannot confirm
                # a request past the original total deadline.
                if arrival_ns >= deadline_ns:
                    anomaly("late", sequence=sequence, reason="arrival after request deadline")
                    continue
                if address != target:
                    anomaly("wrong_source", sequence=sequence, source=address, expected=target)
                    continue
                try:
                    packet = decode_message(reply)
                except TimestampError as exc:
                    anomaly("invalid_timestamps", sequence=sequence, reason=str(exc))
                    continue
                except ValueError as exc:
                    anomaly("invalid", sequence=sequence, reason=str(exc))
                    continue
                mismatch = None
                if packet["session"] != session:
                    mismatch = "wrong_session"
                elif packet["uid"] != uid:
                    mismatch = "wrong_uid"
                elif packet["sequence"] != sequence:
                    mismatch = "late" if packet["sequence"] < sequence else "wrong_sequence"
                elif packet["tick_ms"] != tick_ms:
                    mismatch = "wrong_tick"
                if mismatch:
                    counters["wrong_key"] += 1
                    anomaly(mismatch, sequence=sequence, received_sequence=packet["sequence"],
                            received_tick_ms=packet["tick_ms"], received_session=f"{packet['session']:016X}",
                            received_uid=[f"{word:08X}" for word in packet["uid"]])
                    continue
                if packet["kind"] == 5:
                    if t4_ns is not None:
                        anomaly("duplicate_ack", sequence=sequence)
                        continue
                    if arrival_ns < t1_ns:
                        anomaly("invalid_timestamps", sequence=sequence, reason="PC t4 precedes PC t1")
                        continue
                    t4_ns = arrival_ns
                    emit({"event": "ack", "sequence": sequence, "client_tick_ms": tick_ms,
                          "t4_pc_sw_ns": str(t4_ns), "timestamp_source": "PC_SW", **identity})
                else:
                    if report is not None:
                        anomaly("duplicate_report", sequence=sequence)
                        continue
                    if (previous_rx_ns is not None and
                            (packet["rx_ns"] <= previous_rx_ns or packet["tx_ns"] <= previous_tx_ns)):
                        anomaly("invalid_timestamps", sequence=sequence,
                                reason="board RX and TX must each increase across successful samples")
                        continue
                    report = packet
                    emit({"event": "report", "sequence": sequence, "client_tick_ms": tick_ms,
                          "t2_board_hw_ns": str(report["rx_ns"]), "t3_board_hw_ns": str(report["tx_ns"]),
                          "timestamp_source": "BOARD_HW", **identity})
                if not sampled and t4_ns is not None and report is not None:
                    rtt_ns = t4_ns - t1_ns
                    previous_rx_ns, previous_tx_ns = report["rx_ns"], report["tx_ns"]
                    samples += 1
                    sampled = True
                    rtt_ns_values.append(rtt_ns)
                    emit({"event": "four_stamp_sample", "sequence": sequence,
                          "client_tick_ms": tick_ms, **identity,
                          "t1_pc_sw_ns": str(t1_ns), "t2_board_hw_ns": str(report["rx_ns"]),
                          "t3_board_hw_ns": str(report["tx_ns"]), "t4_pc_sw_ns": str(t4_ns),
                          "rtt_pc_sw_ns": str(rtt_ns), "timestamp_sources": {
                              "t1_pc_sw_ns": "PC_SW", "t2_board_hw_ns": "BOARD_HW",
                              "t3_board_hw_ns": "BOARD_HW", "t4_pc_sw_ns": "PC_SW"},
                          "hardware_verified": False})
                    if submit_observation:
                        # Build only from private request/ACK integers. The sample
                        # emitter is allowed to mutate its own presentation data.
                        observation_key = {
                            "session": f"{session:016X}", "uid": [f"{word:08X}" for word in uid],
                            "sequence": sequence, "client_tick_ms": tick_ms,
                            "scope": "local_only", "hardware_verified": False}
                        try:
                            observation = encode_observation(sequence, tick_ms, uid, session, t1_ns, t4_ns)
                            if sock.sendto(observation, target) != len(observation):
                                raise OSError("incomplete UDP observation send")
                        except (OSError, ValueError) as exc:
                            observation_send_errors += 1
                            emit({"event": "observation_send_errors", **observation_key, "reason": str(exc)})
                        else:
                            observations_submitted += 1
                            emit({"event": "observation_submit", **observation_key})
            if not sampled:
                if t4_ns is None:
                    anomaly("missing_ack", sequence=sequence)
                if report is None:
                    anomaly("missing_report", sequence=sequence)
                anomaly("timeouts", sequence=sequence, missing_ack=t4_ns is None, missing_report=report is None)
    except KeyboardInterrupt:
        stop_reason = "interrupted"
    result = {"event": "summary", "requested": count, "sent": sent, "samples": samples,
              "unanswered": sent - samples, **counters, **identity,
              "duration_s": round((time.monotonic_ns() - start_ns) / NSEC_PER_SEC, 3),
              "stop_reason": stop_reason, "hardware_verified": False,
              "rtt_pc_sw_ms_min": round(min(rtt_ns_values) / 1000000, 6) if rtt_ns_values else None,
              "rtt_pc_sw_ms_max": round(max(rtt_ns_values) / 1000000, 6) if rtt_ns_values else None,
              "rtt_pc_sw_ms_mean": round(sum(rtt_ns_values) / len(rtt_ns_values) / 1000000, 6)
              if rtt_ns_values else None,
              "status": "PASS" if samples == count and not any(counters.values())
              and stop_reason == "count" else "REVIEW"}
    if submit_observation:
        result.update(observations_submitted=observations_submitted,
                      observation_send_errors=observation_send_errors)
        if observation_send_errors or observations_submitted != samples:
            result["status"] = "REVIEW"
    emit(result)
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", type=ipv4, default="192.168.10.100")
    parser.add_argument("--port", type=int, default=6007)
    parser.add_argument("--target", type=ipv4, default="192.168.10.1")
    parser.add_argument("--target-port", type=int, default=5007)
    parser.add_argument("--uid", type=uid_words, required=True, help="Target W0:W1:W2 in hex")
    parser.add_argument("--count", type=int, default=30)
    parser.add_argument("--interval", type=float, default=1)
    parser.add_argument("--timeout", type=float, default=0.8)
    parser.add_argument("--submit-observation", action="store_true",
                        help="Send one type-7 PC_SW observation per complete sample; local evidence only")
    parser.add_argument("--log", type=Path)
    args = parser.parse_args(argv)
    if not any(args.uid):
        parser.error("target UID must be nonzero")
    if not (1 <= args.port <= 65535 and 1 <= args.target_port <= 65535 and 1 <= args.count <= 3600):
        parser.error("ports must be 1..65535 and count 1..3600")
    if not (math.isfinite(args.interval) and math.isfinite(args.timeout)
            and 0.1 <= args.interval <= 60 and 0 < args.timeout <= 60):
        parser.error("interval must be 0.1..60 seconds and timeout greater than 0 and at most 60 seconds")
    if args.submit_observation and (args.interval < 1 or args.timeout > 0.8):
        parser.error("observation submission requires interval at least 1 second and timeout at most 0.8 seconds")
    output = None
    try:
        if args.log:
            args.log.parent.mkdir(parents=True, exist_ok=True)
            output = args.log.open("x", encoding="utf-8")

        def emit(record):
            line = json.dumps(record, ensure_ascii=False)
            print(line, flush=True)
            if output:
                output.write(line + "\n")
                output.flush()

        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.bind((args.bind, args.port))
            result = run_probe(sock, (args.target, args.target_port), args.uid, args.count,
                               args.interval, args.timeout, emit, submit_observation=args.submit_observation)
        return 0 if result["status"] == "PASS" else 1
    except OSError as exc:
        print(f"Time exchange probe error: {exc}", file=sys.stderr)
        return 2
    finally:
        if output:
            output.close()


if __name__ == "__main__":
    sys.exit(main())
