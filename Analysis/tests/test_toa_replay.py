"""Host tests for the TOA replay pipeline (self-contained temp fixtures).

Verifies window slicing rules, the C adapter contract, full-pipeline
conservation with reserved background events, and input reproducibility.
Real firmware files are not touched.
"""
import json
import copy
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
                (run_dir / "replay.jsonl").read_text(
                    encoding="utf-8").splitlines()]
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
            for line in (path / "replay.jsonl").read_text(
                    encoding="utf-8").splitlines():
                row = json.loads(line)
                row.pop("trial", None)
                rows.append(json.dumps(row, sort_keys=True))
            return rows
        self.assertEqual(canonical(first), canonical(second))

    def test_prior_envelope_adds_third_version(self):
        # A --prior-envelope run dir freezes original_envelope as a third version.
        prior = self.root / "prior_run" / "source_snapshots" / "current_envelope"
        prior.mkdir(parents=True, exist_ok=True)
        for name in ("toa_estimator.c", "toa_estimator.h"):
            shutil.copy(ROOT / "App" / "Detector" / name, prior / name)
        self.args += ["--prior-envelope", str(self.root / "prior_run")]
        result = self.pipeline()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        run_dir = sorted((self.root / "Replay").iterdir())[-1]
        rows = [json.loads(line) for line in
                (run_dir / "replay.jsonl").read_text(
                    encoding="utf-8").splitlines()]
        versions = {rw["version"] for row in rows
                    for rw in row.get("replays", [])}
        self.assertEqual(versions, {"peak_backtrack", "original_envelope",
                                    "current_envelope"})

    def test_ledger_problem_fails_tool_check(self):
        # F1: capture ID regression is a ledger-level format problem. It must
        # propagate into per_trial.problems/format_errors and make TOOL_CHECK
        # fail instead of silently passing with exit 0.
        tmp = Path(self.tmp.name) / "ledgerbad"
        trial = "t1"
        trial_dir = tmp / "Logs" / "Chirp" / trial
        pcm_dir = tmp / "Logs" / "Chirp" / "PCM" / trial
        trial_dir.mkdir(parents=True)
        pcm_dir.mkdir(parents=True)
        captures = [
            "$CHIRP_CAPTURE,ID=2,SESSION=1,FIRST=200000,FRAMES=1000,"
            "CLAMP=0,TAIL=1,PKTS=2,TOTAL=2,RESULT=OK",
            "$CHIRP_CAPTURE,ID=1,SESSION=1,FIRST=100000,FRAMES=1000,"
            "CLAMP=0,TAIL=1,PKTS=2,TOTAL=2,RESULT=OK",
        ]
        (trial_dir / "run.jsonl").write_text(
            "".join(json.dumps({"t": 0.1 * i, "line": line}) + "\n"
                    for i, line in enumerate(captures)), encoding="utf-8")
        out_root = tmp / "Replay"
        result = subprocess.run(
            [sys.executable, "-B",
             str(Path(__file__).resolve().parents[1] / "toa_replay.py"),
             "--trials", trial, "--output-root", str(out_root),
             "--root", str(tmp / "Logs" / "Chirp"),
             "--assets-dir", str(tmp / "NoAssets"), "--skip-synthetic"],
            capture_output=True, text=True, timeout=300)
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        summary = json.loads(
            (sorted(out_root.iterdir())[-1] / "summary.json").read_text(
                encoding="utf-8"))
        self.assertEqual(summary["conclusions"]["TOOL_CHECK"], "FAIL")
        self.assertTrue(any("capture ids regress" in p
                            for p in summary["format_errors"]))
        self.assertTrue(any("capture ids regress" in p
                            for p in summary["per_trial"][trial]["problems"]))

    def test_frozen_metadata_own_receive_times_and_prelude_format_failure(self):
        import hashlib
        # A p1-04-like capture received near slot 0 does not contain SOUND 0.
        # A D50-like prelude SEARCH defect remains a global tool failure.
        trial = "t1"
        make_fixture(self.root, trial,
                     [(i + 1, 100000 + i * 96000, 1000) for i in range(10)],
                     [(i + 1, 100000 + i * 96000 + (96 if i else -500))
                      for i in range(10)])
        log_path = self.root / "Logs" / "Chirp" / trial / "run.jsonl"
        records = [json.loads(line) for line in log_path.read_text(
            encoding="utf-8").splitlines()]
        for i, record in enumerate(records):
            record["t"] = 13 + (i % 10) * 2 + (.03 if i < 10 else 0)
        records.insert(0, {"t": .1, "line": "$CHIRP_SEARCH,ID=999,SESSION=1,"
                           "HINT=64,BEST=64"})
        log_path.write_text("".join(json.dumps(r) + "\n" for r in records),
                            encoding="utf-8")
        pcm_dir = self.root / "Logs" / "Chirp" / "PCM" / trial
        inputs = playback_input()
        for name, content in (("trial_summary.json", inputs["summary"]["content"]),
                              ("trial_manifest.json", inputs["trial_manifest"]["content"])):
            (pcm_dir / name).write_text(json.dumps(content), encoding="utf-8")
        result = self.pipeline()
        self.assertNotEqual(result.returncode, 0)
        run_dir = sorted((self.root / "Replay").iterdir())[-1]
        summary = json.loads((run_dir / "summary.json").read_text(encoding="utf-8"))
        self.assertEqual(summary["conclusions"]["TOOL_CHECK"], "FAIL")
        self.assertTrue(any("without capture" in p for p in summary["format_errors"]))
        report = summary["diagnostics"]["playback_window"]["by_trial"][trial]
        self.assertTrue(report["capture_log"]["coverage_complete"])
        self.assertEqual(report["linked_sound_pcm"]["missing_slots"], [0])
        ledger = json.loads((run_dir / "ledger.json").read_text(
            encoding="utf-8"))["ledgers"][trial]
        search = ledger["events"][0]
        self.assertEqual((search["host_sec"], search["time_phase"]), (.1, "prelude"))
        capture = next(e for e in ledger["events"] if e["kind"] == "$CHIRP_CAPTURE")
        self.assertEqual(capture["host_sec"], 13.03)
        self.assertAlmostEqual(capture["slot_residual_s"], .03)
        rows = [json.loads(line) for line in (run_dir / "replay.jsonl").read_text(
            encoding="utf-8").splitlines()]
        self.assertEqual(len(rows), 10)
        self.assertEqual(rows[0]["kind"], "SOUND")
        for i, row in enumerate(rows):
            self.assertEqual(row["host_sec"], 13 + i * 2)
            self.assertEqual(row["time_phase"], "playback")
            self.assertEqual(row["expected_slot_id"], i)
            self.assertEqual(row["slot_residual_s"], 0)
        inventory = json.loads((run_dir / "manifest.json").read_text(
            encoding="utf-8"))["inventory"][trial]
        frozen = inventory["trial_manifest"]
        self.assertEqual(frozen["content"], inputs["trial_manifest"]["content"])
        self.assertEqual(frozen["sha256"], hashlib.sha256(
            (pcm_dir / "trial_manifest.json").read_bytes()).hexdigest())
        # With the first SOUND truly contained, the same prelude defect must
        # still fail TOOL_CHECK while playback association covers all ten.
        records[11]["line"] = records[11]["line"].replace(
            "FIRST=99500,CONFIRMED=99627", "FIRST=100096,CONFIRMED=100223")
        log_path.write_text("".join(json.dumps(r) + "\n" for r in records),
                            encoding="utf-8")
        self.assertNotEqual(self.pipeline().returncode, 0)
        next_dir = sorted((self.root / "Replay").iterdir())[-1]
        next_summary = json.loads((next_dir / "summary.json").read_text(
            encoding="utf-8"))
        self.assertEqual(next_summary["conclusions"]["TOOL_CHECK"], "FAIL")
        next_report = next_summary["diagnostics"]["playback_window"]["by_trial"][trial]
        self.assertTrue(next_report["linked_sound_pcm"]["unique_complete"])


