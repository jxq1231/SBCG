import copy
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from audio_position_analysis import check_positions, parse_records


class PositionTests(unittest.TestCase):
    def setUp(self):
        self.audio, self.positions = [], []
        for i in range(1, 61):
            blocks = i * 6
            self.audio.append(dict(SEQ=str(i), FRAMES=str(blocks * 8192), H=str(blocks//2),
                                   F=str(blocks//2), OK=str(blocks), DROP="0", ERR="0",
                                   HAL="00000000", DMA="00000000", VALID="1", STATE="RUN"))
            self.positions.append(dict(SEQ=str(i), SESSION="1", OBS_FRAMES=str(blocks*8192),
                                       FIRST=str((blocks-1)*8192), COUNT="8192", GAP="0",
                                       INDEX_VALID="1", CONTIG="1", FAULTS="00000000", VALID="1"))

    def test_normal_window(self):
        self.assertEqual(check_positions(self.audio, self.positions)["pairs"], 60)

    def test_callback_between_end_and_snapshot_is_allowed(self):
        for a, p in zip(self.audio, self.positions):
            blocks = int(p["OBS_FRAMES"])//8192+1
            a.update(FRAMES=str(blocks*8192), H=str((blocks+1)//2), F=str(blocks//2))
            p["OBS_FRAMES"] = a["FRAMES"]
        self.assertEqual(check_positions(self.audio, self.positions)["pairs"], 60)

    def test_faults_gaps_identity_bad_bounds_and_stale_positions_fail(self):
        for field, value in (("SESSION", "2"), ("SESSION", "0"), ("SESSION", str(2**32)),
                             ("INDEX_VALID", "0"), ("VALID", "0"), ("CONTIG", "0"),
                             ("GAP", "8192"), ("FAULTS", "00000002"), ("COUNT", "16384"),
                             ("FIRST", "1"), ("FIRST", "0"), ("OBS_FRAMES", str(2**64)),
                             ("SEQ", "3"), ("OBS_FRAMES", "-1")):
            rows = copy.deepcopy(self.positions); rows[20][field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                check_positions(self.audio, rows)

    def test_missing_duplicate_reordered_or_short_records_fail(self):
        for rows in (self.positions[:-1], self.positions+[self.positions[-1]],
                     list(reversed(self.positions)), self.positions[:20]):
            with self.assertRaises(ValueError): check_positions(self.audio, rows)

    def test_audio_errors_and_cross_snapshot_mismatch_fail(self):
        for field, value in (("DROP", "1"), ("ERR", "1"), ("DMA", "00000008"),
                             ("H", "0"), ("FRAMES", "8192"), ("OK", "1"),
                             ("VALID", "0"), ("STATE", "STALLED")):
            rows = copy.deepcopy(self.audio); rows[20][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                check_positions(rows, self.positions)

    def test_parser_requires_complete_unique_fields_and_boot_identity(self):
        lines = [dict(line="$BOOT,FW=SBCG-M3.2-INDEX"),
                 dict(line="$AUDIO,SEQ=1,FRAMES=8192"),
                 dict(line="$AUDIO_POS,SEQ=1,SESSION=1")]
        audio, positions = parse_records(lines)
        self.assertEqual((len(audio), len(positions)), (1, 1))
        for bad in (lines + [lines[0]], lines[1:],
                    [dict(line="$BOOT,FW=old"), *lines[1:]],
                    [*lines, dict(incomplete_tail_hex="24")],
                    [lines[0], dict(line="$AUDIO_POS,SEQ=1,SEQ=2")],
                    [lines[0], dict(line="$AUDIO_POS,SEQ")],
                    [lines[0], lines[2], lines[1]],
                    [lines[0], lines[1], lines[1], lines[2]],
                    [lines[0], lines[1]],
                    [lines[0], lines[1], dict(line="$AUDIO_POS,SEQ=2,SESSION=1")]):
            with self.assertRaises(ValueError): parse_records(bad)


if __name__ == "__main__":
    unittest.main()
