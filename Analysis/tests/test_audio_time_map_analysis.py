"""Host-only real-C replay and evidence tests; no board access."""
import copy
from dataclasses import replace
from fractions import Fraction
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import audio_time_map_analysis as mapping


def fixture(directory, count=90):
    boot = "$BOOT,FW=SBCG-M3.2-ANCHOR,UID_W0=001A002D,UID_W1=30365105,UID_W2=36363631"
    records = [dict(line="preserved previous-boot prefix"), dict(line=boot)]
    for index in range(1, count+1):
        frames = index*47991071//1000
        row = dict(SEQ=str(index), STATUS="OK", VALID="1", SESSION="1", F_LO=str(frames),
                   F_HI=str(frames+1), SEC=str(index), NSEC="0", WIDTH="1", SYNC="0",
                   OK=str(index), REJECTS="0")
        records.append(dict(line="$AUDIO_ANCHOR,"+",".join(k+"="+v for k,v in row.items())))
    serial = directory / "capture.jsonl"
    serial.write_text("\n".join(json.dumps(record) for record in records)+"\n", encoding="utf-8")
    acceptance = dict(status=mapping.EVIDENCE_STATUS, hardware_verified=True, boot=boot,
                      raw_files={str(serial.resolve()): hashlib.sha256(serial.read_bytes()).hexdigest()})
    evidence = directory / "acceptance.json"
    evidence.write_text(json.dumps(acceptance), encoding="utf-8")
    return serial, evidence, records, acceptance


class ReferenceTests(unittest.TestCase):
    def setUp(self):
        self.left = mapping.Anchor(True, 1, 0, 0, 0, 0)
        self.right = mapping.Anchor(True, 1, 48000, 48000, 1, 0)

    def test_fraction_closed_bin_bounds_and_midpoint(self):
        result = mapping.reference(self.left, self.right, 1, 24000)
        self.assertEqual(result, (0, 1, 499989583, 499979166, 500000000))

    def test_frame_translation_above_float_precision_changes_nothing(self):
        base = 2**64-100000
        self.assertEqual(mapping.reference(replace(self.left, frame_lo=base, frame_hi=base),
                                          replace(self.right, frame_lo=base+48000, frame_hi=base+48000),
                                          1, base+24000), mapping.reference(self.left,self.right,1,24000))

    def test_failure_statuses_are_cleared(self):
        cases = [(replace(self.left, valid=False), self.right, 1, 24000, 2),
                 (replace(self.left, session=0), self.right, 1, 24000, 3),
                 (self.left, self.right, 2, 24000, 3),
                 (self.right, self.left, 1, 24000, 4),
                 (self.left, replace(self.right, sec=0, nsec=99999999), 1, 24000, 5),
                 (self.left, replace(self.right, frame_lo=96000, frame_hi=96000), 1, 24000, 6),
                 (self.left, self.right, 1, 0, 7),
                 (replace(self.left, frame_hi=5), self.right, 1, 24000, 2),
                 (replace(self.left, nsec=10**9), self.right, 1, 24000, 2),
                 (self.left, replace(self.right, frame_lo=2**64-1, frame_hi=2**64-1), 1, 24000, 8)]
        for left,right,session,frame,status in cases:
            with self.subTest(status=status):
                self.assertEqual(mapping.reference(left,right,session,frame), (status,0,0,0,0))


class NativeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.build = mapping.build_native(Path(cls.temporary.name))
        cls.executable = Path(cls.build["executable_path"])

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def test_thousands_of_deterministic_cases_match_independent_fraction(self):
        cases = mapping.synthetic_cases(4000)
        actual = mapping.run_native(self.executable, cases)
        self.assertEqual(len(actual), len(cases))
        for case,result in zip(cases,actual):
            with self.subTest(case=case.identifier):
                self.assertEqual(result, (case.identifier, *mapping.reference(
                    case.left,case.right,case.target_session,case.frame_position)))
        self.assertTrue(any(case.left.frame_lo > 2**63 for case in cases))
        self.assertGreater(sum(result[1] == 0 for result in actual), 2500)

    def test_latent_phase_model_contains_true_crossing_and_band_edges(self):
        cases = []
        latent = []
        for width0 in range(5):
            for width1 in range(5):
                left = mapping.Anchor(True,1,2**63,2**63+width0,0,950000000)
                right = mapping.Anchor(True,1,2**63+48000,2**63+48000+width1,1,950000000)
                for frame in (left.frame_hi+1,2**63+24000,right.frame_lo):
                    case = mapping.Case(len(cases)+1,left,right,1,frame)
                    cases.append(case)
                    # Select an actual phase strictly inside each bin, independently
                    # of the interpolation extrema used by the implementation.
                    x = Fraction(left.frame_lo)+Fraction((width0+1)*3,7)
                    y = Fraction(right.frame_lo)+Fraction((width1+1)*5,9)
                    latent.append(Fraction(950000000)+10**9*(frame-x)/(y-x))
        actual = mapping.run_native(self.executable,cases)
        for case,result,true_ns in zip(cases,actual,latent):
            self.assertEqual(result,(case.identifier,*mapping.reference(
                case.left,case.right,case.target_session,case.frame_position)))
            self.assertEqual(result[1],0)
            self.assertLessEqual(result[4],true_ns)
            self.assertGreaterEqual(result[5],true_ns)
        left=mapping.Anchor(True,1,0,0,0,0)
        edge_cases=[mapping.Case(i+1,left,mapping.Anchor(True,1,span,span,1,0),1,24000)
                    for i,span in enumerate((47512,47513,48469,48470))]
        self.assertEqual([r[1] for r in mapping.run_native(self.executable,edge_cases)],[6,0,0,6])

    def test_cli_rejects_invalid_lexemes_widths_and_lines(self):
        normal = mapping.encode_case(mapping.synthetic_cases(1)[0])
        tokens = normal.split()
        bad = ["", "\n", normal+" 0\n", " ".join(tokens[:-1])+"\n", "x\n", "0 "*1000+"\n",
               normal.replace(" ", "\x00", 1)]
        for index,value in ((0,"-1"),(0,"+1"),(0,str(2**64)),(1,"2"),(2,str(2**32)),
                            (5,str(2**32)),(13,str(2**32)),(14,"1.0")):
            changed=tokens.copy(); changed[index]=value; bad.append(" ".join(changed)+"\n")
        for data in bad:
            with self.subTest(data=repr(data[:60])):
                completed = mapping.execute_native(self.executable, data)
                self.assertNotEqual(completed.returncode, 0)
                self.assertIn("CLI_BAD_INPUT", completed.stderr)

    def test_output_checks_order_ids_count_status_and_failure_zeroing(self):
        cases=mapping.synthetic_cases(2)
        for output in ("", "1 0 1 2 3 4\n", "9 0 1 2 3 4\n2 0 1 2 3 4\n",
                       "1 9 0 0 0 0\n2 0 1 2 3 4\n", "1 7 0 1 0 0\n2 0 1 2 3 4\n",
                       "1 0 1 3 4 5\n2 0 1 2 3 4\n"):
            with self.subTest(output=output), self.assertRaises(ValueError):
                mapping.decode_results(output,cases)

    def test_native_failure_and_stderr_are_not_ignored(self):
        for completed in (subprocess.CompletedProcess([],1,"","runtime failed"),
                          subprocess.CompletedProcess([],0,"","unexpected warning")):
            with patch.object(mapping,"execute_native",return_value=completed), self.assertRaises(ValueError):
                mapping.run_native(self.executable,mapping.synthetic_cases(1))

    def test_missing_compiler_and_failed_build_are_clear(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.dict(os.environ,{"CC":"missing-audio-map-compiler"}), self.assertRaisesRegex(ValueError,"compiler"):
                mapping.build_native(Path(directory))
            failed=subprocess.CompletedProcess([],1,"","compiler diagnostic")
            with patch.object(mapping.subprocess,"run",return_value=failed), self.assertRaisesRegex(ValueError,"compile"):
                mapping.build_native(Path(directory))


class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory()
        self.directory=Path(self.temporary.name)
        self.serial,self.evidence,self.records,self.acceptance=fixture(self.directory)

    def tearDown(self): self.temporary.cleanup()

    def test_binding_preserves_prefix_and_checks_all_anchors(self):
        original=self.serial.read_bytes()
        rows,metadata=mapping.load_bound_evidence(self.serial,self.evidence)
        self.assertEqual(len(rows),90)
        self.assertEqual(metadata["discarded_pre_boot_lines"],1)
        self.assertEqual(self.serial.read_bytes(),original)
        self.assertEqual(metadata["source_sha256"],hashlib.sha256(original).hexdigest())

    def test_unbound_failed_or_unverified_evidence_fails(self):
        variants=[dict(self.acceptance,status="FAIL"),dict(self.acceptance,hardware_verified=False),
                  dict(self.acceptance,raw_files={}),dict(self.acceptance,raw_files={str(self.serial.resolve()):"0"*64}),
                  dict(self.acceptance,boot=self.acceptance["boot"].replace("001A002D","00000000"))]
        for acceptance in variants:
            self.evidence.write_text(json.dumps(acceptance),encoding="utf-8")
            with self.subTest(acceptance=acceptance),self.assertRaises(ValueError):
                mapping.load_bound_evidence(self.serial,self.evidence)

    def test_missing_malformed_duplicate_boot_uid_and_short_evidence_fail(self):
        for records in (self.records[:40],self.records+[self.records[1]],
                        [dict(line="$BOOT,FW=old")]+self.records,
                        [self.records[0],dict(line=self.records[1]["line"].replace(",UID_W2=36363631",""))]+self.records[2:],
                        self.records+[dict(incomplete_tail_hex="01")]):
            self.serial.write_text("\n".join(json.dumps(row) for row in records),encoding="utf-8")
            self.acceptance["raw_files"][str(self.serial.resolve())]=hashlib.sha256(self.serial.read_bytes()).hexdigest()
            self.evidence.write_text(json.dumps(self.acceptance),encoding="utf-8")
            with self.subTest(records=len(records)),self.assertRaises(ValueError):
                mapping.load_bound_evidence(self.serial,self.evidence)
        self.serial.write_text("{broken",encoding="utf-8")
        with self.assertRaises(ValueError): mapping.load_bound_evidence(self.serial,self.evidence)
        with self.assertRaises((ValueError,OSError)): mapping.load_bound_evidence(self.directory/"missing",self.evidence)

    def test_duplicate_json_fields_cannot_silently_replace_evidence(self):
        raw=self.evidence.read_text(encoding="utf-8")
        self.evidence.write_text(raw[:-1]+',"hardware_verified":true}',encoding="utf-8")
        with self.assertRaisesRegex(ValueError,"duplicate"):
            mapping.load_bound_evidence(self.serial,self.evidence)

    def test_local_time_anomaly_fails_replay_without_refitting(self):
        # Anchor 2 is held out; 200us passes the original +/-1% rate sanity band
        # but must fail this replay's separately predeclared 100us residual gate.
        self.records[3]["line"]=self.records[3]["line"].replace("NSEC=0,","NSEC=200000,")
        self.serial.write_text("\n".join(json.dumps(r) for r in self.records)+"\n",encoding="utf-8")
        self.acceptance["raw_files"][str(self.serial.resolve())]=hashlib.sha256(self.serial.read_bytes()).hexdigest()
        self.evidence.write_text(json.dumps(self.acceptance),encoding="utf-8")
        result=mapping.replay(self.serial,self.evidence,self.directory/"anomaly",synthetic_count=4)
        self.assertEqual(result["status"],"FAIL_M323_EMPIRICAL_CHECK")
        self.assertGreater(result["holdout"]["max_abs_residual_ns"],100000)

    def test_holdout_uses_fixed_nonoverlap_triples_and_never_middle_time(self):
        rows,_=mapping.load_bound_evidence(self.serial,self.evidence)
        cases,held=mapping.holdout_cases(rows)
        self.assertEqual(len(cases),30)
        self.assertEqual([row["SEQ"] for row in held],[str(i) for i in range(2,91,3)])
        self.assertEqual(cases[0].frame_position,(int(rows[1]["F_LO"])+int(rows[1]["F_HI"])+1)//2)
        modified=copy.deepcopy(rows); modified[1]["SEC"]="999"
        self.assertEqual(mapping.holdout_cases(modified)[0],cases)

    def test_replay_produces_actual_coverage_and_refuses_overwrite(self):
        output=self.directory/"new-results"
        result=mapping.replay(self.serial,self.evidence,output,synthetic_count=40)
        self.assertFalse(result["hardware_verified"])
        self.assertEqual(result["holdout"]["count"],30)
        self.assertEqual(result["holdout"]["covered_count"]+result["holdout"]["outside_count"],30)
        self.assertLessEqual(result["holdout"]["max_abs_residual_ns"],100000)
        self.assertEqual(len(json.loads((output/"holdout.json").read_text())),30)
        self.assertIn("source_sha256",result["native_build"])
        before=(output/"summary.json").read_bytes()
        with self.assertRaises((ValueError,FileExistsError)): mapping.replay(self.serial,self.evidence,output)
        self.assertEqual((output/"summary.json").read_bytes(),before)

    def test_coverage_reports_real_outside_distance_and_threshold_failure(self):
        held=[dict(SEC="1",NSEC="20"),dict(SEC="1",NSEC="200001")]
        results=[(1,0,1,1000000010,1000000005,1000000015),
                 (2,0,1,1000000000,999999990,1000000010)]
        rows,summary=mapping.summarize_holdout(results,held)
        self.assertEqual(rows[0]["residual_ns"],10)
        self.assertEqual(rows[0]["outside_distance_ns"],5)
        self.assertEqual(summary["outside_count"],2)
        self.assertEqual(summary["max_outside_distance_ns"],199991)
        self.assertFalse(summary["empirical_check_pass"])


if __name__ == "__main__": unittest.main()
