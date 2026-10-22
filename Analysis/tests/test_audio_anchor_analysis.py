import copy
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from audio_anchor_analysis import check_anchor_rows, parse_anchor_records


class AnchorAnalysisTests(unittest.TestCase):
    def setUp(self):
        self.rows = []
        for i in range(1, 61):
            frames = i*47991071//1000
            self.rows.append(dict(SEQ=str(i), STATUS="OK", VALID="1", SESSION="1",
                                  F_LO=str(frames), F_HI=str(frames+i%3), SEC=str(i),
                                  NSEC="0", WIDTH=str(i%3), SYNC="0", OK=str(i), REJECTS="0"))

    def reject(self, rows, index, status):
        rows[index] = dict(SEQ=rows[index]["SEQ"], STATUS=status, VALID="0", PTP_ERR="0",
                           SYNC="0", OK="0", REJECTS="0")
        good = bad = 0
        for row in rows:
            if row["STATUS"] == "OK": good += 1
            else: bad += 1
            row.update(OK=str(good), REJECTS=str(bad))

    def test_normal_window_rate_interval_includes_quantization(self):
        result = check_anchor_rows(self.rows)
        self.assertEqual(result["valid"], 60)
        self.assertEqual(result["max_width_frames"], 2)
        self.assertLess(float(result["rate_hz_lower"]), 47991.071)
        self.assertGreater(float(result["rate_hz_upper"]), 47991.071)
        self.assertFalse(result["hardware_verified"])

    def test_zero_width_does_not_produce_zero_rate_uncertainty(self):
        rows = [dict(r, F_HI=r["F_LO"], WIDTH="0") for r in self.rows]
        result = check_anchor_rows(rows)
        self.assertLess(float(result["rate_hz_lower"]), float(result["rate_hz_upper"]))

    def test_local_clock_anomaly_not_hidden_by_endpoint_average(self):
        rows = copy.deepcopy(self.rows); rows[30]["NSEC"] = "100000000"
        with self.assertRaisesRegex(ValueError, "local rate"):
            check_anchor_rows(rows)

    def test_transient_rejections_are_counted_not_hidden(self):
        rows = copy.deepcopy(self.rows); self.reject(rows, 10, "BOUNDARY")
        result = check_anchor_rows(rows, minimum_valid=50)
        self.assertEqual((result["valid"], result["rejected"]), (59, 1))
        for i in range(7): self.reject(rows, i, "CURSOR")
        with self.assertRaises(ValueError): check_anchor_rows(rows, minimum_valid=50)

    def test_clock_identity_and_order_failures_block_normal_acceptance(self):
        for status in ("PTP", "BACKWARD", "SESSION", "ORDER"):
            rows = copy.deepcopy(self.rows); self.reject(rows, 10, status)
            with self.subTest(status=status), self.assertRaises(ValueError):
                check_anchor_rows(rows, minimum_valid=50)

    def test_bad_bounds_timestamps_counters_and_old_values_fail(self):
        for field, value in (("WIDTH", "5"), ("F_LO", "1"), ("F_HI", str(2**64)),
                             ("SESSION", "2"), ("SESSION", "0"), ("SEC", "0"),
                             ("NSEC", "1000000000"), ("SEQ", "2"), ("SYNC", "1"),
                             ("OK", "0"), ("REJECTS", "1"), ("VALID", "0")):
            rows = copy.deepcopy(self.rows); rows[20][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError): check_anchor_rows(rows)
        rows = copy.deepcopy(self.rows); self.reject(rows, 20, "WIDE"); rows[20]["F_LO"]="100"
        with self.assertRaises(ValueError): check_anchor_rows(rows, minimum_valid=50)

    def test_missing_short_reordered_and_unreal_rate_fail(self):
        for rows in (self.rows[:-1], self.rows[:5]+self.rows[6:], list(reversed(self.rows))):
            with self.assertRaises(ValueError): check_anchor_rows(rows)
        rows = [dict(r, F_LO=str(int(r["F_LO"])*2), F_HI=str(int(r["F_LO"])*2+int(r["WIDTH"]))) for r in self.rows]
        with self.assertRaises(ValueError): check_anchor_rows(rows)

    def test_boot_parser_requires_identity_and_unique_fields(self):
        boot=dict(line="$BOOT,FW=SBCG-M3.2-ANCHOR,UID_W0=001A002D")
        anchor=dict(line="$AUDIO_ANCHOR,"+",".join(k+"="+v for k,v in self.rows[0].items()))
        self.assertEqual(len(parse_anchor_records([boot, anchor])), 1)
        for records in ([anchor], [anchor, boot], [boot, anchor, boot],
                        [dict(line="$BOOT,FW=old"), anchor],
                        [boot, dict(line="$AUDIO_ANCHOR,SEQ=1,SEQ=2")],
                        [boot, anchor, dict(incomplete_tail_hex="01")]):
            with self.assertRaises(ValueError): parse_anchor_records(records)


if __name__ == "__main__": unittest.main()
