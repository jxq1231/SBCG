"""Deterministic clock/socket boundary tests plus real loopback UDP coverage."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import socket
import struct
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
spec = importlib.util.spec_from_file_location(
    "time_exchange_probe", Path(__file__).resolve().parents[1] / "time_exchange_probe.py")
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)
WIRE = struct.Struct("!4sBBHIIIIIQ")
REPORT = struct.Struct("!4sBBHIIIIIQIIII")
OBSERVATION = struct.Struct("!4sBBHIIIIIQQQ")
UID = (0x001A002D, 0x30365105, 0x36363631)
SESSION = 0xFEDCBA9876543210
TARGET = ("192.168.10.1", 5007)
BASE_NS = 2**54 + 123456789


def reply(request, kind=5, stamps=(20000000, 10, 20000000, 20), **changes):
    fields = list(WIRE.unpack(request))
    fields[2], fields[3] = kind, 52 if kind == 6 else 36
    for index, name in ((4, "sequence"), (5, "tick_ms"), (9, "session")):
        if name in changes:
            fields[index] = changes[name]
    if "uid" in changes:
        fields[6:9] = changes["uid"]
    return REPORT.pack(*fields, *stamps) if kind == 6 else WIRE.pack(*fields)


class FakeClock:
    def __init__(self):
        self.now_ns = BASE_NS
        self.calls = []

    def monotonic_ns(self):
        self.calls.append("clock")
        return self.now_ns

    def sleep(self, seconds):
        self.now_ns += max(1, int(seconds * 1e9))


class FakeSocket:
    """Transport double controls only packet arrival and the host clock."""
    def __init__(self, clock, respond):
        self.clock, self.respond = clock, respond
        self.queue, self.requests, self.arrivals = [], [], []
        self.timeout = 0

    def getsockname(self):
        return ("192.168.10.100", 6007)

    def settimeout(self, seconds):
        self.timeout = seconds

    def sendto(self, data, target):
        self.clock.calls.append("send")
        self.requests.append(data)
        self.queue.extend(self.respond(data))
        return len(data)

    def recvfrom(self, size):
        if not self.queue:
            self.clock.now_ns += max(1, int(self.timeout * 1e9))
            raise socket.timeout()
        item = self.queue.pop(0)
        if isinstance(item, BaseException):
            raise item
        data, address = item
        self.clock.now_ns += 100
        self.arrivals.append(self.clock.now_ns)
        self.clock.calls.append("recv")
        return data, address


class WireTests(unittest.TestCase):
    def test_random_session_retries_zero_and_is_fresh_each_run(self):
        with patch.object(probe.secrets, "randbits", side_effect=[0, SESSION, SESSION - 1]) as random_bits:
            self.assertEqual(probe.new_session(), SESSION)
            self.assertEqual(probe.new_session(), SESSION - 1)
        self.assertEqual(random_bits.call_args_list, [unittest.mock.call(64)] * 3)

    def test_request_envelope_full_key_and_big_endian(self):
        expected = WIRE.pack(b"SBCG", 1, 4, 36, 0x10203040, 0x50607080, *UID, SESSION)
        self.assertEqual(probe.encode_request(0x10203040, 0x50607080, UID, SESSION), expected)

    def test_decode_ack_and_report(self):
        request = WIRE.pack(b"SBCG", 1, 4, 36, 7, 42, *UID, SESSION)
        for kind in (5, 6):
            with self.subTest(kind=kind):
                packet = probe.decode_message(reply(request, kind))
                self.assertIsInstance(packet, dict)
                self.assertEqual((packet["kind"], packet["sequence"], packet["tick_ms"],
                                  packet["uid"], packet["session"]), (kind, 7, 42, UID, SESSION))
                if kind == 6:
                    self.assertEqual((packet["rx_ns"], packet["tx_ns"]),
                                     (20000000000000010, 20000000000000020))

    def test_bad_envelopes_and_zero_identifiers_rejected(self):
        request = WIRE.pack(b"SBCG", 1, 4, 36, 7, 42, *UID, SESSION)
        good = reply(request)
        bad = [good[:-1], good + b"x", b"FAIL" + good[4:],
               good[:4] + b"\x02" + good[5:], request,
               good[:6] + b"\x00\x23" + good[8:],
               reply(request, sequence=0), reply(request, session=0),
               reply(request, uid=(0, 0, 0))]
        for data in bad:
            with self.subTest(data=data), self.assertRaises(ValueError):
                probe.decode_message(data)

    def test_invalid_board_nanoseconds_and_reverse_order_rejected(self):
        request = WIRE.pack(b"SBCG", 1, 4, 36, 7, 42, *UID, SESSION)
        for stamps in ((1, 1000000000, 2, 0), (1, 0, 2, 1000000000), (2, 0, 1, 0)):
            with self.subTest(stamps=stamps), self.assertRaises(ValueError):
                probe.decode_message(reply(request, 6, stamps=stamps))


class PairingTests(unittest.TestCase):
    def run_fake(self, respond, count=1, emit_hook=None):
        clock, events = FakeClock(), []
        sock = FakeSocket(clock, respond)

        def emit(event):
            events.append(event)
            if emit_hook:
                emit_hook(event, clock)

        with patch.object(probe, "time", clock, create=True), \
                patch.object(probe, "new_session", return_value=SESSION, create=True):
            result = probe.run_probe(sock, TARGET, UID, count, 0.001, 0.00001, emit)
        return result, events, sock, clock

    def samples(self, events):
        return [event for event in events if event["event"] == "four_stamp_sample"]

    def test_ack_then_report_is_one_sample(self):
        result, events, sock, _ = self.run_fake(
            lambda request: [(reply(request), TARGET), (reply(request, 6), TARGET)])
        self.assertEqual(result["status"], "PASS")
        sample, = self.samples(events)
        self.assertEqual(sample["t4_pc_sw_ns"], str(sock.arrivals[0]))
        self.assertFalse(result["hardware_verified"])

    def test_report_first_never_supplies_t4(self):
        result, events, sock, _ = self.run_fake(
            lambda request: [(reply(request, 6), TARGET), (reply(request), TARGET)])
        self.assertEqual(result["status"], "PASS")
        sample, = self.samples(events)
        self.assertEqual(sample["t4_pc_sw_ns"], str(sock.arrivals[1]))
        self.assertNotEqual(sample["t4_pc_sw_ns"], str(sock.arrivals[0]))

    def test_clock_read_immediately_follows_every_recv_and_precedes_encoding(self):
        clock, events = FakeClock(), []
        sock = FakeSocket(clock, lambda request: [(reply(request), TARGET), (reply(request, 6), TARGET)])
        original = probe.encode_request

        def encode(*args):
            self.assertEqual(clock.calls[-1], "clock")
            self.assertEqual(args[1], (clock.now_ns // 1000000) & 0xFFFFFFFF)
            clock.calls.append("encode")
            clock.now_ns += 20
            return original(*args)

        with patch.object(probe, "time", clock, create=True), \
                patch.object(probe, "new_session", return_value=SESSION, create=True), \
                patch.object(probe, "encode_request", side_effect=encode):
            result = probe.run_probe(sock, TARGET, UID, 1, 0.001, 0.00001, events.append)
        self.assertEqual(result["status"], "PASS")
        for index, call in enumerate(clock.calls):
            if call == "recv":
                self.assertEqual(clock.calls[index + 1], "clock")
        self.assertEqual(self.samples(events)[0]["t1_pc_sw_ns"], str(BASE_NS))

    def test_large_nanoseconds_session_uid_and_source_labels_are_lossless_json(self):
        result, events, _, _ = self.run_fake(
            lambda request: [(reply(request), TARGET), (reply(request, 6), TARGET)])
        self.assertEqual(result["status"], "PASS")
        sample = json.loads(json.dumps(self.samples(events)[0]))
        for name in ("t1_pc_sw_ns", "t2_board_hw_ns", "t3_board_hw_ns", "t4_pc_sw_ns", "rtt_pc_sw_ns"):
            self.assertIsInstance(sample[name], str)
        self.assertGreater(int(sample["t2_board_hw_ns"]), 2**53)
        self.assertEqual(sample["session"], "FEDCBA9876543210")
        self.assertEqual(sample["uid"], [f"{word:08X}" for word in UID])
        self.assertEqual(sample["timestamp_sources"], {
            "t1_pc_sw_ns": "PC_SW", "t2_board_hw_ns": "BOARD_HW",
            "t3_board_hw_ns": "BOARD_HW", "t4_pc_sw_ns": "PC_SW"})

    def test_cross_domain_clock_values_are_never_compared(self):
        result, events, _, _ = self.run_fake(lambda request: [
            (reply(request, 6, stamps=(0, 1, 0, 2)), TARGET), (reply(request), TARGET)])
        self.assertEqual(result["status"], "PASS")
        self.assertEqual(self.samples(events)[0]["t2_board_hw_ns"], "1")

    def test_each_wrong_full_key_field_and_source_are_counted(self):
        for counter, change in (("wrong_session", {"session": SESSION - 1}),
                                ("wrong_uid", {"uid": (1, 2, 3)}),
                                ("wrong_sequence", {"sequence": 99}),
                                ("wrong_tick", {"tick_ms": 99}),
                                ("wrong_source", {})):
            with self.subTest(counter=counter):
                result, events, _, _ = self.run_fake(lambda request: [
                    (reply(request, **change), ("127.0.0.2", 5007) if counter == "wrong_source" else TARGET),
                    (reply(request), TARGET), (reply(request, 6), TARGET)])
                self.assertEqual(result["samples"], 1)
                self.assertEqual(result[counter], 1)
                self.assertEqual(result["status"], "REVIEW")
                self.assertTrue(any(event["event"] == counter for event in events))

    def test_duplicate_ack_preserves_first_t4_and_reports_review(self):
        result, events, sock, _ = self.run_fake(lambda request: [
            (reply(request), TARGET), (reply(request), TARGET), (reply(request, 6), TARGET)])
        self.assertEqual(result["samples"], 1)
        self.assertEqual(result["duplicate_ack"], 1)
        self.assertEqual(result["status"], "REVIEW")
        self.assertEqual(self.samples(events)[0]["t4_pc_sw_ns"], str(sock.arrivals[0]))

    def test_wrong_report_key_cannot_complete_valid_ack(self):
        for counter, change in (("wrong_session", {"session": SESSION - 1}),
                                ("wrong_uid", {"uid": (1, 2, 3)}),
                                ("wrong_sequence", {"sequence": 99}),
                                ("wrong_tick", {"tick_ms": 99})):
            with self.subTest(counter=counter):
                result, events, _, _ = self.run_fake(lambda request: [
                    (reply(request), TARGET), (reply(request, 6, **change), TARGET)])
                self.assertEqual(result["samples"], 0)
                self.assertEqual(result[counter], 1)
                self.assertEqual(result["missing_report"], 1)
                self.assertEqual(result["status"], "REVIEW")
                self.assertFalse(self.samples(events))

    def test_ack_received_at_deadline_is_late_not_t4(self):
        def near_deadline(event, clock):
            if event["event"] == "request":
                clock.now_ns = BASE_NS + 9900
        result, events, _, _ = self.run_fake(
            lambda request: [(reply(request), TARGET)], emit_hook=near_deadline)
        self.assertEqual(result["samples"], 0)
        self.assertEqual(result["late"], 1)
        self.assertEqual(result["missing_ack"], 1)
        self.assertEqual(result["status"], "REVIEW")
        self.assertFalse(any(event["event"] == "ack" for event in events))

    def test_duplicate_report_after_sample_is_observed(self):
        result, events, _, _ = self.run_fake(lambda request: [
            (reply(request), TARGET), (reply(request, 6), TARGET), (reply(request, 6), TARGET)])
        self.assertEqual(result["samples"], 1)
        self.assertEqual(result["duplicate_report"], 1)
        self.assertEqual(len(self.samples(events)), 1)
        self.assertEqual(result["status"], "REVIEW")

    def test_missing_ack_report_and_both_have_explicit_reasons(self):
        for kind, ack_missing, report_missing in ((5, 0, 1), (6, 1, 0), (None, 1, 1)):
            with self.subTest(kind=kind):
                result, events, _, _ = self.run_fake(
                    lambda request: [] if kind is None else [(reply(request, kind), TARGET)])
                self.assertEqual(result["samples"], 0)
                self.assertEqual((result["missing_ack"], result["missing_report"], result["timeouts"]),
                                 (ack_missing, report_missing, 1))
                self.assertEqual(result["status"], "REVIEW")
                self.assertFalse(self.samples(events))

    def test_invalid_packets_and_board_time_never_make_samples(self):
        for data_kind, counter in (("envelope", "invalid"), ("time", "invalid_timestamps")):
            with self.subTest(kind=data_kind):
                result, events, _, _ = self.run_fake(lambda request: [
                    (b"bad" if data_kind == "envelope" else reply(request, 6, stamps=(2, 0, 1, 0)), TARGET),
                    (reply(request), TARGET)])
                self.assertEqual(result["samples"], 0)
                self.assertEqual(result[counter], 1)
                self.assertFalse(self.samples(events))

    def test_pc_clock_reversal_never_makes_sample(self):
        def rewind(event, clock):
            if event["event"] == "request":
                clock.now_ns = BASE_NS - 1000
        result, events, _, _ = self.run_fake(lambda request: [
            (reply(request), TARGET), (reply(request, 6), TARGET)], emit_hook=rewind)
        self.assertEqual(result["samples"], 0)
        self.assertGreater(result["invalid_timestamps"], 0)
        self.assertEqual(result["status"], "REVIEW")

    def test_board_rx_and_tx_each_must_strictly_increase(self):
        for second_stamps in ((9, 0, 21, 0), (11, 0, 19, 0), (10, 0, 20, 0)):
            with self.subTest(stamps=second_stamps):
                def respond(request):
                    seq = WIRE.unpack(request)[4]
                    stamps = (10, 0, 20, 0) if seq == 1 else second_stamps
                    return [(reply(request), TARGET), (reply(request, 6, stamps=stamps), TARGET)]
                result, events, _, _ = self.run_fake(respond, count=2)
                self.assertEqual(result["samples"], 1)
                self.assertEqual(result["invalid_timestamps"], 1)
                self.assertEqual(result["status"], "REVIEW")

    def test_old_request_packets_are_late_and_never_pair_with_current(self):
        old = []
        def respond(request):
            result = [(reply(old[0]), TARGET), (reply(old[0], 6), TARGET)] if old else []
            old.append(request)
            seq = WIRE.unpack(request)[4]
            return result + [(reply(request), TARGET),
                             (reply(request, 6, stamps=(seq, 1, seq, 2)), TARGET)]
        result, events, _, _ = self.run_fake(respond, count=2)
        self.assertEqual(result["samples"], 2)
        self.assertEqual(result["late"], 2)
        self.assertEqual(result["status"], "REVIEW")
        self.assertEqual([event["sequence"] for event in self.samples(events)], [1, 2])

    def test_deadline_is_total_not_reset_by_invalid_or_reordered_arrivals(self):
        result, events, sock, clock = self.run_fake(lambda request: [(b"bad", TARGET)] * 300)
        self.assertEqual(result["samples"], 0)
        self.assertLessEqual(clock.now_ns - BASE_NS, 10100)
        self.assertLess(len(sock.arrivals), 300)
        self.assertEqual(result["timeouts"], 1)

    def test_send_and_receive_errors_are_explicit_review(self):
        clock = FakeClock()
        for operation in ("sendto", "recvfrom"):
            with self.subTest(operation=operation):
                events = []
                sock = FakeSocket(clock, lambda request: [])
                with patch.object(sock, operation, side_effect=OSError("test IO failure")), \
                        patch.object(probe, "time", clock, create=True), \
                        patch.object(probe, "new_session", return_value=SESSION, create=True):
                    result = probe.run_probe(sock, TARGET, UID, 1, .001, .00001, events.append)
                self.assertEqual(result["status"], "REVIEW")
                self.assertEqual(result["send_errors" if operation == "sendto" else "socket_errors"], 1)


class ObservationTests(unittest.TestCase):
    def run_fake(self, respond=None, enabled=True, send_error=None, emit_hook=None, count=1):
        clock, events, submissions = FakeClock(), [], []
        if respond is None:
            def respond(request):
                sequence = WIRE.unpack(request)[4]
                return [(reply(request), TARGET),
                        (reply(request, 6, stamps=(sequence, 10, sequence, 20)), TARGET)]
        sock = FakeSocket(clock, respond)
        original_send = sock.sendto

        def send(data, target):
            if data[5] == 7:
                submissions.append((data, target))
                if isinstance(send_error, BaseException):
                    raise send_error
                return len(data) if send_error is None else send_error
            return original_send(data, target)

        def emit(event):
            events.append(event)
            if emit_hook:
                emit_hook(event, clock)

        with patch.object(sock, "sendto", side_effect=send), \
                patch.object(probe, "time", clock), \
                patch.object(probe, "new_session", return_value=SESSION):
            args = (sock, TARGET, UID, count, .001, .00001, emit)
            result = probe.run_probe(*args, submit_observation=enabled)
        return result, events, sock, submissions

    def test_encoder_exact_original_integer_stamps_full_key_and_network_order(self):
        tick_ms = (BASE_NS // 1000000) & 0xFFFFFFFF
        expected = OBSERVATION.pack(b"SBCG", 1, 7, 52, 7, tick_ms,
                                    *UID, SESSION, BASE_NS, BASE_NS + 100)
        self.assertEqual(probe.encode_observation(7, tick_ms, UID, SESSION, BASE_NS, BASE_NS + 100),
                         expected)

    def test_encoder_rejects_invalid_identity_tick_and_pc_timestamp_range(self):
        tick_ms = (BASE_NS // 1000000) & 0xFFFFFFFF
        good = [7, tick_ms, UID, SESSION, BASE_NS, BASE_NS + 100]
        bad_values = ((0, 0), (0, 2**32), (1, tick_ms ^ 1), (1, -1),
                      (2, (0, 0, 0)), (2, (1, 2)), (2, (1, 2, 2**32)),
                      (3, 0), (3, 2**64), (4, -1), (4, 2**64),
                      (5, -1), (5, 2**64), (5, BASE_NS - 1))
        for index, value in bad_values:
            args = good.copy()
            args[index] = value
            with self.subTest(index=index, value=value), self.assertRaises(ValueError):
                probe.encode_observation(*args)

    def test_encoder_allows_unsigned_boundary_and_equal_pc_times(self):
        for t1_ns in (0, 2**63, 2**64 - 1):
            with self.subTest(t1=t1_ns):
                tick_ms = (t1_ns // 1000000) & 0xFFFFFFFF
                data = probe.encode_observation(0xFFFFFFFF, tick_ms, UID,
                                                2**64 - 1, t1_ns, t1_ns)
                self.assertEqual(OBSERVATION.unpack(data)[-2:], (t1_ns, t1_ns))

    def test_decoder_still_rejects_type7_as_response(self):
        packet = OBSERVATION.pack(b"SBCG", 1, 7, 52, 7, 42, *UID, SESSION, 42_000_000, 42_000_100)
        with self.assertRaises(ValueError):
            probe.decode_message(packet)

    def test_default_mode_preserves_ready_summary_schema_and_never_sends_type7(self):
        result, events, sock, submissions = self.run_fake(enabled=False)
        self.assertFalse(submissions)
        self.assertEqual(len(sock.requests), 1)
        self.assertNotIn("submit_observation", events[0])
        self.assertNotIn("observations_submitted", result)
        self.assertNotIn("observation_send_errors", result)
        self.assertEqual([event["event"] for event in events],
                         ["ready", "request", "ack", "report", "four_stamp_sample", "summary"])
        expected_keys = {"event", "requested", "sent", "samples", "unanswered", "session", "uid",
                         "duration_s", "stop_reason", "hardware_verified", "rtt_pc_sw_ms_min",
                         "rtt_pc_sw_ms_max", "rtt_pc_sw_ms_mean", "status", *probe.COUNTERS}
        self.assertEqual(set(result), expected_keys)

    def test_ack_first_and_report_first_use_first_ack_not_report_or_submit_time(self):
        for order in ((5, 6), (6, 5)):
            with self.subTest(order=order):
                result, events, sock, submissions = self.run_fake(
                    lambda request: [(reply(request, kind), TARGET) for kind in order])
                data, target = submissions[0]
                request = WIRE.unpack(sock.requests[0])
                fields = OBSERVATION.unpack(data)
                self.assertEqual(fields[:2], request[:2])
                self.assertEqual(fields[2:4], (7, 52))
                self.assertEqual(fields[4:10], request[4:10])
                self.assertEqual(fields[-2:], (BASE_NS, sock.arrivals[order.index(5)]))
                self.assertEqual(target, TARGET)
                self.assertEqual(len(submissions), 1)
                self.assertEqual(result["observations_submitted"], 1)
                self.assertEqual(result["observation_send_errors"], 0)
                self.assertEqual(result["status"], "PASS")
                self.assertTrue(events[0]["submit_observation"])
                sample_index = next(i for i, event in enumerate(events)
                                    if event["event"] == "four_stamp_sample")
                submitted = events[sample_index + 1]
                self.assertEqual(submitted["event"], "observation_submit")
                self.assertEqual(submitted["scope"], "local_only")
                self.assertFalse(submitted["hardware_verified"])
                for key in ("session", "uid", "sequence", "client_tick_ms"):
                    self.assertEqual(submitted[key], events[sample_index][key])

    def test_two_requests_each_submit_once_with_same_session_and_own_key(self):
        result, _, sock, submissions = self.run_fake(count=2)
        self.assertEqual(result["observations_submitted"], 2)
        for (data, _), request in zip(submissions, sock.requests):
            self.assertEqual(OBSERVATION.unpack(data)[4:10], WIRE.unpack(request)[4:10])

    def test_duplicate_ack_and_report_do_not_resubmit_or_replace_first_ack(self):
        result, _, sock, submissions = self.run_fake(lambda request: [
            (reply(request), TARGET), (reply(request), TARGET),
            (reply(request, 6), TARGET), (reply(request, 6), TARGET)])
        self.assertEqual(len(submissions), 1)
        self.assertEqual(OBSERVATION.unpack(submissions[0][0])[-1], sock.arrivals[0])
        self.assertEqual(result["samples"], 1)
        self.assertEqual(result["status"], "REVIEW")

    def test_emitter_mutation_cannot_change_wire_pc_times_or_request_key(self):
        def mutate(event, clock):
            if event["event"] == "four_stamp_sample":
                event.update(t1_pc_sw_ns="0", t4_pc_sw_ns="1", session="0", sequence=999)
                event["uid"][0] = "00000000"
                clock.now_ns += 1000
        result, _, sock, submissions = self.run_fake(emit_hook=mutate)
        fields = OBSERVATION.unpack(submissions[0][0])
        self.assertEqual(fields[4:10], WIRE.unpack(sock.requests[0])[4:10])
        self.assertEqual(fields[-2:], (BASE_NS, sock.arrivals[0]))
        self.assertEqual(result["status"], "PASS")

    def test_submit_exception_and_short_send_keep_sample_review_no_retry(self):
        for failure in (OSError("submit failed"), 51):
            with self.subTest(failure=failure):
                result, events, _, submissions = self.run_fake(lambda request: [
                    (reply(request), TARGET), (reply(request, 6), TARGET),
                    (reply(request), TARGET), (reply(request, 6), TARGET)], send_error=failure)
                self.assertEqual(len(submissions), 1)
                self.assertEqual(result["samples"], 1)
                self.assertEqual(result["observations_submitted"], 0)
                self.assertEqual(result["observation_send_errors"], 1)
                self.assertEqual(result["send_errors"], 0)
                self.assertEqual(result["status"], "REVIEW")
                error, = [event for event in events if event["event"] == "observation_send_errors"]
                self.assertTrue(error["reason"])
                self.assertEqual(error["sequence"], 1)
                self.assertEqual(error["session"], f"{SESSION:016X}")
                self.assertEqual(error["uid"], [f"{word:08X}" for word in UID])
                self.assertEqual(error["client_tick_ms"], (BASE_NS // 1000000) & 0xFFFFFFFF)
                self.assertEqual(error["scope"], "local_only")
                self.assertFalse(error["hardware_verified"])

    def test_interrupted_after_sample_before_submission_is_review_with_missing_count(self):
        def interrupt(event, clock):
            if event["event"] == "four_stamp_sample":
                raise KeyboardInterrupt()
        result, _, _, submissions = self.run_fake(emit_hook=interrupt)
        self.assertFalse(submissions)
        self.assertEqual(result["samples"], 1)
        self.assertEqual(result["observations_submitted"], 0)
        self.assertEqual(result["observation_send_errors"], 0)
        self.assertEqual(result["status"], "REVIEW")

    def test_incomplete_invalid_foreign_wrong_key_never_submit(self):
        cases = [lambda request: [],
                 lambda request: [(reply(request), TARGET)],
                 lambda request: [(reply(request, 6), TARGET)],
                 lambda request: [(reply(request), ("127.0.0.1", 999)), (reply(request, 6), TARGET)],
                 lambda request: [(reply(request, session=SESSION - 1), TARGET), (reply(request, 6), TARGET)],
                 lambda request: [(reply(request, uid=(1, 2, 3)), TARGET), (reply(request, 6), TARGET)],
                 lambda request: [(reply(request, sequence=2), TARGET), (reply(request, 6), TARGET)],
                 lambda request: [(reply(request, tick_ms=0), TARGET), (reply(request, 6), TARGET)],
                 lambda request: [(b"invalid", TARGET), (reply(request), TARGET)],
                 lambda request: [(reply(request, 6, stamps=(2, 0, 1, 0)), TARGET), (reply(request), TARGET)]]
        for index, respond in enumerate(cases):
            with self.subTest(case=index):
                result, _, _, submissions = self.run_fake(respond)
                self.assertFalse(submissions)
                self.assertEqual(result["observations_submitted"], 0)
                self.assertEqual(result["status"], "REVIEW")

    def test_cli_flag_forwarding_default_off_and_explicit_mode_timing_limits(self):
        base = ["--bind", "127.0.0.1", "--uid", ":".join(f"{word:08X}" for word in UID)]
        for enabled in (False, True):
            with self.subTest(enabled=enabled), patch.object(probe, "run_probe", return_value={"status": "PASS"}) as run, \
                    patch.object(probe.socket, "socket"):
                self.assertEqual(probe.main(base + (["--submit-observation"] if enabled else [])), 0)
                self.assertEqual(run.call_args.kwargs.get("submit_observation", False), enabled)
        for timing in (["--interval", ".5"], ["--timeout", ".81"]):
            with self.subTest(timing=timing), contextlib.redirect_stderr(io.StringIO()), \
                    self.assertRaises(SystemExit):
                probe.main(base + ["--submit-observation"] + timing)

    def test_real_loopback_submission_contains_original_four_stamp_sample(self):
        events, errors, submissions = [], [], []
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server, \
                socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
            server.bind(("127.0.0.1", 0))
            server.settimeout(2)
            client.bind(("127.0.0.1", 0))
            def serve():
                try:
                    request, address = server.recvfrom(100)
                    server.sendto(reply(request, 6), address)
                    server.sendto(reply(request), address)
                    data, submit_address = server.recvfrom(100)
                    submissions.append((request, data, address, submit_address))
                except BaseException as exc:
                    errors.append(exc)
            worker = threading.Thread(target=serve, daemon=True)
            worker.start()
            result = probe.run_probe(client, server.getsockname(), UID, 1, .02, .03,
                                     events.append, submit_observation=True)
            worker.join(2)
            self.assertFalse(worker.is_alive())
            self.assertFalse(errors, errors)
        request, data, address, submit_address = submissions[0]
        self.assertEqual(address, submit_address)
        self.assertEqual(OBSERVATION.unpack(data)[4:10], WIRE.unpack(request)[4:10])
        sample, = [event for event in events if event["event"] == "four_stamp_sample"]
        self.assertEqual(OBSERVATION.unpack(data)[-2:],
                         (int(sample["t1_pc_sw_ns"]), int(sample["t4_pc_sw_ns"])))
        self.assertEqual(result["status"], "PASS")
        self.assertFalse(result["hardware_verified"])


class LocalSocketTests(unittest.TestCase):
    def test_real_loopback_reordered_ack_report_three_requests(self):
        events, errors, sessions = [], [], []
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server, \
                socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
            server.bind(("127.0.0.1", 0))
            server.settimeout(2)
            client.bind(("127.0.0.1", 0))
            def serve():
                try:
                    for expected in range(1, 4):
                        request, address = server.recvfrom(100)
                        fields = WIRE.unpack(request)
                        self.assertEqual(fields[:5], (b"SBCG", 1, 4, 36, expected))
                        self.assertEqual(fields[6:9], UID)
                        sessions.append(fields[9])
                        for kind in ((6, 5) if expected % 2 else (5, 6)):
                            server.sendto(reply(request, kind, stamps=(expected, 10, expected, 20)), address)
                except BaseException as exc:
                    errors.append(exc)
            worker = threading.Thread(target=serve, daemon=True)
            worker.start()
            result = probe.run_probe(client, server.getsockname(), UID, 3, .02, .01, events.append)
            worker.join(2)
            self.assertFalse(worker.is_alive())
            self.assertFalse(errors, errors)
        self.assertEqual(result["status"], "PASS")
        self.assertEqual(result["samples"], 3)
        self.assertEqual(len(set(sessions)), 1)
        self.assertGreater(sessions[0], 0)
        self.assertFalse(result["hardware_verified"])

    def test_cli_uid_required_and_all_zero_uid_rejected(self):
        for args in ([], ["--uid", "00000000:00000000:00000000"]):
            with self.subTest(args=args), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                probe.main(args)

    def test_cli_local_jsonl_output_and_exclusive_log_creation(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "capture.jsonl"
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server, \
                    socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reserve:
                server.bind(("127.0.0.1", 0))
                server.settimeout(2)
                reserve.bind(("127.0.0.1", 0))
                port = reserve.getsockname()[1]
                reserve.close()
                errors = []
                def serve():
                    try:
                        request, address = server.recvfrom(100)
                        server.sendto(reply(request), address)
                        server.sendto(reply(request, 6), address)
                    except BaseException as exc:
                        errors.append(exc)
                worker = threading.Thread(target=serve, daemon=True)
                worker.start()
                args = ["--bind", "127.0.0.1", "--port", str(port), "--target", "127.0.0.1",
                        "--target-port", str(server.getsockname()[1]), "--uid", ":".join(f"{x:08X}" for x in UID),
                        "--count", "1", "--timeout", "0.03", "--log", str(path)]
                with contextlib.redirect_stdout(io.StringIO()):
                    code = probe.main(args)
                worker.join(2)
                self.assertFalse(worker.is_alive())
                self.assertFalse(errors, errors)
            self.assertEqual(code, 0)
            records = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
            self.assertEqual(records[-1]["status"], "PASS")
            self.assertEqual(len([row for row in records if row["event"] == "four_stamp_sample"]), 1)
            before = path.read_bytes()
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(probe.main(args), 2)
            self.assertEqual(path.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
