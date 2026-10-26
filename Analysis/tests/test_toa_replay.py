"""Host tests for the TOA replay pipeline (self-contained temp fixtures).

Verifies window slicing rules, the C adapter contract, full-pipeline
conservation with reserved background events, and input reproducibility.
Real firmware files are not touched.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from toa_replay import (  # noqa: E402
    ROOT, extract_window, make_run_dir, run_cli, run_git,
)

PCM_DIR = Path(__file__).resolve().parents[2] / "Logs" / "Chirp" / "PCM"


def make_pcm(frames):
    """Simple mono int16 PCM: a ramp from 0 so thresholds are crossed."""
    raw = bytearray()
    for i in range(frames):
        value = min((i + 1) * 200, 32767)
        raw += (value & 0xFFFF).to_bytes(2, "little")
    return bytes(raw)


def make_fixture(root: Path, trial: str, capture_specs, sound_specs):
    trial_dir = root / "Logs" / "Chirp" / trial
    trial_dir.mkdir(parents=True, exist_ok=True)
    pcm_dir = root / "Logs" / "Chirp" / "PCM" / trial
    pcm_dir.mkdir(parents=True, exist_ok=True)
    lines = []
    for ident, first, frames in capture_specs:
        lines.append(f"$CHIRP_CAPTURE,ID={ident},SESSION=1,FIRST={first},"
                     f"FRAMES={frames},CLAMP=0,TAIL=1,PKTS=2,TOTAL=2,RESULT=OK")
        (pcm_dir / f"id{ident}.pcm").write_bytes(make_pcm(frames))
        (pcm_dir / f"id{ident}.json").write_text(
            json.dumps({"id": ident, "session": 1, "frames": frames,
                        "first_frame": first, "hint_frame": first + 64,
                        "raw_first_frame": first + 64}), encoding="utf-8")
    for seq, first in sound_specs:
        lines.append(f"$SOUND_CAND,SEQ={seq},SESSION=1,CH=0,FIRST={first},"
                     f"CONFIRMED={first + 127},E_AC=1,PEAK=1,CLIP=0,LOCAL=1,"
                     f"SYNC=0,TOA_US={int(first * 1e6 / 48000)}")
    with (trial_dir / "run.jsonl").open("w", encoding="utf-8") as handle:
        for index, line in enumerate(lines):
            handle.write(json.dumps({"t": 0.1 * index, "line": line}) + "\n")


class WindowSliceTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.blob = Path(self._tmp.name) / "blob.pcm"
        self.blob.write_bytes(b"\x00\x00" * 1000)

    def tearDown(self):
        self._tmp.cleanup()

    def test_full_256_window(self):
        data, status = extract_window(self.blob, 2000, 1000, 1056)
        self.assertEqual(status, "OK")
        self.assertEqual(len(data), 512)

    def test_missing_left_is_rejected_without_padding(self):
        data, status = extract_window(self.blob, 1000, 2000, 1056)
        self.assertIsNone(data)
        self.assertEqual(status, "missing_left")

    def test_truncated_tail_is_rejected(self):
        data, status = extract_window(self.blob, 100, 1000, 1056)
        self.assertIsNone(data)
        self.assertEqual(status, "truncated_tail")


class CliAdapterTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.snapshot = Path(cls.tmp.name) / "snap"
        cls.snapshot.mkdir()
        for name in ("toa_estimator.c", "toa_estimator.h"):
            src = ROOT / "App" / "Detector" / name
            if src.exists():
                shutil.copy(src, cls.snapshot / name)
            else:
                content = run_git(
                    [f"show", "c79bf25:App/Detector/%s" % name]).stdout
                (cls.snapshot / name).write_text(content, encoding="utf-8")
        cls.exe = Path(cls.tmp.name) / "cli.exe"
        cc = shutil.which("gcc")
        result = subprocess.run(
            [cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic",
             "-I", str(cls.snapshot),
             str(Path(__file__).resolve().parents[1] / "toa_replay_cli.c"),
             str(cls.snapshot / "toa_estimator.c"), "-o", str(cls.exe), "-lm"],
            capture_output=True, text=True, timeout=120)
        if result.returncode != 0:
            raise RuntimeError(result.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_256_samples_valid(self):
        window = Path(self.tmp.name) / "ok.pcm"
        window.write_bytes(make_pcm(256))
        out = run_cli(self.exe, [window])
        self.assertEqual(out[str(window)]["VALID"], "1")
        fraction = float(out[str(window)]["FRACTION"])
        self.assertTrue(0.0 <= fraction < 256.0)

    def test_255_samples_rejected(self):
        window = Path(self.tmp.name) / "short.pcm"
        window.write_bytes(make_pcm(255))
        out = run_cli(self.exe, [window])
        self.assertEqual(out[str(window)]["VALID"], "0")
        self.assertEqual(out[str(window)]["REASON"], "window_size")

    def test_missing_file_rejected(self):
        window = Path(self.tmp.name) / "absent.pcm"
        out = run_cli(self.exe, [window])
        self.assertEqual(out[str(window)]["VALID"], "0")


class PipelineTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        make_fixture(
            self.root, "t1",
            [(1, 900, 2000), (2, 52000, 2000)],
            [(1, 1056), (2, 52856)])
        output_root = self.root / "Replay"
        self.args = ["--trials", "t1", "--output-root", str(output_root),
                     "--root", str(self.root / "Logs" / "Chirp"),
                     "--assets-dir", str(self.root / "NoAssets"),
                     "--skip-synthetic"]

    def tearDown(self):
        self.tmp.cleanup()

    def pipeline(self):
        exe = sys.executable
        result = subprocess.run(
            [exe, "-B", str(Path(__file__).resolve().parents[1] / "toa_replay.py")]
            + self.args, capture_output=True, text=True, timeout=300)
        return result

    def test_pipeline_conservation_and_background_retention(self):
        result = self.pipeline()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        runs = sorted((self.root / "Replay").iterdir())
        run_dir = runs[-1]
        summary = json.loads((run_dir / "summary.json").read_text(encoding="utf-8"))
        self.assertEqual(summary["hard_failures"], [])
        rows = [json.loads(line) for line in
                (run_dir / "replay.jsonl").open(encoding="utf-8")]
        windows = [r for r in rows if r["kind"] == "WINDOW"]
        self.assertEqual(len(windows), 2)
        self.assertTrue(all(r["offline_window_status"] == "OK" for r in windows))
        self.assertTrue(all(r["online_window_status"] == "UNKNOWN" for r in windows))

    def test_pipeline_is_reproducible(self):
        self.pipeline()
        runs = sorted((self.root / "Replay").iterdir())
        first = runs[-1]
        self.pipeline()
        runs = sorted((self.root / "Replay").iterdir())
        second = runs[-1]
        self.assertNotEqual(first, second)
        def canonical(path):
            rows = []
            for line in (path / "replay.jsonl").open(encoding="utf-8"):
                row = json.loads(line)
                row.pop("trial", None)
                rows.append(json.dumps(row, sort_keys=True))
            return rows
        self.assertEqual(canonical(first), canonical(second))


if __name__ == "__main__":
    unittest.main()