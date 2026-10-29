"""Failure-mode tests for the PCM event ledger (self-contained temp dirs)."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from pcm_event_ledger import (  # noqa: E402
    assign_domains, build_ledger, parse_trial_records, scan_pcm_directory,
)


def make_trial(root: Path, name: str, lines, pcm_files=(), summary=None):
    trial_dir = root / "Logs/Chirp" / name
    trial_dir.mkdir(parents=True, exist_ok=True)
    pcm_dir = root / "Logs/Chirp" / "PCM" / name
    pcm_dir.mkdir(parents=True, exist_ok=True)
    with (trial_dir / "run.jsonl").open("w", encoding="utf-8") as f:
        for index, text in enumerate(lines):
            f.write(json.dumps({"t": 0.1 * index, "line": text}) + "\n")
    if summary is not None:
        with (trial_dir / "trial_summary.json").open("w", encoding="utf-8") as f:
            json.dump(summary, f)
    for stem, pcm_bytes, meta in pcm_files:
        (pcm_dir / (stem + ".pcm")).write_bytes(pcm_bytes)
        with (pcm_dir / (stem + ".json")).open("w", encoding="utf-8") as f:
            json.dump(meta, f)


def capture(id_, session=1, first=100000, frames=1000):
    return (f"$CHIRP_CAPTURE,ID={id_},SESSION={session},FIRST={first},"
            f"FRAMES={frames},CLAMP=0,TAIL=1,PKTS=2,TOTAL=2,RESULT=OK")


def sound(seq, first, session=1, toa="NA"):
    return (f"$SOUND_CAND,SEQ={seq},SESSION={session},CH=0,FIRST={first},"
            f"CONFIRMED={first + 127},E_AC=1,PEAK=1,CLIP=0,LOCAL=1,SYNC=0,"
            f"TOA_US={toa}")


class LedgerTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def build(self, name, lines, pcm_files=()):
        make_trial(self.root, name, lines, pcm_files)
        records, _ = parse_trial_records(name, self.root / "Logs/Chirp" / name / "run.jsonl")
        assign_domains(records, name)
        entries, scan_problems = scan_pcm_directory(
            self.root / "Logs/Chirp/PCM" / name, name)
        return build_ledger(records, name, entries, scan_problems)

    def test_same_id_across_trials_are_independent(self):
        for trial in ("t1", "t2"):
            ledger = self.build(trial, [capture(1, session=1, first=100000)])
            self.assertEqual(len(ledger.events), 1)
        ledger_a = self.build("t1", [capture(1, session=1)])
        ledger_b = self.build("t2", [capture(1, session=1)])
        self.assertEqual(ledger_a.events[0].domain, "t1::unknown::1")
        self.assertEqual(ledger_b.events[0].domain, "t2::unknown::1")
        self.assertNotEqual(ledger_a.events[0].domain, ledger_b.events[0].domain)

    def test_frame_regression_flags_conflict_and_blocks_links(self):
        ledger = self.build("t", [capture(1, first=100000),
                                  capture(2, first=90000)])
        domain = next(iter(ledger.domains.values()))
        self.assertTrue(domain["conflict"])
        self.assertTrue(any("regresses" in p for p in ledger.problems))
        for edge in ledger.edges:
            self.assertNotEqual(edge.status, "LINKED")

    def test_boot_splits_domains(self):
        lines = [capture(1, first=100000),
                 "BOOT,REASON=RESET",
                 capture(5, first=200000)]
        ledger = self.build("t", lines)
        capture_domains = {e.domain for e in ledger.events
                           if e.kind == "$CHIRP_CAPTURE"}
        self.assertEqual(len(capture_domains), 2)

    def test_seq_vs_id_difference_keeps_association(self):
        # SOUND SEQ=26 has FIRST inside capture ID=29's range; SEQ mismatch
        # between the two gates must not drop the candidate edge.
        lines = [capture(29, first=5200000, frames=8544),
                 sound(26, 5200064)]
        ledger = self.build("t", lines)
        sound_edges = [e for e in ledger.edges if e.kind == "sound-capture"]
        self.assertEqual(len(sound_edges), 1)
        self.assertEqual(sound_edges[0].status, "UNIQUE")
        self.assertIn("29", sound_edges[0].target)

    def test_one_sound_two_compatible_pcms_is_ambiguous(self):
        lines = [capture(1, first=90000, frames=10000),
                 capture(2, first=95000, frames=10000),
                 sound(7, 98000)]
        ledger = self.build("t", lines)
        sound_edges = [e for e in ledger.edges if e.kind == "sound-capture"]
        self.assertEqual(len(sound_edges), 2)
        for edge in sound_edges:
            self.assertEqual(edge.status, "AMBIGUOUS")

    def test_orphans_and_bad_json_are_preserved(self):
        make_trial(self.root, "t", [],
                   pcm_files=[("id2", b"\x00\x00" * 8,
                               {"id": 2, "session": 1, "frames": 8})])
        meta3 = {"id": 3, "session": 1, "frames": 4}
        (self.root / "Logs/Chirp/PCM/t/id3.json").write_text(
            json.dumps(meta3), encoding="utf-8")
        (self.root / "Logs/Chirp/PCM/t/id5.pcm").write_bytes(b"\x00\x00" * 8)
        ledger = self.build("t", [])
        self.assertIn("t:2", ledger.isolation["metadata_conflict"])
        self.assertIn("t:3", ledger.isolation["orphan_json"])
        self.assertIn("t:5", ledger.isolation["orphan_pcm"])
        (self.root / "Logs/Chirp/PCM/t/id4.json").write_text(
            "{bad json", encoding="utf-8")
        (self.root / "Logs/Chirp/PCM/t/id6.json").write_text(
            "[]", encoding="utf-8")
        ledger = self.build("t", [])
        self.assertIn("t:4", ledger.isolation["orphan_json"])
        self.assertIn("t:6", ledger.isolation["orphan_json"])
        self.assertTrue(any("bad json" in p for p in ledger.problems))

    def test_negative_first_frame_regression_in_log_order(self):
        # Same session logs ID2/FIRST200000 then ID1/FIRST100000: must be a
        # regression even though sorting by id would hide it.
        lines = [capture(2, first=200000), capture(1, first=100000)]
        ledger = self.build("t1", lines)
        self.assertTrue(next(iter(ledger.domains.values()))["conflict"])
        self.assertTrue(any("regresses" in p for p in ledger.problems))

    def test_dollar_boot_splits_domains(self):
        lines = [capture(1, first=100000),
                 "$BOOT,FW=x",
                 capture(5, first=200000)]
        ledger = self.build("t", lines)
        capture_domains = {e.domain for e in ledger.events
                           if e.kind == "$CHIRP_CAPTURE"}
        self.assertEqual(len(capture_domains), 2)

    def test_sound_seq_regression_flagged(self):
        lines = [sound(2, 100500), sound(1, 110000), capture(1, first=100000)]
        ledger = self.build("t", lines)
        self.assertTrue(next(iter(ledger.domains.values()))["conflict"])
        self.assertTrue(any("sound seq regresses" in p for p in ledger.problems))

    def test_keys_include_domain_and_first(self):
        # Two boot domains with identical SOUND SEQ1/SESSION1 must produce
        # distinct event keys and distinct replay labels.
        lines = [capture(1, first=100000), sound(1, 100500),
                 "$BOOT,FW=x", capture(5, first=200000), sound(1, 201000)]
        ledger = self.build("t", lines)
        sound_keys = [e.key for e in ledger.events if e.kind == "$SOUND_CAND"]
        self.assertEqual(len(set(sound_keys)), 2)

    def test_pcm_links_only_matching_boot_domain(self):
        # Same capture id before and after boot; pcm metadata matches the
        # first boot only. The second domain must not swallow the pcm edge.
        lines = [capture(1, first=100000, frames=1000),
                 "$BOOT,FW=x", capture(1, first=200000, frames=1000)]
        make_trial(self.root, "t", lines,
                   pcm_files=[("id1", b"\x00\x00" * 2000,
                               {"id": 1, "session": 1, "frames": 1000,
                                "first_frame": 100000})])
        records, _ = parse_trial_records("t", self.root / "Logs/Chirp/t/run.jsonl")
        assign_domains(records, "t")
        entries, scan_problems = scan_pcm_directory(
            self.root / "Logs/Chirp/PCM/t", "t")
        ledger = build_ledger(records, "t", entries, scan_problems)
        pcm_edges = [e for e in ledger.edges if e.kind == "pcm-capture"]
        self.assertEqual([e.status for e in pcm_edges], ["LINKED"])
        self.assertEqual(len(pcm_edges), 1)

    def test_frames_mismatch_is_metadata_conflict(self):
        lines = [capture(1, first=100000, frames=1000)]
        make_trial(self.root, "t", lines,
                   pcm_files=[("id1", b"\x00\x00" * 14,
                               {"id": 1, "session": 1, "frames": 7,
                                "first_frame": 100000})])
        records, _ = parse_trial_records("t", self.root / "Logs/Chirp/t/run.jsonl")
        assign_domains(records, "t")
        entries, scan_problems = scan_pcm_directory(
            self.root / "Logs/Chirp/PCM/t", "t")
        ledger = build_ledger(records, "t", entries, scan_problems)
        self.assertTrue(["id1" for p in ledger.isolation["metadata_conflict"]])
        pcm_edges = [e for e in ledger.edges if e.kind == "pcm-capture"]
        self.assertEqual(len(pcm_edges), 0)

    def test_search_hint_best_cross_check(self):
        lines = ["$CHIRP_SEARCH,ID=1,SESSION=1,HINT=100064,LO=100000,HI=100512,"
                 "BEST=100070,CORR_PPM=100,RMS=1,CLIP=0,INV=1,EDGE=0,MATCH=0,"
                 "WHY=LOW_CORR,N=257,V=257,LR=0,Z=0,I=0",
                 capture(1, first=100000, frames=1000)]
        make_trial(self.root, "t", lines,
                   pcm_files=[("id1", b"\x00\x00" * 1000,
                               {"id": 1, "session": 1, "frames": 500,
                                "first_frame": 100000,
                                "hint_frame": 100066, "raw_first_frame": 100088})])
        records, _ = parse_trial_records("t", self.root / "Logs/Chirp/t/run.jsonl")
        assign_domains(records, "t")
        entries, scan_problems = scan_pcm_directory(
            self.root / "Logs/Chirp/PCM/t", "t")
        ledger = build_ledger(records, "t", entries, scan_problems)
        hits = [p for p in ledger.problems
                if "pcm hint_frame" in p or "pcm raw_first_frame" in p]
        self.assertEqual(len(hits), 2)

    def test_non_numeric_pcm_id_is_listed(self):
        make_trial(self.root, "t", [])
        (self.root / "Logs/Chirp/PCM/t/idX.pcm").write_bytes(b"\x00\x00" * 4)
        ledger = self.build("t", [])
        self.assertTrue(any("non-numeric" in p for p in ledger.problems))

    def test_pcm_first_frame_conflict_with_capture(self):
        make_trial(self.root, "t", [capture(1, first=100000, frames=1000)],
                   pcm_files=[("id1", b"\x00\x00" * 1000,
                               {"id": 1, "session": 1, "frames": 1000,
                                "first_frame": 111111})])
        records, _ = parse_trial_records("t", self.root / "Logs/Chirp/t/run.jsonl")
        assign_domains(records, "t")
        entries, _ = scan_pcm_directory(self.root / "Logs/Chirp/PCM/t", "t")
        ledger = build_ledger(records, "t", entries)
        self.assertTrue(["id1" for p in ledger.isolation["metadata_conflict"]])

    def test_orphan_candidate_kept(self):
        # Sound before the first capture range stays in the ledger.
        lines = [capture(5, first=681280, frames=8544),
                 sound(1, 50000)]
        ledger = self.build("t", lines)
        self.assertEqual(len(ledger.events), 2)
        sound_edges = [e for e in ledger.edges if e.kind == "sound-capture"]
        self.assertEqual(len(sound_edges), 0)
        self.assertTrue(any(e.parse_problem for e in ledger.events
                            if e.kind == "$SOUND_CAND"))

    def test_conservation_counts(self):
        lines = [capture(1, first=100000), sound(1, 100500), sound(2, 101000)]
        ledger = self.build("t", lines)
        kinds = [e.kind for e in ledger.events]
        self.assertEqual(kinds.count("$CHIRP_CAPTURE"), 1)
        self.assertEqual(kinds.count("$SOUND_CAND"), 2)


if __name__ == "__main__":
    unittest.main()