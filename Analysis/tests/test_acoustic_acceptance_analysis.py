"""O1 fixed-slot ledger tests; no hardware, audio, or network."""
from __future__ import annotations

import importlib.util
import contextlib
import csv
import io
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
            self.assertEqual(summary["counts"]["slots_with_result"], 0)
            self.assertEqual(summary["metric_availability"]["identity"], "UNAVAILABLE")

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


def v2_manifest() -> dict:
    return {"schema": 2, "slot_times_s": [10.0 + 2.0 * i for i in range(10)],
            "reference_uncertainty_s": 0.005,
            "clock_alignment": {
                "A": {"clock_key": "elapsed_s", "offset_s": 0, "uncertainty_s": 0.002},
                "B": {"clock_key": "elapsed_s", "offset_s": 4, "uncertainty_s": 0.003}}}


def row(time_s: float, line: str) -> dict:
    return {"elapsed_s": time_s, "line": line}


def decision(seq: int = 1, source: int = 1, extra: str = "") -> str:
    return (f"$RANGE_DECISION,SCHEMA=2,SEQ={seq},DOMAIN=7,A_SESS=11,A_SEQ={source},"
            f"B_SESS=22,B_SEQ={source},RAW_NS=933000,DEC=ACCEPT,REASON=PROXY{extra}")


def link(seq: int = 1, identity: int = 1, rev: int = 1, new: int = 1,
         epoch: int = 3, extra: str = "") -> str:
    return (f"$RANGE_LINK,SCHEMA=2,SEQ={seq},DOMAIN=7,EPOCH={epoch},REV={rev},"
            f"ID={identity},NEW={new},FORMAL=0,PROV=1,SRC_NS=1000{extra}")


def snapshot(tag: str = "$RANGE_RESULT", identity: int = 1, rev: int = 1,
             state: str = "CONFIRMING", mm: int = 315, epoch: int = 3,
             extra: str = "") -> str:
    return (f"{tag},SCHEMA=2,DOMAIN=7,EPOCH={epoch},REV={rev},ID={identity},"
            f"STATE={state},MM={mm},DIR=A,SRC_NS=1000,AGE_MS=50,"
            f"HAS_VALUE=1,FRESH=1,FORMAL=0,PROV=1{extra}")


