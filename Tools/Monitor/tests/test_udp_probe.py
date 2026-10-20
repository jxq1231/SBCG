import importlib.util
from pathlib import Path
import socket
import struct
import sys
import threading
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
spec = importlib.util.spec_from_file_location("udp_probe", Path(__file__).resolve().parents[1]/"udp_probe.py")
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)
WIRE = struct.Struct("!4sBBHIIIII")
UID = (0x001A002D, 0x30365105, 0x36363631)


class ProbeTests(unittest.TestCase):
    def test_ack_envelope_and_byte_order(self):
        data = WIRE.pack(b"SBCG",1,3,28,7,12345,*UID)
        self.assertEqual(probe.decode_ack(data), (7,12345,UID))
        for bad in (data[:-1], data+b"x", b"FAIL"+data[4:],
                    data[:4]+b"\x02"+data[5:], data[:5]+b"\x02"+data[6:],
                    data[:6]+b"\x00\x1b"+data[8:]):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                probe.decode_ack(bad)

    def run_local(self, count, faulty=False):
        events, server_errors = [], []
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as server, \
                socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as client:
            server.bind(("127.0.0.1",0)); server.settimeout(2)
            client.bind(("127.0.0.1",0))

            def respond():
                try:
                    for expected in range(1,count+1):
                        data, addr = server.recvfrom(100)
                        fields = WIRE.unpack(data)
                        self.assertEqual(fields[:5], (b"SBCG",1,2,28,expected))
                        self.assertEqual(fields[6:], UID)
                        if faulty:
                            server.sendto(b"invalid",addr)
                            server.sendto(WIRE.pack(b"SBCG",1,3,28,expected,123,1,2,3),addr)
                            server.sendto(WIRE.pack(b"SBCG",1,3,28,expected+99,fields[5],*UID),addr)
                            server.sendto(WIRE.pack(b"SBCG",1,3,28,expected,(fields[5]-1000)&0xFFFFFFFF,*UID),addr)
                        server.sendto(WIRE.pack(b"SBCG",1,3,28,expected,fields[5],*UID),addr)
                except BaseException as exc:
                    server_errors.append(exc)

            worker=threading.Thread(target=respond,daemon=True);worker.start()
            result=probe.run_probe(client,server.getsockname(),UID,count=count,
                                   interval_s=0.001,timeout_s=0.1,emit=events.append)
            worker.join(2)
            self.assertFalse(worker.is_alive())
            self.assertFalse(server_errors,server_errors)
        return result,events

    def test_real_local_request_reply_roundtrip(self):
        result,events=self.run_local(3)
        self.assertEqual((result["sent"],result["received"],result["timeouts"],result["status"]),
                         (3,3,0,"PASS"))
        self.assertFalse(result["hardware_verified"])
        acks=[e for e in events if e["event"]=="ack"]
        self.assertEqual([e["sequence"] for e in acks],[1,2,3])
        self.assertTrue(all(e["rtt_ms"]>=0 for e in acks))
        self.assertEqual([e["echo_tick_ms"] for e in acks],
                         [e["client_tick_ms"] for e in events if e["event"]=="request"])

    def test_invalid_uid_and_wrong_sequence_never_confirm_current_request(self):
        result,events=self.run_local(1,faulty=True)
        self.assertEqual((result["received"],result["invalid"],result["unexpected"],result["status"]),
                         (1,2,2,"REVIEW"))
        self.assertEqual(len([e for e in events if e["event"]=="ack"]),1)

    def test_no_reply_is_timeout_not_success(self):
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as idle, \
                socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as client:
            idle.bind(("127.0.0.1",0));client.bind(("127.0.0.1",0))
            r=probe.run_probe(client,idle.getsockname(),UID,count=2,
                              interval_s=0.001,timeout_s=0.01,emit=lambda e:None)
        self.assertEqual((r["sent"],r["received"],r["timeouts"],r["status"]),(2,0,2,"REVIEW"))
