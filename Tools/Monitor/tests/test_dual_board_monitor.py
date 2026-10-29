"""Dual-board identity window tests; no hardware or real network access."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import socket
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

MONITOR_DIR = Path(__file__).resolve().parents[1]
if str(MONITOR_DIR) not in sys.path:
    sys.path.insert(0, str(MONITOR_DIR))
spec = importlib.util.spec_from_file_location("dual_board_monitor", MONITOR_DIR / "dual_board_monitor.py")
dual = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = dual
spec.loader.exec_module(dual)

UID_A = (0x001A002D, 0x30365105, 0x36363631)
UID_B = (0x00380050, 0x35385104, 0x37313532)
ADDR_A = ("192.168.10.1", 5005)
ADDR_B = ("192.168.10.2", 5005)


def packet(seq, tick=None, uid=UID_A):
    return struct.pack("!4sBBHIIIII", b"SBCG", 1, 1, 28, seq,
                       (seq * 1000) & 0xFFFFFFFF if tick is None else tick, *uid)


class FakeClock:
    def __init__(self):
        self.now = 0.0

    def __call__(self):
        return self.now


class FakeSocket:
    def __init__(self, clock, packets=()):
        self.clock = clock
        self.packets = iter(packets)
        self.closed = False
        self.bound = None
        self.timeout = 0.0

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.closed = True

    def bind(self, address):
        self.bound = address

    def getsockname(self):
        return self.bound or ("192.168.10.100", 5005)

    def settimeout(self, timeout):
        self.timeout = timeout

    def recvfrom(self, _):
        item = next(self.packets, None)
        if isinstance(item, BaseException):
            raise item
        if item is None:
            self.clock.now += self.timeout
            raise socket.timeout
        self.clock.now += 0.001
        return item


def capture(packets, duration_s=1, min_packets=2):
    clock = FakeClock()
    sock = FakeSocket(clock, packets)
    events = []
    result = dual.monitor(sock, duration_s, min_packets, events.append, clock=clock)
    return result, events


class DualBoardTests(unittest.TestCase):
    def test_interleaved_boards_have_separate_sequence_and_tick_statistics(self):
        rows = [(packet(10), ADDR_A), (packet(900, 20, UID_B), ADDR_B),
                (packet(11), ADDR_A), (packet(901, 1021, UID_B), ADDR_B)]
        result, events = capture(rows)
        self.assertEqual(result["status"], "PASS")
        self.assertEqual(result["foreign"], 0)
        self.assertEqual(result["boards"]["A"]["unique"], 2)
        self.assertEqual(result["boards"]["B"]["first_sequence"], 900)
        self.assertEqual(result["boards"]["A"]["tick_step_ms_min"], 1000)
        self.assertEqual(result["boards"]["B"]["tick_step_ms_min"], 1001)
        self.assertFalse(result["hardware_verified"])
        self.assertIn("identity", result["scope"])
        record = next(e for e in events if e["event"] == "packet")
        self.assertEqual(record["raw_hex"], rows[0][0].hex())
        self.assertEqual(record["uid"], ["001A002D", "30365105", "36363631"])
        self.assertEqual(record["source"], ADDR_A)

    def test_crossed_ip_uid_and_known_uid_on_unknown_ip_are_identity_conflicts(self):
        for address, uid in ((ADDR_A, UID_B), (ADDR_B, UID_A),
                             (("192.168.10.55", 5005), UID_A)):
            with self.subTest(address=address, uid=uid):
                result, events = capture([(packet(1, uid=uid), address)], min_packets=1)
                self.assertEqual(result["identity_conflicts"], 1)
                self.assertEqual(result["status"], "FAIL")
                self.assertEqual(events[1]["event"], "identity_conflict")

    def test_source_port_must_match(self):
        result, events = capture([(packet(1), (ADDR_A[0], 5006))])
        self.assertEqual(result["identity_conflicts"], 1)
        self.assertEqual(result["boards"]["A"]["unique"], 0)
        self.assertEqual(result["status"], "FAIL")

    def test_unknown_uid_or_source_is_recorded_without_polluting_trackers(self):
        for address in (ADDR_A, ("192.168.10.99", 5005)):
            with self.subTest(address=address):
                result, events = capture([(packet(1, uid=(1, 2, 3)), address)])
                self.assertEqual(result["foreign"], 1)
                self.assertEqual(events[1]["event"], "foreign")
                self.assertIn("raw_hex", events[1])
                self.assertEqual(result["boards"]["A"]["unique"], 0)

    def test_bad_packet_is_recorded_with_original_bytes(self):
        result, events = capture([(b"broken", ADDR_B)])
        self.assertEqual(result["invalid"], 1)
        self.assertEqual(events[1]["raw_hex"], b"broken".hex())
        self.assertEqual(events[1]["event"], "invalid")
        self.assertNotEqual(result["status"], "PASS")

    def test_loss_is_not_pass_and_only_affected_board_has_gap(self):
        result, _ = capture([(packet(1), ADDR_A), (packet(3), ADDR_A),
                             (packet(8, uid=UID_B), ADDR_B), (packet(9, uid=UID_B), ADDR_B)])
        self.assertEqual(result["boards"]["A"]["missing"], 1)
        self.assertEqual(result["boards"]["B"]["missing"], 0)
        self.assertEqual(result["status"], "REVIEW")

    def test_duplicates_and_reordering_can_fill_gap_without_extra_unique(self):
        rows = [(packet(n), ADDR_A) for n in (10, 12, 12, 11)]
        rows += [(packet(n, uid=UID_B), ADDR_B) for n in (50, 51, 52)]
        result, _ = capture(rows, min_packets=3)
        self.assertEqual(result["status"], "REVIEW")
        a = result["boards"]["A"]
        self.assertEqual((a["unique"], a["duplicates"], a["reordered"], a["missing"]), (3, 1, 1, 0))

    def test_duplicate_packet_prevents_baseline_pass_despite_sufficient_unique(self):
        rows = [(packet(n), ADDR_A) for n in (1, 2, 2)]
        rows += [(packet(n, uid=UID_B), ADDR_B) for n in (1, 2)]
        result, events = capture(rows)
        self.assertEqual(result["boards"]["A"]["missing"], 0)
        self.assertEqual(result["boards"]["A"]["duplicates"], 1)
        self.assertEqual(len([event for event in events if event["event"] == "packet"]), 5)
        self.assertEqual(result["status"], "REVIEW")

    def test_reordered_packet_prevents_baseline_pass_after_gap_is_filled(self):
        rows = [(packet(n), ADDR_A) for n in (1, 3, 2)]
        rows += [(packet(n, uid=UID_B), ADDR_B) for n in (1, 2, 3)]
        result, events = capture(rows, min_packets=3)
        self.assertEqual(result["boards"]["A"]["missing"], 0)
        self.assertEqual(result["boards"]["A"]["reordered"], 1)
        self.assertEqual(result["boards"]["A"]["duplicates"], 0)
        self.assertEqual(len([event for event in events if event["event"] == "packet"]), 6)
        self.assertEqual(result["status"], "REVIEW")

    def test_sequence_tick_inconsistency_fails_and_never_resets_tracker(self):
        for seq, tick in ((11, 200), (9, 11000), (10, 10100), (1, 1000)):
            with self.subTest(seq=seq, tick=tick):
                result, events = capture([(packet(10), ADDR_A), (packet(seq, tick), ADDR_A),
                                          (packet(11), ADDR_A)])
                self.assertEqual(result["status"], "FAIL")
                self.assertEqual(result["sequence_errors"], 1)
                self.assertEqual(result["boards"]["A"]["first_sequence"], 10)
                self.assertEqual(result["boards"]["A"]["unique"], 2)
                self.assertTrue(any(e["event"] == "sequence_error" for e in events))

    def test_modular_sequence_tick_wrap_is_valid(self):
        rows = []
        for uid, address in ((UID_A, ADDR_A), (UID_B, ADDR_B)):
            rows.extend([(packet(0xFFFFFFFF, 0xFFFFFFE8, uid), address),
                         (packet(0, 976, uid), address)])
        result, _ = capture(rows)
        self.assertEqual(result["status"], "PASS")

    def test_timeout_with_missing_board_never_passes(self):
        result, _ = capture([(packet(1), ADDR_A), (packet(2), ADDR_A)])
        self.assertEqual(result["status"], "REVIEW")
        self.assertEqual(result["boards"]["B"]["unique"], 0)

    def test_duplicate_packets_cannot_satisfy_minimum(self):
        rows = [(packet(1), ADDR_A)] * 3 + [(packet(1, uid=UID_B), ADDR_B)] * 3
        result, _ = capture(rows)
        self.assertNotEqual(result["status"], "PASS")

    def test_ctrl_c_never_passes_even_after_enough_packets(self):
        rows = [(packet(1), ADDR_A), (packet(1, uid=UID_B), ADDR_B), KeyboardInterrupt()]
        result, _ = capture(rows, min_packets=1)
        self.assertEqual(result["stop_reason"], "interrupted")
        self.assertNotEqual(result["status"], "PASS")

    def test_packet_quota_does_not_end_window_early(self):
        rows = [(packet(1), ADDR_A), (packet(1, uid=UID_B), ADDR_B)]
        result, _ = capture(rows, duration_s=2, min_packets=1)
        self.assertGreaterEqual(result["duration_s"], 2)
        self.assertEqual(result["status"], "PASS")

    def test_packet_returned_after_deadline_does_not_satisfy_short_window(self):
        rows = [(packet(1), ADDR_A), (packet(1, uid=UID_B), ADDR_B)]
        result, _ = capture(rows, duration_s=0.0015, min_packets=1)
        self.assertEqual(result["boards"]["B"]["unique"], 0)
        self.assertNotEqual(result["status"], "PASS")

    def test_unknown_or_invalid_traffic_prevents_clean_window_pass(self):
        for data, address in ((b"bad", ADDR_A), (packet(2, uid=(1, 2, 3)), ADDR_B)):
            with self.subTest(data=data):
                rows = [(packet(1), ADDR_A), (packet(1, uid=UID_B), ADDR_B), (data, address)]
                result, _ = capture(rows, min_packets=1)
                self.assertEqual(result["status"], "REVIEW")

    def test_direct_monitor_api_rejects_unbounded_windows(self):
        for duration_s, minimum in ((float("nan"), 1), (float("inf"), 1), (0, 1),
                                     (3601, 1), (1, 0), (1, 100001), (1, 1.5), (1, True)):
            with self.subTest(duration_s=duration_s, minimum=minimum), self.assertRaises(ValueError):
                dual.monitor(None, duration_s, minimum, lambda _: None)


class CliTests(unittest.TestCase):
    def run_main(self, argv, packets=()):
        clock = FakeClock()
        sock = FakeSocket(clock, packets)
        with patch.object(dual.socket, "socket", return_value=sock) as factory, \
                patch.object(dual.time, "monotonic", clock), \
                contextlib.redirect_stdout(io.StringIO()) as stdout, \
                contextlib.redirect_stderr(io.StringIO()):
            code = dual.main(argv)
        return code, sock, factory, stdout.getvalue()

    def test_default_bind_port_duration_minimum_and_one_closed_socket(self):
        code, sock, factory, output = self.run_main([])
        self.assertEqual(code, 1)
        self.assertEqual(sock.bound, ("192.168.10.100", 5005))
        self.assertTrue(sock.closed)
        factory.assert_called_once_with(socket.AF_INET, socket.SOCK_DGRAM)
        records = [json.loads(line) for line in output.splitlines()]
        self.assertEqual(records[0]["duration_s"], 60)
        self.assertEqual(records[0]["min_packets_per_board"], 50)

    def test_invalid_cli_numbers_and_ipv4_are_rejected_before_socket_creation(self):
        cases = [("--duration", v) for v in ("0", "-1", "nan", "inf", "3601")]
        cases += [("--min-packets", v) for v in ("0", "-1", "1.5", "100001")]
        cases += [("--port", v) for v in ("0", "-1", "65536")]
        cases += [("--bind", "not-an-ip")]
        for argv in cases:
            with self.subTest(argv=argv), patch.object(dual.socket, "socket") as factory, \
                    contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as exc:
                    dual.main(list(argv))
                self.assertEqual(exc.exception.code, 2)
                factory.assert_not_called()

    def test_existing_log_is_not_overwritten_and_socket_not_opened(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "capture.jsonl"
            path.write_text("preserve", encoding="utf-8")
            code, _, factory, _ = self.run_main(["--log", str(path)])
            self.assertEqual(code, 2)
            self.assertEqual(path.read_text(encoding="utf-8"), "preserve")
            factory.assert_not_called()

    def test_jsonl_creates_parent_records_metadata_and_returns_zero_for_window_pass(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "nested" / "capture.jsonl"
            rows = [(packet(1), ADDR_A), (packet(1, uid=UID_B), ADDR_B)]
            code, sock, _, _ = self.run_main(["--log", str(path), "--duration", "0.01",
                                              "--min-packets", "1"], rows)
            self.assertEqual(code, 0)
            self.assertTrue(sock.closed)
            records = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
            self.assertEqual(records[-1]["status"], "PASS")
            self.assertFalse(records[-1]["hardware_verified"])
            self.assertEqual(records[1]["source"], list(ADDR_A))
            self.assertEqual(records[1]["raw_hex"], packet(1).hex())
            self.assertEqual(records[2]["uid"], ["00380050", "35385104", "37313532"])

    def test_socket_closes_on_bind_error(self):
        sock = FakeSocket(FakeClock())
        with patch.object(dual.socket, "socket", return_value=sock), \
                patch.object(sock, "bind", side_effect=OSError("bind failed")), \
                contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(dual.main([]), 2)
        self.assertTrue(sock.closed)

    def test_socket_closes_on_receive_error_and_returns_error_exit_code(self):
        code, sock, _, output = self.run_main([], [OSError("receive failed")])
        self.assertEqual(code, 2)
        self.assertTrue(sock.closed)
        self.assertNotIn('"status": "PASS"', output)

    def test_ctrl_c_closes_socket_and_preserves_summary_in_log(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "capture.jsonl"
            code, sock, _, _ = self.run_main(["--log", str(path)], [KeyboardInterrupt()])
            self.assertEqual(code, 1)
            self.assertTrue(sock.closed)
            self.assertEqual(json.loads(path.read_text(encoding="utf-8").splitlines()[-1])["stop_reason"],
                             "interrupted")


if __name__ == "__main__":
    unittest.main()