class IdentityLedgerTests(unittest.TestCase):
    def run_evidence(self, a: list[dict], b: list[dict] | None = None,
                     mf: object | None = None) -> tuple:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "manifest.json").write_text(
                json.dumps(v2_manifest() if mf is None else mf), encoding="utf-8")
            jsonl(root / "A.jsonl", a)
            jsonl(root / "B.jsonl", b or [])
            out = root / "out"
            with contextlib.redirect_stdout(io.StringIO()):
                code = analysis.main(["--manifest", str(root / "manifest.json"),
                                      "--a", str(root / "A.jsonl"),
                                      "--b", str(root / "B.jsonl"), "--out", str(out)])
            summary = json.loads((out / "summary.json").read_text(encoding="utf-8"))
            integrity = json.loads((out / "integrity.json").read_text(encoding="utf-8"))
            with (out / "ledger.csv").open(encoding="utf-8", newline="") as handle:
                ledger = list(csv.DictReader(handle))
            audit = ([json.loads(text) for text in (out / "records.jsonl").read_text(
                encoding="utf-8").splitlines()] if (out / "records.jsonl").exists() else [])
            return code, summary, integrity, ledger, audit

    def test_split_link_keeps_tail_result_and_ui_in_original_slot(self) -> None:
        code, summary, _, ledger, _ = self.run_evidence(
            [row(12.1, decision()), row(12.11, link()), row(33, snapshot())],
            [row(30, snapshot("$UI_RANGE"))])
        self.assertEqual(code, 0)
        self.assertEqual(ledger[1]["result_id"], "1")
        self.assertEqual(ledger[1]["ui_id"], "1")
        self.assertEqual(ledger[9]["has_result"], "0")
        self.assertEqual(summary["counts"]["new_measurements"], 1)
        self.assertEqual(summary["counts"]["ui_matches"], 1)
        self.assertAlmostEqual(float(ledger[1]["first_ui_log_latency_s"]), 22)

    def test_hold_does_not_bind_rejected_source_or_fill_later_slots(self) -> None:
        code, summary, _, ledger, audit = self.run_evidence([
            row(10.1, decision()), row(10.11, link()), row(10.2, snapshot()),
            row(12.1, decision(2, 2)), row(12.11, link(2, rev=2, new=0)),
            row(12.2, snapshot(rev=2, state="HOLD")),
            row(14.2, snapshot(rev=2, state="HOLD"))])
        self.assertEqual(code, 0)
        self.assertEqual(summary["counts"]["new_measurements"], 1)
        self.assertEqual(summary["counts"]["slots_with_result"], 1)
        self.assertEqual(summary["counts"]["hold_records"], 2)
        self.assertEqual(ledger[1]["has_result"], "0")
        self.assertEqual(ledger[2]["has_result"], "0")
        self.assertEqual(len(audit), 7)

    def test_unrelated_ui_is_invalid_instead_of_receipt_time_guess(self) -> None:
        code, summary, integrity, _, _ = self.run_evidence(
            [row(10.1, decision()), row(10.11, link()), row(10.2, snapshot())],
            [row(6.3, snapshot("$UI_RANGE", identity=777))])
        self.assertEqual(code, 1)
        self.assertEqual(summary["counts"]["ui_matches"], 0)
        self.assertTrue(any("unbound" in text for text in integrity["problems"]))

    def test_full_decision_keys_support_inline_link(self) -> None:
        fields = ",EPOCH=3,REV=1,ID=1,NEW=1,FORMAL=0,PROV=1,SRC_NS=1000"
        code, summary, _, ledger, _ = self.run_evidence(
            [row(10.1, decision(extra=fields)), row(12.5, snapshot())])
        self.assertEqual(code, 0)
        self.assertEqual(summary["counts"]["new_measurements"], 1)
        self.assertEqual(ledger[0]["result_id"], "1")

    def test_conflicting_same_state_key_is_invalid(self) -> None:
        code, _, integrity, _, _ = self.run_evidence([
            row(10.1, decision()), row(10.11, link()),
            row(10.2, snapshot()), row(10.3, snapshot(mm=999))])
        self.assertEqual(code, 1)
        self.assertTrue(any("conflicting" in text for text in integrity["problems"]))

    def test_reused_id_in_new_epoch_is_separate_measurement(self) -> None:
        code, summary, _, ledger, _ = self.run_evidence([
            row(10.1, decision()), row(10.11, link()), row(10.2, snapshot()),
            row(12.1, decision(2, 2)), row(12.11, link(2, epoch=4)),
            row(12.2, snapshot(epoch=4))])
        self.assertEqual(code, 0)
        self.assertEqual(summary["counts"]["new_measurements"], 2)
        self.assertEqual([ledger[i]["has_result"] for i in (0, 1)], ["1", "1"])

    def test_missing_clock_alignment_disables_cross_board_metrics(self) -> None:
        mf = v2_manifest()
        del mf["clock_alignment"]["B"]
        code, summary, integrity, _, _ = self.run_evidence(
            [row(10.1, decision()), row(10.11, link()), row(10.2, snapshot())],
            [row(6.3, snapshot("$UI_RANGE"))], mf)
        self.assertEqual(code, 1)
        self.assertEqual(summary["counts"]["ui_matches"], 0)
        self.assertTrue(any("clock_alignment" in text for text in integrity["problems"]))

    def test_missing_split_decision_is_unbound(self) -> None:
        code, _, integrity, _, _ = self.run_evidence([row(10.1, link())])
        self.assertEqual(code, 1)
        self.assertTrue(any("unbound" in text for text in integrity["problems"]))

    def test_ambiguous_source_slot_is_invalid(self) -> None:
        code, summary, integrity, _, _ = self.run_evidence([
            row(11, decision()), row(11.01, link()), row(11.1, snapshot())])
        self.assertEqual(code, 1)
        self.assertEqual(summary["counts"]["new_measurements"], 0)
        self.assertTrue(any("ambiguous" in text for text in integrity["problems"]))

    def test_empty_and_malformed_manifests_still_write_ten_rows(self) -> None:
        for mf in ({}, [], {"slot_times_s": []},
                   {"playback_event_onsets_s": [0] * 10,
                    "capture_play_start_s": "bad"}):
            with self.subTest(manifest=mf):
                code, summary, integrity, ledger, _ = self.run_evidence([], mf=mf)
                self.assertEqual(code, 1)
                self.assertEqual(len(ledger), 10)
                self.assertEqual(summary["expected_slots"], 10)
                self.assertEqual(integrity["status"], "EVIDENCE_INVALID")

    def test_invalid_onset_keeps_original_slot_positions(self) -> None:
        mf = v2_manifest()
        mf["slot_times_s"][2] = "bad"
        code, _, _, ledger, _ = self.run_evidence([row(16.1, pair_line(315))], mf=mf)
        self.assertEqual(code, 1)
        self.assertEqual(len(ledger), 10)
        self.assertEqual(ledger[2]["expected_s"], "")
        self.assertEqual(ledger[3]["expected_s"], "16.000000")
        self.assertEqual(ledger[3]["a_pair_count"], "1")

    def test_background_and_legacy_snapshots_are_retained_without_new_metrics(self) -> None:
        code, summary, _, _, audit = self.run_evidence([
            row(1, "$OTHER,NOTE=background"), row(2, pair_line(315)),
            row(10.2, "$RANGE_RESULT,SCHEMA=1,REV=NA,ID=1,STATE=VALID,MM=315")],
            mf={"slot_times_s": v2_manifest()["slot_times_s"]})
        self.assertEqual(code, 0)
        self.assertEqual(summary["counts"]["new_measurements"], 0)
        self.assertEqual(summary["counts"]["slots_with_result"], 0)
        self.assertEqual(summary["metric_availability"]["identity"], "UNAVAILABLE")
        self.assertEqual(len(audit), 3)

    def test_common_time_reference_anchors_delayed_source_diagnostic(self) -> None:
        mf = v2_manifest()
        mf["common_time_reference"] = {"common_ns": 1000, "capture_s": 12,
                                       "uncertainty_s": 0.001}
        code, _, _, ledger, _ = self.run_evidence([
            row(16.1, decision()), row(16.11, link()), row(33, snapshot())], mf=mf)
        self.assertEqual(code, 0)
        self.assertEqual(ledger[1]["has_result"], "1")
        self.assertEqual(ledger[1]["source_time_basis"], "common_time_reference")

    def test_full_pair_source_key_precedes_delayed_decision_receipt(self) -> None:
        code, _, _, ledger, _ = self.run_evidence([
            row(12.1, pair_line(315).replace("A_SEQ=1,B_SEQ=2", "A_SESS=11,A_SEQ=1,B_SESS=22,B_SEQ=1")),
            row(16.1, decision()), row(16.11, link()), row(33, snapshot())])
        self.assertEqual(code, 0)
        self.assertEqual(ledger[1]["has_result"], "1")
        self.assertEqual(ledger[1]["source_time_basis"], "pair_receipt_proxy")

    def test_unmatched_revision_does_not_claim_ui_agreement(self) -> None:
        code, summary, integrity, _, _ = self.run_evidence(
            [row(10.1, decision()), row(10.11, link()), row(10.2, snapshot())],
            [row(6.3, snapshot("$UI_RANGE", rev=2))])
        self.assertEqual(code, 1)
        self.assertEqual(summary["counts"]["ui_matches"], 0)
        self.assertTrue(any("unbound" in text for text in integrity["problems"]))

    def test_ui_payload_disagreement_is_explicit(self) -> None:
        code, summary, integrity, _, _ = self.run_evidence(
            [row(10.1, decision()), row(10.11, link()), row(10.2, snapshot())],
            [row(6.3, snapshot("$UI_RANGE", mm=999))])
        self.assertEqual(code, 1)
        self.assertEqual(summary["counts"]["ui_matches"], 0)
        self.assertEqual(summary["counts"]["ui_mismatches"], 1)
        self.assertTrue(any("mismatch" in text for text in integrity["problems"]))

    def test_missing_link_partner_is_explicitly_unbound(self) -> None:
        code, _, integrity, _, _ = self.run_evidence([row(10.1, decision())])
        self.assertEqual(code, 1)
        self.assertTrue(any("unbound" in text for text in integrity["problems"]))

    def test_malformed_snapshot_flags_cannot_count_as_valid_result(self) -> None:
        code, summary, integrity, _, _ = self.run_evidence([
            row(10.1, decision()), row(10.11, link()), row(10.2, snapshot(extra=",FRESH=2"))])
        self.assertEqual(code, 1)
        self.assertEqual(summary["counts"]["slots_with_result"], 0)
        self.assertTrue(integrity["problems"])

    def test_no_new_upgrade_classifies_same_measurement_without_rebinding_source(self) -> None:
        code, summary, _, ledger, _ = self.run_evidence([
            row(10.1, decision()), row(10.11, link()), row(10.2, snapshot()),
            row(12.1, decision(2, 2)), row(12.11, link(2, rev=2, new=0, extra=",FORMAL=1,PROV=0")),
            row(12.2, snapshot(rev=2, extra=",FORMAL=1,PROV=0"))])
        self.assertEqual(code, 0)
        self.assertEqual(summary["counts"]["new_measurements"], 1)
        self.assertEqual(summary["counts"]["formal_measurements"], 1)
        self.assertEqual(summary["counts"]["provisional_measurements"], 1)
        self.assertEqual(summary["counts"]["missing_measurements"], 9)
        self.assertEqual(ledger[1]["new_measurement_count"], "0")

    def test_multiple_new_measurements_in_one_slot_are_ambiguous(self) -> None:
        code, _, integrity, _, _ = self.run_evidence([
            row(10.1, decision()), row(10.11, link()), row(10.2, snapshot()),
            row(10.3, decision(2, 2)), row(10.31, link(2, identity=2, rev=2)),
            row(10.4, snapshot(identity=2, rev=2))])
        self.assertEqual(code, 1)
        self.assertTrue(any("multiple" in text for text in integrity["problems"]))

    def test_first_ui_fields_follow_aligned_clock_order(self) -> None:
        code, _, _, ledger, _ = self.run_evidence([
            row(10.1, decision()), row(10.11, link()), row(10.15, snapshot()),
            row(10.2, snapshot("$UI_RANGE")),
            row(10.21, decision(2, 2)), row(10.22, link(2, rev=2, new=0)),
            row(10.25, snapshot(rev=2, state="VALID"))],
            [row(6.3, snapshot("$UI_RANGE", rev=2, state="VALID"))])
        self.assertEqual(code, 0)
        self.assertEqual(ledger[0]["ui_state"], "CONFIRMING")

    def test_unspecified_wav_uncertainty_does_not_invent_latency_bounds(self) -> None:
        mf = v2_manifest()
        del mf["reference_uncertainty_s"]
        code, _, _, ledger, _ = self.run_evidence(
            [row(10.1, decision()), row(10.11, link()), row(10.2, snapshot())],
            [row(6.3, snapshot("$UI_RANGE"))], mf)
        self.assertEqual(code, 0)
        self.assertEqual(ledger[0]["first_ui_log_latency_min_s"], "")
        self.assertEqual(ledger[0]["first_ui_log_latency_max_s"], "")

    def test_nonfinite_alignment_is_invalid_and_not_copied_into_output_json(self) -> None:
        mf = v2_manifest()
        mf["clock_alignment"]["A"]["offset_s"] = float("nan")
        code, summary, _, ledger, _ = self.run_evidence([], mf=mf)
        self.assertEqual(code, 1)
        self.assertEqual(len(ledger), 10)
        self.assertNotIn("A", summary["clock_alignment"])


if __name__ == "__main__":
    unittest.main()
