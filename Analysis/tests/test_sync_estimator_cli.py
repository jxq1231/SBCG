"""Exercise the native text adapter linked to the unchanged C estimator."""
from pathlib import Path
import json
import os
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
MAX_NS = 4294967295999999999
UINT64_MAX = 18446744073709551615
OK = "SYNC_ESTIMATOR_OK"
BAD_INPUT = {"index": None, "status": "CLI_BAD_INPUT"}


class SyncEstimatorCliTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("gcc")
        if compiler is None:
            raise RuntimeError("Native GCC is required")
        cls.temporary = tempfile.TemporaryDirectory(prefix="sync cli tests ")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.executable = Path(cls.temporary.name) / (
            "sync estimator.exe" if os.name == "nt" else "sync estimator")
        command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                   "-I", str(ROOT / "App"),
                   str(ROOT / "Analysis/sync_estimator_cli.c"),
                   str(ROOT / "App/Sync/sync_estimator.c"),
                   "-o", str(cls.executable)]
        build = subprocess.run(command, capture_output=True, text=True,
                               timeout=30, check=False)
        if build.returncode or build.stdout or build.stderr:
            raise RuntimeError(f"Native build failed or emitted diagnostics: {build}")

    def run_cli(self, payload, expected_returncode=0):
        if isinstance(payload, str):
            payload = payload.encode("ascii")
        result = subprocess.run([str(self.executable)], input=payload,
                                capture_output=True, timeout=5, check=False)
        self.assertEqual(result.returncode, expected_returncode, result)
        self.assertEqual(result.stderr, b"")
        return [json.loads(line) for line in result.stdout.splitlines()]

    @staticmethod
    def row(index, timestamps, window=800000000):
        return " ".join(str(value) for value in (index, *timestamps, window)) + "\n"

    def expect_ok(self, row, index, offset, delay):
        self.assertEqual(self.run_cli(row), [{
            "index": index, "status": OK, "offset_x2_ns": str(offset),
            "mean_path_delay_x2_ns": str(delay)}])

    def test_design_positive_offset(self):
        self.expect_ok(self.row(1, (1000000000, 1000150000, 1000350000,
                                    1000400000)), 1, 100000, 200000)

    def test_design_negative_zero_and_asymmetric_offsets(self):
        cases = [((1000000000, 1000050000, 1000250000, 1000400000), -100000),
                 ((1000000000, 1000100000, 1000300000, 1000400000), 0),
                 ((1000000000, 999600000, 999800000, 1000400000), -1000000),
                 ((1000000000, 1000200000, 1000400000, 1000400000), 200000)]
        for timestamps, offset in cases:
            with self.subTest(timestamps=timestamps):
                self.expect_ok(self.row(7, timestamps), 7, offset, 200000)

    def test_signed_half_nanosecond_numerators(self):
        for timestamps, offset in [((1000, 1001, 1002, 1002), 1),
                                   ((1000, 1000, 1001, 1002), -1)]:
            with self.subTest(offset=offset):
                self.expect_ok(self.row(2, timestamps), 2, offset, 1)

    def test_large_common_epoch_remains_exact(self):
        epoch = 4000000000000000000
        self.expect_ok(self.row(3, tuple(epoch + value for value in
                       (1000000000, 1000150000, 1000350000, 1000400000))),
                       3, 100000, 200000)

    def test_maximum_domain_preserves_int64_results(self):
        for timestamps, offset, delay in [
                ((MAX_NS, MAX_NS, MAX_NS, MAX_NS), 0, 0),
                ((0, MAX_NS, MAX_NS, 0), 8589934591999999998, 0),
                ((MAX_NS, 0, 0, MAX_NS), -8589934591999999998, 0),
                ((0, 0, 0, MAX_NS), -MAX_NS, MAX_NS)]:
            with self.subTest(timestamps=timestamps):
                self.expect_ok(self.row(4294967295, timestamps, MAX_NS),
                               4294967295, offset, delay)

    def test_out_of_range_values_reach_native_status(self):
        for window in (0, MAX_NS + 1, UINT64_MAX):
            with self.subTest(window=window):
                self.assertEqual(self.run_cli(self.row(1, (0, 0, 0, 0), window)),
                                 [{"index": 1, "status": "SYNC_ESTIMATOR_OUT_OF_RANGE"}])
        for position in range(4):
            for value in (MAX_NS + 1, UINT64_MAX):
                timestamps = [0, 0, 0, 0]
                timestamps[position] = value
                with self.subTest(position=position, value=value):
                    self.assertEqual(self.run_cli(self.row(1, timestamps)),
                                     [{"index": 1, "status": "SYNC_ESTIMATOR_OUT_OF_RANGE"}])

    def test_native_rejections_do_not_reuse_success_results(self):
        cases = [((10, 20, 30, 9), 1, "BAD_ORDER"),
                 ((10, 20, 19, 30), 1, "BAD_ORDER"),
                 ((0, 0, 0, 11), 10, "WINDOW_EXCEEDED"),
                 ((0, 0, 11, 0), 10, "WINDOW_EXCEEDED"),
                 ((0, 0, 100, 99), 100, "NEGATIVE_DELAY")]
        payload = self.row(9, (1000, 1001, 1002, 1002))
        for index, (timestamps, window, _) in enumerate(cases, 10):
            payload += self.row(index, timestamps, window)
        rows = self.run_cli(payload)
        self.assertEqual(len(rows), 6)
        self.assertEqual(rows[0]["status"], OK)
        for index, (_, _, status) in enumerate(cases, 10):
            self.assertEqual(rows[index - 9],
                             {"index": index, "status": "SYNC_ESTIMATOR_" + status})

    def test_exact_window_boundary_is_accepted(self):
        self.expect_ok(self.row(1, (0, 0, 10, 10), 10), 1, 0, 0)

    def test_batch_echoes_nonsequential_indexes_in_order(self):
        payload = "".join(self.row(index, (0, 0, 0, 0), 1)
                          for index in (4294967295, 4, 1, 999))
        rows = self.run_cli(payload)
        self.assertEqual([row["index"] for row in rows], [4294967295, 4, 1, 999])
        self.assertTrue(all(row["status"] == OK for row in rows))

    def test_crlf_and_final_short_line_without_newline(self):
        self.expect_ok("1 1000 1001 1002 1002 10\r\n", 1, 1, 1)
        self.expect_ok("2 1000 1001 1002 1002 10", 2, 1, 1)

    def test_ascii_horizontal_whitespace_and_leading_zeroes(self):
        self.expect_ok("\t 0001\t0 0\t0 0 01 \t\n", 1, 0, 0)

    def test_empty_input_and_blank_lines_are_invalid(self):
        for payload in (b"", b"\n", b"\r\n", b" \t\n"):
            with self.subTest(payload=payload):
                self.assertEqual(self.run_cli(payload, 2), [BAD_INPUT])

    def test_field_count_and_malformed_decimal_are_invalid(self):
        for payload in ("1 0 0 0 0\n", "1 0 0 0 0 1 7\n", "1 0 0 0 0 1x\n",
                        "1 0 0 0 0 1.0\n", "1 0 0 0 0 1e0\n",
                        "1 0 0 0 0 0x1\n", "1 0 0\r0 0 1\n",
                        b"1 0 0 0 0 1\x00garbage\n",
                        b"1 0 0 0 0 1\xff\n"):
            with self.subTest(payload=payload):
                self.assertEqual(self.run_cli(payload, 2), [BAD_INPUT])

    def test_signs_and_numeric_overflow_are_invalid(self):
        for position in range(6):
            for invalid in ("-1", "+1", str(UINT64_MAX + 1)):
                fields = ["1", "0", "0", "0", "0", "1"]
                fields[position] = invalid
                with self.subTest(position=position, invalid=invalid):
                    self.assertEqual(self.run_cli(" ".join(fields) + "\n", 2),
                                     [BAD_INPUT])
        for index in (0, 4294967296, UINT64_MAX):
            with self.subTest(index=index):
                self.assertEqual(self.run_cli(self.row(index, (0, 0, 0, 0)), 2),
                                 [BAD_INPUT])

    def test_line_length_limit_counts_newline_and_crlf(self):
        base = "1 0 0 0 0 1"
        for ending in ("\n", "\r\n", ""):
            valid = base + " " * (159 - len(base) - len(ending)) + ending
            with self.subTest(ending=ending, length=159):
                self.expect_ok(valid, 1, 0, 0)
            invalid = base + " " * (160 - len(base) - len(ending)) + ending
            with self.subTest(ending=ending, length=160):
                self.assertEqual(self.run_cli(invalid, 2), [BAD_INPUT])

    def test_overlong_input_exits_without_processing_more_records(self):
        self.assertEqual(self.run_cli("1" * 100000 + "\n" +
                                     self.row(3, (0, 0, 0, 0)), 2), [BAD_INPUT])

    def test_invalid_text_terminates_batch_immediately(self):
        payload = self.row(8, (0, 0, 0, 0)) + "bad\n" + self.row(9, (0, 0, 0, 0))
        rows = self.run_cli(payload, 2)
        self.assertEqual(len(rows), 2)
        self.assertEqual(rows[0]["index"], 8)
        self.assertEqual(rows[0]["status"], OK)
        self.assertEqual(rows[1], BAD_INPUT)


if __name__ == "__main__":
    unittest.main()
