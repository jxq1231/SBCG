import importlib.util
from pathlib import Path
import socket
import struct
import sys
import unittest

spec = importlib.util.spec_from_file_location(
    "udp_monitor", Path(__file__).resolve().parents[1] / "udp_monitor.py")
udp = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = udp
spec.loader.exec_module(udp)
UID = (0x001A002D, 0x30365105, 0x36363631)


def packet(seq, tick=None, uid=UID):
    if tick is None:
        tick = (seq * 1000) & 0xFFFFFFFF
    return struct.pack("!4sBBHIIIII", b"SBCG", 1, 1, 28, seq, tick, *uid)


class ProtocolTests(unittest.TestCase):
    def test_network_byte_order_and_uid(self):
        data = bytes.fromhex("534243470101001c00000001000003e8001a002d3036510536363631")
        p = udp.decode_packet(data)
        self.assertEqual((p.sequence, p.tick_ms, p.uid_words), (1, 1000, UID))

    def test_rejects_bad_envelope(self):
        good = packet(1)
        cases = [good[:-1], good + b"x", b"BAD!" + good[4:],
                 good[:4] + b"\x02" + good[5:], good[:5] + b"\x02" + good[6:],
                 good[:6] + b"\x00\x1b" + good[8:]]
        for data in cases:
            with self.subTest(data=data), self.assertRaises(ValueError):
                udp.decode_packet(data)

    def test_gap_can_be_filled_without_counting_duplicate_twice(self):
        t = udp.SequenceTracker()
        for seq in (10, 12, 12):
            t.observe(udp.decode_packet(packet(seq)))
        self.assertEqual((t.summary()["unique"], t.summary()["missing"],
                          t.summary()["duplicates"]), (2, 1, 1))
        t.observe(udp.decode_packet(packet(11)))
        self.assertEqual((t.summary()["unique"], t.summary()["missing"],
                          t.summary()["reordered"]), (3, 0, 1))

    def test_sequence_and_tick_wrap(self):
        t = udp.SequenceTracker()
        for seq, tick in ((0xFFFFFFFE, 0xFFFFFC00), (0xFFFFFFFF, 0xFFFFFFE8),
                          (0, 976), (1, 1976)):
            t.observe(udp.decode_packet(packet(seq, tick)))
        self.assertEqual((t.summary()["unique"], t.summary()["missing"]), (4, 0))

    def test_old_packet_before_first_observation_is_not_loss(self):
        t = udp.SequenceTracker()
        t.observe(udp.decode_packet(packet(100)))
        t.observe(udp.decode_packet(packet(99)))
        self.assertEqual(t.summary()["unique"], 1)
        self.assertEqual(t.summary()["before_baseline"], 1)
        self.assertEqual(t.summary()["missing"], 0)

    def test_uid_changes_and_inconsistent_tick_do_not_mix_sessions(self):
        for p in (packet(11, 200), packet(9, 11000), packet(10, 10100), packet(11, uid=(1, 2, 3))):
            with self.subTest(p=p):
                t = udp.SequenceTracker()
                t.observe(udp.decode_packet(packet(10)))
                with self.assertRaises(ValueError):
                    t.observe(udp.decode_packet(p))
                self.assertEqual(t.summary()["unique"], 1)

    def test_cadence_uses_consecutive_forward_sequences_only(self):
        t = udp.SequenceTracker()
        for seq, tick in ((1, 1016), (2, 2017), (4, 4016), (3, 3016), (5, 5016)):
            t.observe(udp.decode_packet(packet(seq, tick)))
        result = t.summary()
        self.assertEqual((result["cadence_samples"], result["tick_step_ms_min"],
                          result["tick_step_ms_max"]), (2, 1000, 1001))

    def test_memory_and_sequence_span_are_bounded(self):
        t = udp.SequenceTracker(max_unique=2)
        t.observe(udp.decode_packet(packet(1)))
        t.observe(udp.decode_packet(packet(2)))
        with self.assertRaises(ValueError):
            t.observe(udp.decode_packet(packet(3)))
        t = udp.SequenceTracker()
        for seq in (0, 0x40000000):
            t.observe(udp.decode_packet(packet(seq, 100)))
        with self.assertRaises(ValueError):
            t.observe(udp.decode_packet(packet(0x80000000, 100)))

    def test_real_local_socket_filters_source_and_records_packets(self):
        events = []
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as rx, \
                socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as tx:
            rx.bind(("127.0.0.1", 0))
            tx.bind(("127.0.0.1", 0))
            target = rx.getsockname()
            tx.sendto(b"invalid", target)
            tx.sendto(packet(1), target)
            tx.sendto(packet(3), target)
            tx.sendto(packet(2), target)
            tx.sendto(packet(2), target)
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as foreign:
                foreign.sendto(packet(4), target)
            result = udp.monitor(rx, "127.0.0.1", tx.getsockname()[1], UID,
                                 duration_s=0.1, min_packets=3, emit=events.append)
        self.assertEqual((result["unique"], result["missing"], result["duplicates"],
                          result["reordered"], result["invalid"], result["foreign"]),
                         (3, 0, 1, 1, 1, 1))
        self.assertEqual(len([e for e in events if e["event"] == "packet"]), 4)
        self.assertEqual(result["status"], "REVIEW")

    def test_timeout_without_packets_is_not_success(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as rx:
            rx.bind(("127.0.0.1", 0))
            result = udp.monitor(rx, "127.0.0.1", 5005, UID,
                                 duration_s=0.01, min_packets=30, emit=lambda e: None)
        self.assertEqual((result["unique"], result["status"]), (0, "REVIEW"))
