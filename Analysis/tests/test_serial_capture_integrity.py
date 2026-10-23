import json
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from serial_capture_integrity import validate_boot_capture

class IntegrityTests(unittest.TestCase):
    def rows(self,raw):
        return [dict(line=p.rstrip(b'\r').decode('ascii',errors='replace')) for p in raw[:-1].split(b'\n')]
    def test_reset_fragment_preserved_as_raw_hex_and_projection_matches(self):
        raw=b'old,SEQ=1\r\nold,partial\xff$BOOT,FW=TEST,UID=1\r\n$HEARTBEAT,SEQ=1\r\n'
        lines,proof=validate_boot_capture(raw,self.rows(raw),'TEST')
        self.assertEqual(lines,['$BOOT,FW=TEST,UID=1','$HEARTBEAT,SEQ=1'])
        self.assertEqual(proof['discarded_pre_boot_lines'],1)
        self.assertEqual(proof['discarded_prefix_hex'],b'old,partial\xff'.hex())
        self.assertEqual(proof['pre_boot_nonascii_bytes'],1)
        self.assertTrue(proof['raw_json_projection_matches'])
    def test_nonascii_after_new_boot_is_rejected(self):
        raw=b'$BOOT,FW=TEST,UID=1\r\n$DATA,VALUE=\xff\r\n'
        with self.assertRaisesRegex(ValueError,'non-ASCII'):
            validate_boot_capture(raw,self.rows(raw),'TEST')
    def test_tampered_projection_missing_rows_and_tail_are_rejected(self):
        raw=b'$BOOT,FW=TEST,UID=1\r\n$DATA,VALUE=1\r\n'
        rows=self.rows(raw)
        for altered in (rows[:-1],[rows[0],dict(line='$DATA,VALUE=2')],rows+[dict(incomplete_tail_hex='ff')]):
            with self.assertRaises(ValueError):validate_boot_capture(raw,altered,'TEST')
        with self.assertRaises(ValueError):validate_boot_capture(raw[:-1],rows,'TEST')
    def test_wrong_duplicate_boot_and_oversized_reset_fragment_fail(self):
        for raw in (b'$BOOT,FW=OLD,UID=1\r\n',b'$BOOT,FW=TEST,UID=1\r\n$BOOT,FW=TEST,UID=1\r\n',b'x'*256+b'$BOOT,FW=TEST,UID=1\r\n'):
            with self.assertRaises(ValueError):validate_boot_capture(raw,self.rows(raw),'TEST')

if __name__=='__main__':unittest.main()
