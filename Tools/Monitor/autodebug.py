"""SBCG host diagnostics. A successful host action never implies a hardware pass."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import time
import uuid


PROJECT_ROOT = Path(__file__).resolve().parents[2]
BUILD_TOOLS = ("cmake", "ninja", "arm-none-eabi-gcc")
OPTIONAL_TOOLS = ("arm-none-eabi-gdb", "openocd", "arm-none-eabi-addr2line", "clangd")
GCC_DIAGNOSTIC = re.compile(
    r"^(?P<file>.+?):(?P<line>\d+)(?::(?P<column>\d+))?:\s*"
    r"(?P<severity>fatal error|error|warning|note):\s*(?P<message>.*)$"
)


def parse_diagnostics(output):
    diagnostics = []
    for line in output.splitlines():
        match = GCC_DIAGNOSTIC.match(line)
        if match:
            item = match.groupdict()
            item["line"] = int(item["line"])
            item["column"] = int(item["column"]) if item["column"] else None
            diagnostics.append(item)
        elif re.search(r"CMake Error|undefined reference|multiple definition|will not fit|overflowed by|cannot find -l", line):
            diagnostics.append({"severity": "error", "message": line})
    return diagnostics


def stop_process_tree(process):
    """Only terminate the process group/tree started for this stage."""
    if os.name == "nt":
        try:
            subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=10)
        except (OSError, subprocess.TimeoutExpired):
            if process.poll() is None:
                process.kill()
    else:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
    if process.poll() is None:
        process.kill()
    process.wait(timeout=5)


def run_stage(name, command, root, report_dir, timeout_s):
    log_path = report_dir / (name + ".log")
    started = time.monotonic()
    options = ({"creationflags": subprocess.CREATE_NEW_PROCESS_GROUP} if os.name == "nt"
               else {"start_new_session": True})
    timed_out = False
    with log_path.open("w", encoding="utf-8") as log:
        process = subprocess.Popen(command, cwd=root, stdin=subprocess.DEVNULL,
                                   stdout=log, stderr=subprocess.STDOUT, **options)
        try:
            process.wait(timeout=timeout_s)
        except subprocess.TimeoutExpired:
            timed_out = True
            stop_process_tree(process)
        except KeyboardInterrupt:
            stop_process_tree(process)
            raise
    return {
        "name": name, "command": command, "returncode": process.returncode,
        "timed_out": timed_out, "duration_s": round(time.monotonic() - started, 3),
        "log": str(log_path), "output": log_path.read_text(encoding="utf-8", errors="replace"),
    }


def artifact_info(elf):
    return {"path": str(elf), "size_bytes": elf.stat().st_size,
            "sha256": hashlib.sha256(elf.read_bytes()).hexdigest()}


def finish_stage(report, stage):
    report["stages"].append(stage)
    report["diagnostics"].extend(parse_diagnostics(stage["output"]))
    if stage["timed_out"]:
        report["status"] = "TIMEOUT"
        return 4
    if stage["returncode"] != 0:
        report["status"] = stage["name"].upper() + "_FAILED"
        return 1
    return 0


def build(report, args, report_dir):
    cmake = report["tools"]["cmake"]
    for name, command in (
        ("configure", [cmake, "--preset", args.preset]),
        ("build", [cmake, "--build", "--preset", args.preset, "--parallel"]),
    ):
        if not args.json:
            print("[" + name + "] " + subprocess.list2cmdline(command), flush=True)
        stage = run_stage(name, command, args.project_root, report_dir, args.timeout)
        if not args.json:
            print(stage["output"], end="" if stage["output"].endswith("\n") else "\n")
        code = finish_stage(report, stage)
        if code:
            return code
    elf = args.project_root / "build" / args.preset / "SBCG.elf"
    if not elf.is_file() or elf.stat().st_size == 0:
        report["status"] = "ARTIFACT_MISSING"
        report["message"] = "CMake finished without the expected nonempty SBCG.elf."
        return 1
    report["artifact"] = artifact_info(elf)
    report["status"] = "BUILD_OK"
    return 0


def symbolize(report, args, report_dir):
    elf = args.project_root / "build" / args.preset / "SBCG.elf"
    if not elf.is_file() or elf.stat().st_size == 0:
        report["status"] = "ARTIFACT_MISSING"
        report["message"] = "Build the matching ELF before resolving an address."
        return 7
    # Cortex-M function pointers can carry the Thumb bit; ELF locations do not.
    addresses = [hex(address & ~1) for address in args.addresses]
    command = [report["tools"]["arm-none-eabi-addr2line"], "-e", str(elf), "-f", "-C", *addresses]
    stage = run_stage("symbolize", command, args.project_root, report_dir, args.timeout)
    code = finish_stage(report, stage)
    if code:
        return code
    report["artifact"] = artifact_info(elf)
    lines = stage["output"].splitlines()
    results = []
    for index, address in enumerate(addresses):
        function = lines[index * 2] if index * 2 < len(lines) else "??"
        source = lines[index * 2 + 1] if index * 2 + 1 < len(lines) else "??:0"
        line = re.search(r":([1-9]\d*)(?:\s|$)", source)
        results.append({"input_address": hex(args.addresses[index]), "address": address,
                        "function": function, "source": source,
                        "resolved": function != "??" and not source.startswith("??") and bool(line)})
    report["symbols"] = results
    report["status"] = "SYMBOLIZED" if all(item["resolved"] for item in results) else "ADDRESS_UNRESOLVED"
    return 0 if report["status"] == "SYMBOLIZED" else 7


def positive_seconds(value):
    try:
        result = float(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("timeout must be a positive number") from error
    if not math.isfinite(result) or result <= 0:
        raise argparse.ArgumentTypeError("timeout must be finite and positive")
    return result


def address_value(value):
    try:
        result = int(value, 0)
    except ValueError as error:
        raise argparse.ArgumentTypeError("use an address such as 0x08000100") from error
    if not 0 <= result <= 0xFFFFFFFF:
        raise argparse.ArgumentTypeError("address must fit in an unsigned 32-bit value")
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="action", required=True)
    for action in ("doctor", "build", "symbolize"):
        command = subparsers.add_parser(action)
        command.add_argument("--project-root", type=Path, default=PROJECT_ROOT)
        command.add_argument("--preset", choices=("Debug", "Release"), default="Debug")
        command.add_argument("--timeout", type=positive_seconds, default=120,
                             help="timeout in seconds for each subprocess stage")
        command.add_argument("--json", action="store_true", help="print one JSON result instead of build output")
        if action == "symbolize":
            command.add_argument("addresses", nargs="+", type=address_value)
    args = parser.parse_args(argv)
    args.project_root = args.project_root.resolve()
    report = {"schema_version": 1, "action": args.action, "preset": args.preset,
              "project_root": str(args.project_root), "started_utc": datetime.now(timezone.utc).isoformat(),
              "status": "CONFIG_ERROR", "tools": {}, "stages": [], "diagnostics": [],
              "flash": "NOT_RUN", "runtime": "NOT_RUN", "hardware_verified": False}
    report_dir = None
    code = 6
    try:
        for name in ("CMakeLists.txt", "CMakePresets.json", "SBCG.ioc"):
            if not (args.project_root / name).is_file():
                raise ValueError("Not an SBCG project root; missing " + name)
        run_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S") + "-" + uuid.uuid4().hex[:8]
        report_dir = args.project_root / "Logs" / "AutoDebug" / run_id
        report_dir.mkdir(parents=True)
        report["report_path"] = str(report_dir / "report.json")
        report["tools"] = {name: shutil.which(name) for name in BUILD_TOOLS + OPTIONAL_TOOLS}
        required = ("arm-none-eabi-addr2line",) if args.action == "symbolize" else BUILD_TOOLS
        missing = [name for name in required if not report["tools"][name]]
        if missing:
            report["status"] = "TOOL_MISSING"
            report["message"] = "Missing required tools on PATH: " + ", ".join(missing)
        elif args.action == "doctor":
            report["status"] = "ENVIRONMENT_OK"
            report["optional_tools_missing"] = [name for name in OPTIONAL_TOOLS if not report["tools"][name]]
            code = 0
        elif args.action == "build":
            code = build(report, args, report_dir)
        else:
            code = symbolize(report, args, report_dir)
    except KeyboardInterrupt:
        report["status"] = "CANCELLED"
        code = 130
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        report["status"] = "CONFIG_ERROR"
        report["message"] = str(error)
        code = 6
    report["exit_code"] = code
    if report_dir is not None:
        try:
            (report_dir / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n",
                                                   encoding="utf-8")
        except OSError as error:
            report["status"], report["exit_code"], code = "REPORT_WRITE_FAILED", 6, 6
            report["message"] = str(error)
    if args.json:
        print(json.dumps(report, ensure_ascii=False, indent=2))
    else:
        print("[" + report["status"] + "] " + report.get("message", "Host action only; hardware verification NOT_RUN."))
        if args.action == "doctor":
            for tool, path in report["tools"].items():
                print("  " + tool + ": " + (path or "NOT FOUND"))
        for item in report.get("symbols", []):
            print("  " + item["address"] + " " + item["function"] + " " + item["source"])
        if "report_path" in report:
            print("Report: " + report["report_path"])
    return code


if __name__ == "__main__":
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    sys.exit(main())
