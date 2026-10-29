"""Dual PCM isolation and integrity tests; no attached hardware."""
import sys
import struct
import tempfile
import unittest
from unittest.mock import patch, MagicMock
from types import SimpleNamespace
from contextlib import redirect_stdout
import io
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from dual_acoustic_capture import CaptureStore,run
from udp_chirp_capture import HEADER

def chunk(seq=0,total=2,present=512,seed=1,first=1000,capture_id=7):
    return HEADER.pack(b'PCM1',1,1,0,0,seq,total,present,0,capture_id,1,
                       first+512*seq,1200,1100)+struct.pack('<'+str(present)+'h',*([seed]*present))

class StoreTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.store=CaptureStore(Path(self.tmp.name))

    def test_same_ids_never_mix_boards(self):
        for seq in (1,0):
            self.store.accept(chunk(seq,seed=10),('192.168.10.1',5008),1)
            self.store.accept(chunk(seq,seed=20),('192.168.10.2',5008),1)
        a=list(Path(self.tmp.name).glob('A/**/*.pcm'));b=list(Path(self.tmp.name).glob('B/**/*.pcm'))
        self.assertEqual((len(a),len(b)),(1,1));self.assertNotEqual(a[0].read_bytes(),b[0].read_bytes())

    def test_full_8544_frame_capture(self):
        for seq in reversed(range(17)):
            result=self.store.accept(chunk(seq,total=17,present=352 if seq==16 else 512),('192.168.10.1',5008),1)
        self.assertEqual(result['frames'],8544)

    def test_bind_failure_is_saved_and_socket_closed(self):
        args=SimpleNamespace(out=Path(self.tmp.name)/'failure',bind='192.168.10.100',
                             udp_port=6008,flash_record=None,duration=1,a_port='A',b_port='B')
        with patch('dual_acoustic_capture.socket.socket') as factory,redirect_stdout(io.StringIO()):
            factory.return_value.bind.side_effect=OSError('bind failed')
            self.assertEqual(run(args),1)
            factory.return_value.close.assert_called_once()
        self.assertEqual(__import__('json').loads((args.out/'manifest.json').read_text())['status'],'FAILED')

    def test_second_port_failure_closes_first(self):
        import serial
        args=SimpleNamespace(out=Path(self.tmp.name)/'port-failure',bind='192.168.10.100',
                             udp_port=6008,flash_record=None,duration=1,a_port='A',b_port='B')
        first,second=MagicMock(),MagicMock();second.open.side_effect=serial.SerialException('busy')
        with patch('dual_acoustic_capture.socket.socket'),patch('serial.Serial',side_effect=[first,second]),redirect_stdout(io.StringIO()):
            self.assertEqual(run(args),1)
        first.close.assert_called_once()

    def test_reordered_and_identical_duplicate(self):
        self.store.accept(chunk(1),('192.168.10.1',5008),1)
        self.store.accept(chunk(1),('192.168.10.1',5008),2)
        result=self.store.accept(chunk(0),('192.168.10.1',5008),3)
        self.assertEqual(result['frames'],1024);self.assertEqual(result['first_frame'],1000)
        self.assertEqual(self.store.duplicates,1)
        self.store.accept(chunk(0),('192.168.10.1',5008),4)
        self.assertEqual(len(list(Path(self.tmp.name).glob('A/**/*.pcm'))),1)

    def test_conflicting_duplicate_invalidates_capture(self):
        self.store.accept(chunk(0),('192.168.10.1',5008),1)
        with self.assertRaises(ValueError):self.store.accept(chunk(0,seed=2),('192.168.10.1',5008),2)
        with self.assertRaises(ValueError):self.store.accept(chunk(1),('192.168.10.1',5008),3)
        self.assertFalse(list(Path(self.tmp.name).rglob('*.pcm')))

    def test_metadata_drift(self):
        self.store.accept(chunk(0),('192.168.10.1',5008),1)
        with self.assertRaises(ValueError):self.store.accept(chunk(1,first=2000),('192.168.10.1',5008),2)

    def test_missing_and_whole_capture_absence_not_called_complete(self):
        self.store.accept(chunk(1),('192.168.10.1',5008),1)
        pending=self.store.summary()['incomplete']
        self.assertEqual(pending[0]['missing_chunks'],[0])
        self.assertEqual(self.store.summary()['completed'],{'A':0,'B':0})

    def test_boot_change_keeps_old_incomplete_separate(self):
        self.store.accept(chunk(0),('192.168.10.1',5008),1)
        self.store.note_boot('A')
        self.store.accept(chunk(1),('192.168.10.1',5008),2)
        self.assertFalse(list(Path(self.tmp.name).rglob('*.pcm')))
        self.store.accept(chunk(0),('192.168.10.1',5008),3)
        self.assertEqual(len(self.store.summary()['incomplete']),1)

    def test_foreign_short_reserved_and_short_middle_rejected(self):
        bad=bytearray(chunk());bad[7]=1
        for data,address in [(chunk(),('192.168.10.9',5008)),(chunk(),('192.168.10.1',5009)),
                             (b'PCM1',('192.168.10.1',5008)),(bytes(bad),('192.168.10.1',5008)),
                             (chunk(present=12),('192.168.10.1',5008))]:
            with self.subTest(address=address),self.assertRaises(ValueError):self.store.accept(data,address,1)

if __name__=='__main__':unittest.main()
