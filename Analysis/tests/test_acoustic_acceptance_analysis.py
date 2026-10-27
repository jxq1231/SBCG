"""O1 fixed-slot ledger tests; no hardware, audio, or network."""
from __future__ import annotations

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "Analysis" / "acoustic_acceptance_analysis.py"
SPEC = importlib.util.spec_from_file_location("acoustic_acceptance_analysis", MODULE_PATH)
assert SPEC and SPEC.loader
analysis = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(analysis)


def manifest(path: Path, slot_times: list[float]) -> None:
    path.write_text(json.dumps({"slot_times_s": slot_times}), encoding="utf-8")


def pair_line(distance_mm: int, direction: str = "A") -> str:
    return ("$SOUND_PAIR,STATUS=OK,DELTA_NS=933000,DIST_MM=" + str(distance_mm) +
            ",DIR=" + direction + ",A_SEQ=1,B_SEQ=2,DOMAIN=7,QUALITY=98,"
            "SPEED_MM_S=343000,DIAG=1,LOCAL=1,SYNC=0\r\n")


def jsonl(path: Path, rows: list[dict]) -> None:
    path.write_text("\n".join(json.dumps(row, ensure_ascii=False) for row in rows) + "\n",
                    encoding="utf-8")


class AcceptanceAnalysisTests(unittest.TestCase):
    def slots(self) -> list[float]:
        return [10.0 + 2.0 * index for index in range(10)]

    def test_background_does_not_occupy_slot_and_fixed_ten_rows(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            mf = root / "manifest.json"
            manifest(mf, self.slots())
            a = root / "A.jsonl"
            b = root / "B.jsonl"
            jsonl(a, [
                {"elapsed_s": 5.0, "line": pair_line(999)},
                {"elapsed_s": 10.1, "line": pair_line(315)},
                {"elapsed_s": 10.1, "line":
                 "$RANGE_DECISION,SCHEMA=1,SEQ=1,DOMAIN=7,A_SESS=1,A_SEQ=1,"
                 "B_SESS=1,B_SEQ=2,RAW_NS=933000,DEC=ACCEPT,REASON=FORMAL,ID=1,FORMAL=1,PROV=0\r\n"},
                {"elapsed_s": 10.2, "line":
                 "$RANGE_RESULT,SCHEMA=1,REV=NA,ID=1,STATE=VALID,MM=315,DIR=A,"
                 "SRC_NS=1000,AGE_MS=50,HAS_VALUE=1,FRESH=1,EVIDENCE=PROXY\r\n"},
                {"elapsed_s": 12.1, "line": pair_line(316)},
            ])
            jsonl(b, [
                {"elapsed_s": 10.2, "line":
                 "$UI_RANGE,SCHEMA=1,REV=NA,ID=1,STATE=VALID,MM=315,DIR=A,AGE_MS=50,"
                 "HAS_VALUE=1,FRESH=1\r\n"},
            ])
            out = root / "out"
            code = analysis.main(["--manifest", str(mf), "--a", str(a),
                                  "--b", str(b), "--out", str(out)])
            self.assertEqual(code, 0)
            lines = (out / "ledger.csv").read_text(encoding="utf-8").splitlines()
            self.assertEqual(len(lines), 11)
            summary = json.loads((out / "summary.json").read_text(encoding="utf-8"))
            self.assertEqual(summary["expected_slots"], 10)
            self.assertEqual(summary["counts"]["background_records"], 1)
            self.assertEqual(summary["counts"]["slots_with_pair"], 2)
            integrity = json.loads((out / "integrity.json").read_text(encoding="utf-8"))
            self.assertEqual(integrity["status"], "OK")

    def test_slot0_missing_and_tail_result_are_explicit(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            mf = root / "manifest.json"
            manifest(mf, self.slots())
            a = root / "A.jsonl"
            b = root / "B.jsonl"
            jsonl(a, [
                {"elapsed_s": 12.1, "line": pair_line(317)},
                {"elapsed_s": 13.8, "line":
                 "$RANGE_RESULT,SCHEMA=1,REV=NA,ID=2,STATE=VALID,MM=317,DIR=A,"
                 "SRC_NS=2000,AGE_MS=100,HAS_VALUE=1,FRESH=1,EVIDENCE=PROXY\r\n"},
            ])
            jsonl(b, [])
            out = root / "out"
            self.assertEqual(analysis.main(["--manifest", str(mf), "--a", str(a),
                                            "--b", str(b), "--out", str(out)]), 0)
            summary = json.loads((out / "summary.json").read_text(encoding="utf-8"))
            self.assertIn("0", summary["missing_pair_slots"])
            self.assertEqual(summary["counts"]["slots_with_result"], 1)

    def test_duplicate_pair_is_counted_not_deleted(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            mf = root / "manifest.json"
            manifest(mf, self.slots())
            a = root / "A.jsonl"
            b = root / "B.jsonl"
            jsonl(a, [
                {"elapsed_s": 10.1, "line": pair_line(315)},
                {"elapsed_s": 10.2, "line": pair_line(316)},
            ])
            jsonl(b, [])
            out = root / "out"
            self.assertEqual(analysis.main(["--manifest", str(mf), "--a", str(a),
                                            "--b", str(b), "--out", str(out)]), 0)
            summary = json.loads((out / "summary.json").read_text(encoding="utf-8"))
            self.assertEqual(summary["counts"]["duplicate_pairs"], 1)
            row = (out / "ledger.csv").read_text(encoding="utf-8").splitlines()[1]
            self.assertIn(",2,0,", row)

    def test_mixed_clock_keys_are_invalid(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            mf = root / "manifest.json"
            manifest(mf, self.slots())
            a = root / "A.jsonl"
            b = root / "B.jsonl"
            jsonl(a, [
                {"elapsed_s": 10.1, "line": pair_line(315)},
                {"t": 12.1, "line": pair_line(316)},
            ])
            jsonl(b, [])
            out = root / "out"
            code = analysis.main(["--manifest", str(mf), "--a", str(a),
                                  "--b", str(b), "--out", str(out)])
            self.assertEqual(code, 1)
            integrity = json.loads((out / "integrity.json").read_text(encoding="utf-8"))
            self.assertEqual(integrity["status"], "EVIDENCE_INVALID")
            self.assertTrue(any("mixed clock" in item for item in integrity["problems"]))


if __name__ == "__main__":
    unittest.main()
