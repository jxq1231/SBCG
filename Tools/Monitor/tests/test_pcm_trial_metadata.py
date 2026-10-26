"""Host tests for controlled-trial metadata; no hardware or sound."""
import importlib.util
import json
import struct
import tempfile
import unittest
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location(
    "pcm_trial_metadata", ROOT / "Tools" / "Monitor" / "pcm_trial_metadata.py")
metadata = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(metadata)


class MetadataTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.wav = self.root / "standard.wav"
        with wave.open(str(self.wav), "wb") as handle:
            handle.setnchannels(1)
            handle.setsampwidth(2)
            handle.setframerate(48000)
            handle.writeframes(struct.pack("<4800h", *([0] * 4800)))
        self.manifest = self.root / "candidate_manifest.json"
        self.manifest.write_text(json.dumps({
            "sample_rate_hz": 48000,
            "playback_event_start_frames": [480, 960],
            "files": {"standard.wav": {"sha256": "unused"}},
        }), encoding="utf-8")
        self.image = self.root / "image.elf"
        self.image.write_bytes(b"firmware")

    def tearDown(self):
        self.tmp.cleanup()

    def records(self):
        return [
            {"t": 0.5, "line": "$BOOT,FW=SBCG-M4.2-PCMCAP,HCLK_HZ=200000000,"
                              "UID_W0=001A002D,UID_W1=30365105,UID_W2=36363631"},
            {"t": 0.6, "line": "$CHIRP_INIT,RESULT=OK,CH=0,TEMPLATE=2K6K50MS"},
            {"t": 0.7, "line": "$AUDIO,SEQ=1,STATE=RUN,FRAMES=1000,RATE_HZ=48000"},
            {"t": 1.0, "line": "$SOUND_CAND,SEQ=1,FIRST=1"},
            {"t": 3.0, "line": "$SOUND_CAND,SEQ=2,FIRST=2"},
        ]

    def test_wav_reference_and_serial_parse(self):
        ref = metadata.wav_reference(self.wav, self.manifest)
        self.assertEqual(ref["sample_rate_hz"], 48000)
        self.assertEqual(ref["frames"], 4800)
        self.assertEqual(ref["expected_onsets_s"], [0.01, 0.02])
        parsed = metadata.parse_serial_metadata([r["line"] for r in self.records()])
        self.assertEqual(parsed["firmware"], "SBCG-M4.2-PCMCAP")
        self.assertEqual(parsed["uid"], "001A002D:30365105:36363631")
        self.assertEqual(parsed["channel"], 0)
        self.assertEqual(parsed["rate_hz"], 48000)

    def test_complete_manifest(self):
        manifest, errors = metadata.build_manifest(
            image_path=self.image, expected_fw="SBCG-M4.2-PCMCAP",
            wav=self.wav, manifest_path=self.manifest,
            serial_records=self.records(), play_span=(2.0, 2.2),
            prelude_s=2.0, tail_s=1.0, expected_channel=0)
        self.assertEqual(errors, [])
        self.assertEqual(len(manifest["image"]["sha256"]), 64)
        self.assertEqual(manifest["observed"]["uid"],
                         "001A002D:30365105:36363631")
        self.assertEqual(manifest["playback"]["sound_candidates_before"], 1)
        self.assertEqual(manifest["playback"]["sound_candidates_after"], 1)

    def test_missing_or_mismatched_evidence_is_an_error(self):
        manifest, errors = metadata.build_manifest(
            image_path=self.root / "missing.elf", image_sha256=None,
            expected_fw="SBCG-M4.2-PCMCAP", wav=self.wav,
            manifest_path=self.manifest, serial_records=[], play_span=None,
            prelude_s=1.0, tail_s=1.0)
        self.assertTrue(any("image sha256" in e or "image path" in e for e in errors))
        self.assertTrue(any("firmware" in e for e in errors))
        _, errors = metadata.build_manifest(
            image_path=self.image, image_sha256="x" * 64,
            expected_fw="WRONG", wav=self.wav, manifest_path=self.manifest,
            serial_records=self.records(), play_span=(1.0, 2.0),
            prelude_s=1.0, tail_s=1.0)
        self.assertTrue(any("firmware mismatch" in e for e in errors))


if __name__ == "__main__":
    unittest.main()
