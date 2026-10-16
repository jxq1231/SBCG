"""Host workflow contracts; no test connects to a debug probe."""

import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


SCRIPT = Path(__file__).resolve().parents[1] / "autodebug.py"
spec = importlib.util.spec_from_file_location("sbcg_autodebug", SCRIPT)
workflow = importlib.util.module_from_spec(spec)
spec.loader.exec_module(workflow)


class WorkflowTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="sbcg workflow ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in ("CMakeLists.txt", "CMakePresets.json", "SBCG.ioc"):
            (self.root / name).write_text("fixture", encoding="utf-8")

    def invoke(self, command, runner):
        with patch.object(workflow.shutil, "which", side_effect=lambda name: name), \
                patch.object(workflow, "run_stage", side_effect=runner):
            code = workflow.main([command, "--project-root", str(self.root)])
        reports = list((self.root / "Logs" / "AutoDebug").glob("*/report.json"))
        self.assertEqual(len(reports), 1)
        return code, json.loads(reports[0].read_text(encoding="utf-8"))

    def test_windows_gcc_location_and_link_error_are_preserved(self):
        diagnostics = workflow.parse_diagnostics(
            "D:/project with spaces/Core/Src/main.c:12:7: error: bad call\n"
            "D:\\project\\app.c:22: warning: unused variable\n"
            "ld.exe: section .bss will not fit in region RAM\n"
        )
        self.assertEqual(diagnostics[0]["file"], "D:/project with spaces/Core/Src/main.c")
        self.assertEqual(diagnostics[0]["line"], 12)
        self.assertEqual(diagnostics[0]["column"], 7)
        self.assertEqual(diagnostics[1]["severity"], "warning")
        self.assertIn("will not fit", diagnostics[2]["message"])

    def test_configure_failure_stops_before_build(self):
        calls = []

        def runner(name, command, root, report_dir, timeout_s):
            calls.append(name)
            return {"name": name, "returncode": 1, "output": "CMake Error: failed", "timed_out": False}

        code, report = self.invoke("build", runner)
        self.assertEqual(calls, ["configure"])
        self.assertEqual(code, 1)
        self.assertEqual(report["status"], "CONFIGURE_FAILED")
        self.assertFalse(report["hardware_verified"])
        self.assertEqual(report["runtime"], "NOT_RUN")

    def test_zero_exit_without_elf_is_failure(self):
        runner = lambda name, *args: {"name": name, "returncode": 0, "output": "", "timed_out": False}
        code, report = self.invoke("build", runner)
        self.assertEqual(code, 1)
        self.assertEqual(report["status"], "ARTIFACT_MISSING")

    def test_build_failure_never_accepts_previous_elf(self):
        elf = self.root / "build/Debug/SBCG.elf"
        elf.parent.mkdir(parents=True)
        elf.write_bytes(b"old ELF")

        def runner(name, *args):
            return {"name": name, "returncode": int(name == "build"), "output": "", "timed_out": False}

        code, report = self.invoke("build", runner)
        self.assertEqual(code, 1)
        self.assertEqual(report["status"], "BUILD_FAILED")
        self.assertNotIn("artifact", report)

    def test_build_success_has_hash_and_never_claims_hardware_pass(self):
        def runner(name, *args):
            if name == "build":
                elf = self.root / "build/Debug/SBCG.elf"
                elf.parent.mkdir(parents=True)
                elf.write_bytes(b"test ELF")
            return {"name": name, "returncode": 0, "output": "", "timed_out": False}

        code, report = self.invoke("build", runner)
        self.assertEqual(code, 0)
        self.assertEqual(report["status"], "BUILD_OK")
        self.assertEqual(len(report["artifact"]["sha256"]), 64)
        self.assertEqual(report["flash"], "NOT_RUN")
        self.assertEqual(report["runtime"], "NOT_RUN")
        self.assertFalse(report["hardware_verified"])
        self.assertEqual([s["name"] for s in report["stages"]], ["configure", "build"])

    def test_missing_build_tool_is_reported_before_any_command(self):
        with patch.object(workflow.shutil, "which", return_value=None), \
                patch.object(workflow, "run_stage") as runner:
            code = workflow.main(["build", "--project-root", str(self.root)])
        self.assertEqual(code, 6)
        runner.assert_not_called()
        report_path = next((self.root / "Logs/AutoDebug").glob("*/report.json"))
        report = json.loads(report_path.read_text(encoding="utf-8"))
        self.assertEqual(report["status"], "TOOL_MISSING")

    def test_stage_timeout_stops_workflow(self):
        def runner(name, *args):
            return {"name": name, "returncode": None, "output": "partial", "timed_out": True}

        code, report = self.invoke("build", runner)
        self.assertEqual(code, 4)
        self.assertEqual(report["status"], "TIMEOUT")
        self.assertEqual(len(report["stages"]), 1)

    def test_symbolize_unknown_address_does_not_report_success(self):
        elf = self.root / "build/Debug/SBCG.elf"
        elf.parent.mkdir(parents=True)
        elf.write_bytes(b"test ELF")
        with patch.object(workflow.shutil, "which", return_value="addr2line"), \
                patch.object(workflow, "run_stage", return_value={
                    "name": "symbolize", "returncode": 0, "output": "??\n??:0\n", "timed_out": False,
                }):
            code = workflow.main(["symbolize", "0x08000000", "--project-root", str(self.root)])
        self.assertEqual(code, 7)

    def test_real_subprocess_handles_spaces_and_nonzero_exit(self):
        stage = workflow.run_stage("fixture", [sys.executable, "-c", "print('hello'); raise SystemExit(3)"],
                                   self.root, self.root, 10)
        self.assertEqual(stage["returncode"], 3)
        self.assertIn("hello", stage["output"])
        self.assertFalse(stage["timed_out"])

    def test_real_subprocess_timeout_is_bounded(self):
        stage = workflow.run_stage("timeout", [sys.executable, "-c", "import time; time.sleep(30)"],
                                   self.root, self.root, 0.1)
        self.assertTrue(stage["timed_out"])


if __name__ == "__main__":
    unittest.main()
