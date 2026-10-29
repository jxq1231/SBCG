"""Host-side tests for the UDP chirp candidate PCM capture tooling; no hardware."""
import importlib.util
import struct
from pathlib import Path
import socket
import threading
import unittest

sys_path = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("udp_chirp_capture", sys_path / "udp_chirp_capture.py")
capture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)
HEADER = capture.HEADER


def make_chunk(seq, total, present, capture_id, session, first, hint, raw, flags=0, seed=1):
    payload = [(i % 251) - 125 for i in range(seed * 100, seed * 100 + present)]
    header = HEADER.pack(capture.MAGIC, 1, 1, flags, 0, seq, total, present, 0,
                         capture_id, session, first + seq * capture.CHUNK_FRAMES, hint, raw)
    return header + struct.pack(f"<{present}h", *payload)


class DecodeTests(unittest.TestCase):
    def test_valid_and_byte_order(self):
        chunk = make_chunk(0, 3, 2, 7, 1, 1000, 2052, 1900)
        meta = capture.decode_packet(chunk)
        self.assertEqual((meta["seq"], meta["total"], meta["present"] if "present" in meta else 0), (0, 3, 0))
        self.assertEqual((meta["id"], meta["session"]), (7, 1))
        self.assertEqual(meta["first_frame"], 1000)
        self.assertEqual(len(meta["payload"]), 2)

    def test_rejects_malformed(self):
        good = make_chunk(0, 3, 2, 7, 1, 1000, 2052, 1900)
        for bad in (good[:-1], good + b"x", b"FAIL" + good[4:],
                    HEADER.pack(capture.MAGIC, 1, 1, 0, 0, 99, 3, 2, 0, 7, 1, 1000, 2052, 1900)):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                capture.decode_packet(bad)


class AssemblerTests(unittest.TestCase):
    def test_reassembles_out_of_order_and_duplicates(self):
        assembler = capture.CaptureAssembler()
        chunks = [make_chunk(i, 3, 512 if i < 2 else 100, 1, 1, 300, 500, 400) for i in range(3)]
        done = None
        for chunk in (chunks[2], chunks[0], chunks[0], chunks[1]):  # last-first-dup-middle
            meta = capture.decode_packet(chunk)
            done = assembler.feed(meta, meta.pop("payload"))
        self.assertEqual(assembler.received, 3)
        self.assertEqual(assembler.duplicates, 1)
        self.assertEqual(assembler.completed, 1)
        self.assertIsNotNone(done)
        self.assertEqual((done["id"], done["frames"]), (1, 1124))
        self.assertEqual(done["first_frame"], 300)

    def test_missing_chunk_stays_incomplete(self):
        assembler = capture.CaptureAssembler()
        for i in (0, 2):
            meta = capture.decode_packet(make_chunk(i, 3, 512, 5, 2, 10, 20, 15))
            assembler.feed(meta, meta.pop("payload"))
        self.assertEqual(assembler.completed, 0)

    def test_local_udp_roundtrip(self):
        errors = []
        out = None
        server_port = None
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server, \
                socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
            server.bind(("127.0.0.1", 0))
            server_port = server.getsockname()[1]
            server.settimeout(2)

            def send():
                try:
                    for i in range(4):
                        client.sendto(make_chunk(i, 4, 7, 3, 1, 8800, 9000, 8990), ("127.0.0.1", server_port))
                except BaseException as exc:
                    errors.append(exc)

            worker = threading.Thread(target=send, daemon=True)
            worker.start()
            assembler = capture.CaptureAssembler()
            for _ in range(4):
                data, _ = server.recvfrom(1600)
                meta = capture.decode_packet(data)
                out = assembler.feed(meta, meta.pop("payload"))
            worker.join(2)
            self.assertFalse(worker.is_alive())
        self.assertFalse(errors, errors)
        self.assertIsNotNone(out)
        self.assertEqual((out["id"], out["session"], out["frames"]), (3, 1, 28))


if __name__ == "__main__":
    unittest.main()