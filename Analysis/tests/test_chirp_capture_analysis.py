"""Host tests for offline chirp capture broadband vs band-limited correlation."""
import importlib.util
import json
import tempfile
from pathlib import Path
import unittest

import numpy as np

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("chirp_capture_analysis", root / "chirp_capture_analysis.py")
ana = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ana)
FS = ana.NOMINAL_HZ


def make_chirp(frames=2400, f0=2000.0, f1=6000.0):
    t = np.arange(frames) / FS
    phase = 2 * np.pi * (f0 * t + 0.5 * (f1 - f0) * t * t / (frames / FS))
    value = np.sin(phase).astype(np.float64)
    value -= value.mean()
    return value


class AnalysisTests(unittest.TestCase):
    def run_capture(self, signal, hint, raw):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            pcm = tmp / "id1.pcm"
            pcm.write_bytes(np.rint(signal * 8000.0).astype(np.int16).tobytes())
            meta = {"session": 1, "id": 1, "frames": int(signal.size),
                    "first_frame": 0, "hint_frame": int(hint), "raw_first_frame": int(raw),
                    "left_clamped": False, "right_clamped": True, "tail_topup": False,
                    "tag": "unit"}
            (tmp / "id1.json").write_text(json.dumps(meta), encoding="utf-8")
            return ana.analyze_capture(pcm, tmp / "id1.json", make_chirp())

    def test_perfect_alignment_broadband_and_alignment_shift(self):
        template = make_chirp()
        signal = np.zeros(8192)
        signal[1000:3400] = template
        result = self.run_capture(signal, 1000, 1000)
        broad = result["offline_broadband"]
        self.assertIsNotNone(broad["lag"])
        self.assertGreater(broad["corr"], 0.999)
        self.assertEqual(broad["lag"], 1000)
        self.assertEqual(result["lag_shift_frames"], 0)

    def test_out_of_band_noise_drops_broadband_but_not_bandlimited(self):
        template = make_chirp()
        noise = (0.6 * np.sin(2 * np.pi * 9000 * np.arange(8192) / FS) +
                 0.6 * np.sin(2 * np.pi * 1000 * np.arange(8192) / FS))
        signal = np.zeros(8192)
        signal[1000:3400] = template
        signal += noise
        result = self.run_capture(signal, 1000, 1000)
        broad = result["offline_broadband"]["corr"]
        band = result["offline_bandlimited_2_6k"]["corr"]
        self.assertGreater(band, broad + 0.05, "band limiting must recover in-band peak")
        self.assertGreater(band, 0.95)

    def test_negative_windows_stay_low_and_fa_curve_sane(self):
        template = make_chirp()
        signal = np.zeros(16384)
        signal[2500:4900] = template
        result = self.run_capture(signal, 2500, 2500)
        negatives = result["negatives"]
        self.assertGreaterEqual(negatives["windows"], 20)
        self.assertLess(max(negatives["corrs"]), 0.2)
        self.assertEqual(negatives["fa_curve"][0.8], 0.0)

    def test_true_peak_tracked_with_shifted_alignment(self):
        template = make_chirp()
        signal = np.zeros(8192)
        signal[1500:3900] = template
        result = self.run_capture(signal, 6000, 6000)  # deceptive hint far away
        broad = result["offline_broadband"]
        self.assertEqual(broad["lag"], 1500)
        self.assertEqual(result["lag_shift_frames"], 1500 - 6000)
        self.assertGreater(broad["corr"], 0.999)


if __name__ == "__main__":
    unittest.main()