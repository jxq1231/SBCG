import sys
from pathlib import Path
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from audio_capture_analysis import check_audio_rows


class AudioCaptureAnalysisTests(unittest.TestCase):
    def setUp(self):
        self.init = dict(RESULT="OK", NOMINAL_HZ="48000", DERIVED_MILLI_HZ="47991071",
                         START_ERR="0", CODEC="0", I2C_ERR="00000000", REG="FFFF", WRITES="24")
        self.rows = []
        for i in range(1, 31):
            n = i * 6
            self.rows.append(dict(SEQ=str(i), STATE="RUN", FRAMES=str(n*8192),
                                  H=str(n//2), F=str(n//2), OK=str(n), DROP="0", ERR="0",
                                  HAL="00000000", DMA="00000000", RATE_HZ="47991", VALID="1",
                                  P0=str(100+i), P1=str(200+i), R0="30", R1="40"))

    def test_valid_continuous_rows_pass(self):
        result = check_audio_rows(self.init, self.rows)
        self.assertEqual(result["valid_rows"], 30)

    def test_codec_failure_cannot_pass(self):
        self.init["WRITES"] = "23"
        with self.assertRaises(ValueError): check_audio_rows(self.init, self.rows)

    def test_zero_constant_or_clipped_audio_cannot_pass(self):
        for value in ("0", "100", "32768"):
            rows = [dict(r, P0=value, R0=value) for r in self.rows]
            with self.assertRaises(ValueError): check_audio_rows(self.init, rows)

    def test_error_drop_invalid_or_bad_rate_fails(self):
        for field, value in (("ERR", "1"), ("DROP", "1"), ("HAL", "00000010"),
                             ("DMA", "00000008"), ("VALID", "0"), ("RATE_HZ", "24000"),
                             ("STATE", "STALLED"), ("FRAMES", "1"), ("H", "1")):
            rows = [dict(r) for r in self.rows]; rows[10][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                check_audio_rows(self.init, rows)

    def test_missing_reordered_or_short_window_fails(self):
        for rows in (self.rows[:10], self.rows[:5]+self.rows[6:], list(reversed(self.rows))):
            with self.assertRaises(ValueError): check_audio_rows(self.init, rows)
