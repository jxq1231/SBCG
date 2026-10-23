"""Recorder tests use a fake port; never open hardware."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import serial_monitor as capture

class Clock:
    def __init__(self):self.now=0.0
    def __call__(self):return self.now
class Port:
    def __init__(self,clock,chunks,fail=False):
        self.clock=clock;self.chunks=iter(chunks);self.fail=fail;self.is_open=False;self.closed=False
    def open(self):self.is_open=True
    def close(self):self.closed=True;self.is_open=False
    def read(self,n):
        self.clock.now+=0.25
        if self.fail:raise OSError('test read failure')
        return next(self.chunks,b'')

class RecorderTests(unittest.TestCase):
    def run_capture(self,prefix,chunks,fail=False):
        clock=Clock();port=Port(clock,chunks,fail)
        result=capture.capture_serial('FAKE',1,prefix,lambda **kw:port,clock)
        return result,port
    def test_split_lines_and_binary_projection(self):
        with tempfile.TemporaryDirectory() as d:
            prefix=Path(d)/'run';r,p=self.run_capture(prefix,[b'$BOOT,FW=X\r',b'\n$TEMP,T=1\r\n',b'\xff'])
            self.assertEqual(Path(str(prefix)+'-serial.bin').read_bytes(),b'$BOOT,FW=X\r\n$TEMP,T=1\r\n\xff')
            rows=[json.loads(x) for x in Path(str(prefix)+'-serial.jsonl').read_text().splitlines()]
            self.assertEqual([x.get('line') for x in rows[:2]],['$BOOT,FW=X','$TEMP,T=1'])
            self.assertEqual(rows[-1]['incomplete_tail_hex'],'ff')
            self.assertEqual(r['status'],'CAPTURED_PENDING_ANALYSIS');self.assertFalse(r['hardware_verified'])
            self.assertTrue(p.closed);self.assertFalse(p.dtr);self.assertFalse(p.rts)
    def test_failure_closes_and_preserves_error(self):
        with tempfile.TemporaryDirectory() as d:
            r,p=self.run_capture(Path(d)/'fail',[],True)
            self.assertEqual(r['status'],'FAILED');self.assertIn('read failure',r['error']);self.assertTrue(p.closed)
    def test_refuse_overwrite_before_open(self):
        with tempfile.TemporaryDirectory() as d:
            prefix=Path(d)/'same';r,p=self.run_capture(prefix,[])
            with self.assertRaises(FileExistsError):self.run_capture(prefix,[])
    def test_validate_duration_before_port(self):
        for duration in (0,-1,float('nan'),float('inf'),3601):
            with self.assertRaises(ValueError):capture.capture_serial('FAKE',duration,Path('unused'),lambda **kw:None)
    def test_nonascii_complete_line_preserved(self):
        with tempfile.TemporaryDirectory() as d:
            prefix=Path(d)/'raw';r,p=self.run_capture(prefix,[b'x\xff\n'])
            rows=[json.loads(x) for x in Path(str(prefix)+'-serial.jsonl').read_text().splitlines()]
            self.assertEqual(rows[0]['line'],'x\ufffd')
if __name__=='__main__':unittest.main()