class CliSaferTest(unittest.TestCase):
    """Review-driven: space paths, NaN rejection, format-error gating."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.snap = Path(cls.tmp.name) / "snap"
        ver = cls.snap / "peak_backtrack"
        ver.mkdir(parents=True)
        for name in ("toa_estimator.c", "toa_estimator.h"):
            shutil.copy(ROOT / "App" / "Detector" / name, ver / name)
        from toa_replay import compile_cli as build_cli
        run = Path(cls.tmp.name) / "run"
        run.mkdir()
        code, cls.exe = build_cli(run, cls.snap, "peak_backtrack", "-O2")
        if code != 0:
            raise RuntimeError("cli build failed")
        # NaN stub binary
        stub_dir = Path(cls.tmp.name) / "stub"
        stub_dir.mkdir()
        (stub_dir / "toa_estimator.c").write_text(
            '#include "toa_estimator.h"\n#include <math.h>\n'
            "const ToaBackwardConfig ToaBackward_DefaultConfig={256U,128U,8U,4U};\n"
            "const ToaEstimatorConfig ToaEstimator_DefaultConfig={32U,128U,4U};\n"
            "bool ToaEstimator_EstimateBackward(const ToaBackwardConfig *c,"
            " const int16_t *s, double *o){"
            "(void)c;(void)s;*o=NAN;return true;}\n"
            "bool ToaEstimator_Estimate(const ToaEstimatorConfig *c,"
            " const int16_t *s, double *o){(void)c;(void)s;(void)o;return false;}\n",
            encoding="utf-8")
        shutil.copy(ROOT / "App" / "Detector" / "toa_estimator.h",
                    stub_dir / "toa_estimator.h")
        cc = shutil.which("gcc")
        cls.stub_exe = Path(cls.tmp.name) / "stub_cli.exe"
        result = subprocess.run(
            [cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic",
             "-I", str(stub_dir),
             str(Path(__file__).resolve().parents[1] / "toa_replay_cli.c"),
             str(stub_dir / "toa_estimator.c"), "-o", str(cls.stub_exe), "-lm"],
            capture_output=True, text=True, timeout=120)
        if result.returncode != 0:
            raise RuntimeError(result.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_space_in_path_survives(self):
        from toa_replay import run_cli
        space_dir = Path(self.tmp.name) / "space dir"
        space_dir.mkdir()
        window = space_dir / "two_spikes.pcm"
        window.write_bytes(b"\x00\x00" * 256)
        out = run_cli(self.exe, [window])
        self.assertIn(str(window), out)
        self.assertEqual(out[str(window)]["VALID"], "0")

    def test_nan_fraction_rejected(self):
        from toa_replay import run_cli
        window = Path(self.tmp.name) / "ok.pcm"
        window.write_bytes(make_pcm(256))
        out = run_cli(self.stub_exe, [window])
        self.assertEqual(out[str(window)]["VALID"], "0")
        self.assertEqual(out[str(window)]["REASON"], "nonfinite")

    def test_format_errors_fail_tool_check(self):
        import subprocess as sp
        tmp = Path(self.tmp.name) / "bad"
        trial = "t1"
        trial_dir = tmp / "Logs" / "Chirp" / trial
        pcm_dir = tmp / "Logs" / "Chirp" / "PCM" / trial
        trial_dir.mkdir(parents=True)
        pcm_dir.mkdir(parents=True)
        # one good-looking candidate + a capture with a wrong-format pcm
        (trial_dir / "run.jsonl").write_text(
            json.dumps({"t": 1.0, "line": "$CHIRP_CAPTURE,ID=1,SESSION=1,"
                        "FIRST=100000,FRAMES=1000,CLAMP=0,TAIL=1,PKTS=2,"
                        "TOTAL=2,RESULT=OK"}) + "\n" +
            json.dumps({"t": 1.1, "line": "$SOUND_CAND,SEQ=1,SESSION=1,CH=0,"
                        "FIRST=100500,CONFIRMED=100627,E_AC=1,PEAK=1,CLIP=0,"
                        "LOCAL=1,SYNC=0,TOA_US=1000"}) + "\n",
            encoding="utf-8")
        # pcm byte count disagrees with json frames -> format error
        (pcm_dir / "id1.pcm").write_bytes(b"\x00\x00" * 900)
        (pcm_dir / "id1.json").write_text(
            json.dumps({"id": 1, "session": 1, "frames": 1000,
                        "first_frame": 100000}), encoding="utf-8")
        out_root = tmp / "Replay"
        run = sp.run([sys.executable, "-B",
                      str(Path(__file__).resolve().parents[1] / "toa_replay.py"),
                      "--trials", trial,
                      "--output-root", str(out_root),
                      "--root", str(tmp / "Logs" / "Chirp"),
                      "--assets-dir", str(tmp / "NoAssets"),
                      "--skip-synthetic"],
                     capture_output=True, text=True, timeout=300)
        self.assertNotEqual(run.returncode, 0)
        summary = sorted(out_root.iterdir())[-1]
        s = json.loads((summary / "summary.json").read_text(encoding="utf-8"))
        self.assertEqual(s["conclusions"]["TOOL_CHECK"], "FAIL")
        self.assertTrue(s["format_errors"])


class SlotGateTest(unittest.TestCase):
    """Review-driven: cross-round gate must require one unique value per round."""

    def test_one_round_repeated_three_times_is_not_complete(self):
        from toa_replay import aggregate_diagnostics
        rows = []
        base_us = 10_000_000.0
        # Only toaB-07 exists in the data (repeated three times); the other
        # two rounds are absent entirely.
        for trial in ("toaB-20261026-07", "toaB-20261026-07",
                      "toaB-20261026-07"):
            for slot in range(10):
                frac = (base_us + slot * 2_000_000.0) * 48000 / 1e6
                rows.append({"trial": trial, "sound_key": f"w:{trial}:{slot}",
                             "offline_window_status": "OK", "anchor": 0,
                             "replays": [
                                 {"version": "current_envelope", "opt": "-O0",
                                  "runs": [{"valid": True,
                                            "fraction_frames": frac}]}]})
        diagnostics = aggregate_diagnostics(rows, ["current_envelope"])
        group = diagnostics["cross_round_by_group"]["B"]
        self.assertFalse(
            group["per_version"]["current_envelope"]
            ["full_10_slot_consistency_possible"])

    def test_missing_slots_reports_only_absent_slots(self):
        # F3: slots are stored with str keys; missing_slots must compare keys
        # in the same type, otherwise it always reports all ten slots missing.
        from toa_replay import aggregate_diagnostics
        rows = []
        base_us = 10_000_000.0
        for trial in ("toaB-20261026-07", "toaB-20261026-08",
                      "toaB-20261026-09"):
            for slot in range(5):
                frac = (base_us + slot * 2_000_000.0) * 48000 / 1e6
                rows.append({"trial": trial, "sound_key": f"w:{trial}:{slot}",
                             "offline_window_status": "OK", "anchor": 0,
                             "replays": [
                                 {"version": "current_envelope", "opt": "-O0",
                                  "runs": [{"valid": True,
                                            "fraction_frames": frac}]}]})
        diagnostics = aggregate_diagnostics(rows, ["current_envelope"])
        group = diagnostics["cross_round_by_group"]["B"][
            "per_version"]["current_envelope"]
        self.assertEqual(group["missing_slots"], [5, 6, 7, 8, 9])
        self.assertEqual(sorted(group["slots"]), ["0", "1", "2", "3", "4"])

    def test_validity_delta_reports_transition(self):
        # F2: delta statistics must not hide a valid->invalid transition.
        from toa_replay import aggregate_diagnostics
        rows = []
        specs = [("both", True, True), ("lost", True, False),
                 ("gained", False, True)]
        for label, peak_valid, current_valid in specs:
            rows.append({"trial": "toaB-20261026-07",
                         "sound_key": label, "offline_window_status": "OK",
                         "anchor": 0, "replays": [
                             {"version": "peak_backtrack", "opt": "-O0",
                              "runs": [{"valid": peak_valid,
                                        "fraction_frames": 1.0}]},
                             {"version": "current_envelope", "opt": "-O0",
                              "runs": [{"valid": current_valid,
                                        "fraction_frames": 2.0}]}]})
        diagnostics = aggregate_diagnostics(
            rows, ["peak_backtrack", "current_envelope"])
        delta = diagnostics["version_validity_delta"][
            "peak_backtrack_to_current_envelope"]
        self.assertEqual(delta["both_valid"], 1)
        self.assertIn("toaB-20261026-07|lost", delta["valid_to_invalid"])
        self.assertIn("toaB-20261026-07|gained", delta["invalid_to_valid"])


def playback_input(start=12.0, duration=20.0, onsets=None):
    """Controlled-trial metadata with independently frozen references."""
    onsets = list(range(1, 20, 2)) if onsets is None else onsets
    metadata = {"wav": {"duration_s": duration,
                        "expected_onsets_s": onsets, "sha256": "a" * 64},
                "playback": {"play_span_api_s": [start, start + 0.01]}}
    return {"summary": {"content": {"play_span": [start, start + 0.01],
                                     "metadata": copy.deepcopy(metadata)}},
            "trial_manifest": {"content": copy.deepcopy(metadata)}}


class PlaybackContextTest(unittest.TestCase):
    def context(self, **kwargs):
        import toa_replay
        self.assertTrue(hasattr(toa_replay, "playback_context"),
                        "a pure metadata-based playback context is required")
        return toa_replay.playback_context(playback_input(**kwargs))

    def test_async_return_is_not_wav_end_and_raw_seconds_are_preserved(self):
        ctx = self.context(start=12.008123456)
        self.assertEqual(ctx["status"], "OK")
        self.assertEqual(ctx["api_start_s"], 12.008123456)
        self.assertEqual(ctx["api_return_s"], 12.018123456)
        self.assertEqual(ctx["wav_end_s"], 32.008123456)
        self.assertEqual(ctx["analysis_end_s"], 34.008123456)
        self.assertEqual(ctx["receive_guard_s"], 2.0)
        self.assertEqual(ctx["slot_tolerance_s"], 0.95)
        self.assertEqual(ctx["basis"], "host_serial_receive_approx")

    def test_bad_missing_or_conflicting_references_are_unknown(self):
        import toa_replay
        self.assertTrue(hasattr(toa_replay, "playback_context"),
                        "reference validation is missing")
        cases = [{}, playback_input(), playback_input(), playback_input(),
                 playback_input(), playback_input(), playback_input(),
                 playback_input(), playback_input(), playback_input()]
        cases[1]["summary"]["content"]["play_span"][0] = float("nan")
        cases[2]["summary"]["content"]["play_span"][0] = "12"
        cases[3]["trial_manifest"]["content"]["wav"]["duration_s"] = 21
        cases[4]["trial_manifest"]["content"]["wav"]["sha256"] = "b" * 64
        cases[5]["trial_manifest"]["content"]["wav"]["expected_onsets_s"][0] = 0
        cases[6]["summary"]["content"]["metadata"]["wav"]["duration_s"] = float("inf")
        cases[7]["trial_manifest"]["content"]["playback"]["play_span_api_s"][0] = 13
        del cases[8]["trial_manifest"]
        cases[9]["summary"]["content"]["metadata"]["wav"]["expected_onsets_s"] = []
        for item in cases:
            with self.subTest(item=item):
                self.assertEqual(toa_replay.playback_context(item)["status"],
                                 "UNKNOWN")

    def test_receive_boundaries_tolerance_and_invalid_event_times(self):
        from toa_replay import classify_playback_time
        ctx = self.context()
        expected = [(11.999, "prelude", None), (12.0, "playback", None),
                    (13 - .95, "playback", 0), (13 + .95, "playback", 0),
                    (13 + .95001, "playback", None),
                    (32.001, "playback", None), (34.0, "postlude", None)]
        for host_sec, phase, slot in expected:
            row = classify_playback_time(host_sec, ctx)
            self.assertEqual((row["time_phase"], row["expected_slot_id"]),
                             (phase, slot))
        for host_sec in (None, "13", True, -1, float("nan"), float("inf"), 10**400):
            self.assertEqual(classify_playback_time(host_sec, ctx)["time_phase"],
                             "unknown")
        self.assertEqual(classify_playback_time(13, {"status": "UNKNOWN"})[
            "time_phase"], "unknown")

    def test_overlapping_onset_tolerance_is_explicitly_ambiguous(self):
        from toa_replay import classify_playback_time
        row = classify_playback_time(13.5, self.context(onsets=[1, 2]))
        self.assertEqual(row["slot_status"], "ambiguous")
        self.assertIsNone(row["expected_slot_id"])
        self.assertEqual(row["slot_candidates"], [0, 1])


class PlaybackCoverageTest(unittest.TestCase):
    versions = ["peak_backtrack", "current_envelope"]

    def fixture(self, slots=range(10), missing_pcm=(), extras=()):
        from toa_replay import playback_context, classify_playback_time
        ctx = playback_context(playback_input())
        events, edges, rows = [], [], []
        for index, slot in enumerate(list(slots) + list(extras), start=1):
            host = 13 + slot * 2
            key = f"sound:{index}"
            capkey = f"capture:{index}"
            classification = classify_playback_time(host, ctx)
            sound = {"kind": "$SOUND_CAND", "key": key, "trial": "t1",
                     "line_no": index * 2, "host_sec": host,
                     "fields": {"SEQ": str(index)}, **classification}
            capture = {"kind": "$CHIRP_CAPTURE", "key": capkey,
                       "trial": "t1", "line_no": index * 2 + 1,
                       "host_sec": host + .03, "fields": {"ID": str(index)},
                       **classify_playback_time(host + .03, ctx)}
            events += [sound, capture]
            if slot in missing_pcm:
                rows.append({"trial": "t1", "kind": "SOUND", "sound_key": key,
                             "host_sec": host, "offline_window_status": "no_pcm",
                             "replays": [], **classification})
                continue
            edges += [{"source": key, "target": capkey,
                       "kind": "sound-capture", "status": "UNIQUE"},
                      {"source": f"pcm:t1:{index}", "target": capkey,
                       "kind": "pcm-capture", "status": "LINKED"}]
            rows.append({"trial": "t1", "kind": "WINDOW", "sound_key": key,
                         "host_sec": host, "anchor": index * 96000,
                         "pcm_id": index, "edge_status": "UNIQUE",
                         "offline_window_status": "OK", **classification,
                         "replays": [{"version": v, "opt": "-O0", "runs": [
                             {"valid": True, "fraction_frames": 1.0}]}
                                     for v in self.versions]})
        return ctx, {"events": events, "edges": edges}, rows

    def diagnostics(self, ctx, ledger, rows):
        from toa_replay import aggregate_diagnostics
        return aggregate_diagnostics(rows, self.versions,
                                     playback_contexts={"t1": ctx},
                                     ledgers={"t1": ledger})

    def test_background_preserved_but_not_in_playback_coverage_or_delta(self):
        ctx, ledger, rows = self.fixture(extras=[-1, 11])
        rows[0]["replays"][1]["runs"][0]["valid"] = False
        # Keep a background-only validity loss separate from playback loss.
        rows[-2]["replays"][1]["runs"][0]["valid"] = False
        diag = self.diagnostics(ctx, ledger, rows)
        report = diag["playback_window"]["by_trial"]["t1"]
        self.assertEqual(report["phase_counts"]["$SOUND_CAND"],
                         {"prelude": 1, "playback": 10, "postlude": 1,
                          "unknown": 0})
        self.assertTrue(report["capture_log"]["unique_complete"])
        self.assertTrue(report["linked_sound_pcm"]["unique_complete"])
        self.assertEqual(report["by_version"]["current_envelope"][
            "missing_slots"], [0])
        self.assertEqual(len(rows), 12)
        name = "peak_backtrack_to_current_envelope"
        self.assertEqual(len(diag["version_validity_delta"][name][
            "valid_to_invalid"]), 2)
        self.assertEqual(len(diag["playback_window"]["version_validity_delta"][
            name]["valid_to_invalid"]), 1)
        self.assertEqual(diag["population"], "legacy_all_events")

    def test_missing_first_and_middle_slots_do_not_shift(self):
        for absent in ([0], [5, 6]):
            ctx, ledger, rows = self.fixture(
                slots=[i for i in range(10) if i not in absent])
            report = self.diagnostics(ctx, ledger, rows)["playback_window"][
                "by_trial"]["t1"]
            for key in ("capture_log", "linked_sound_pcm"):
                self.assertEqual(report[key]["missing_slots"], absent)
                self.assertFalse(report[key]["coverage_complete"])
            self.assertEqual(report["by_version"]["current_envelope"][
                "missing_slots"], absent)

    def test_duplicates_and_extra_are_retained_and_block_unique_complete(self):
        ctx, ledger, rows = self.fixture(extras=[2, 9.7])
        report = self.diagnostics(ctx, ledger, rows)["playback_window"][
            "by_trial"]["t1"]
        for key in ("capture_log", "linked_sound_pcm"):
            self.assertTrue(report[key]["coverage_complete"])
            self.assertFalse(report[key]["unique_complete"])
            self.assertEqual(len(report[key]["duplicates"]["2"]), 2)
            self.assertEqual(len(report[key]["extra"]), 1)
            self.assertEqual(len(report[key]["source_events"]), 12)

    def test_coarse_capture_cannot_promote_missing_sound_pcm_or_valid_toa(self):
        ctx, ledger, rows = self.fixture(missing_pcm=[0, 5, 6])
        report = self.diagnostics(ctx, ledger, rows)["playback_window"][
            "by_trial"]["t1"]
        self.assertTrue(report["capture_log"]["coverage_complete"])
        self.assertEqual(report["linked_sound_pcm"]["missing_slots"], [0, 5, 6])
        self.assertEqual(len(report["linked_sound_pcm"]["orphan"]), 3)
        self.assertEqual(report["by_version"]["current_envelope"][
            "missing_slots"], [0, 5, 6])

    def test_no_events_and_unknown_context_never_pass_coverage(self):
        ctx, ledger, rows = self.fixture(slots=[])
        for context in (ctx, {"status": "UNKNOWN", "problems": ["missing"]}):
            report = self.diagnostics(context, ledger, rows)["playback_window"][
                "by_trial"]["t1"]
            self.assertFalse(report["capture_log"]["coverage_complete"])
            self.assertFalse(report["linked_sound_pcm"]["unique_complete"])

    def test_ten_sounds_eight_captures_keep_slots_five_and_six_missing(self):
        ctx, ledger, rows = self.fixture()
        absent_keys = {"capture:6", "capture:7"}
        ledger["events"] = [e for e in ledger["events"] if e["key"] not in absent_keys]
        ledger["edges"] = [e for e in ledger["edges"] if e["target"] not in absent_keys]
        report = self.diagnostics(ctx, ledger, rows)["playback_window"][
            "by_trial"]["t1"]
        self.assertEqual(report["phase_counts"]["$SOUND_CAND"]["playback"], 10)
        for layer in (report["capture_log"], report["linked_sound_pcm"],
                      report["by_version"]["current_envelope"]):
            self.assertEqual(layer["missing_slots"], [5, 6])
            self.assertFalse(layer["coverage_complete"])

    def test_unknown_time_and_slot_ambiguity_remain_traceable(self):
        from toa_replay import playback_context
        ctx, ledger, rows = self.fixture()
        ledger["events"][0]["host_sec"] = None
        report = self.diagnostics(ctx, ledger, rows)["playback_window"][
            "by_trial"]["t1"]
        self.assertEqual(len(report["linked_sound_pcm"]["unknown"]), 1)
        self.assertEqual(report["linked_sound_pcm"]["missing_slots"], [0])
        overlap = playback_context(playback_input(onsets=[1, 2]))
        ledger["events"][0]["host_sec"] = 13.5
        report = self.diagnostics(overlap, ledger, rows)["playback_window"][
            "by_trial"]["t1"]
        self.assertTrue(any(e["key"] == "sound:1" for e in report[
            "linked_sound_pcm"]["ambiguous"]))
        self.assertFalse(report["linked_sound_pcm"]["unique_complete"])

    def test_ambiguous_associations_never_select_nearest_pcm(self):
        ctx, ledger, rows = self.fixture()
        ledger["edges"][0]["status"] = "AMBIGUOUS"
        ledger["edges"].append({"source": "sound:1", "target": "other",
                                "kind": "sound-capture", "status": "AMBIGUOUS"})
        report = self.diagnostics(ctx, ledger, rows)["playback_window"][
            "by_trial"]["t1"]
        self.assertEqual(report["linked_sound_pcm"]["missing_slots"], [0])
        self.assertEqual(len(report["linked_sound_pcm"]["ambiguous"]), 1)
        self.assertFalse(report["by_version"]["current_envelope"][
            "coverage_complete"])


if __name__ == "__main__":
    unittest.main()
