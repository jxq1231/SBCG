"""Compile the PPS qualification service against isolated BSP dependencies."""
from pathlib import Path
import os
import shutil
import subprocess
import sys


def main():
    root = Path(__file__).resolve().parents[2]
    compiler = shutil.which("gcc")
    if not compiler:
        print("Native GCC is required", file=sys.stderr)
        return 1
    output = root / "Logs/PPS/M2_3_3" / (
        "test_pps_service.exe" if os.name == "nt" else "test_pps_service")
    output.parent.mkdir(parents=True, exist_ok=True)
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               "-I", str(root / "App"),
               str(root / "Tests/PpsService/test_pps_service.c"),
               str(root / "App/Sync/pps_service.c"), "-o", str(output)]
    result = subprocess.run(command, cwd=root, timeout=30, check=False)
    if result.returncode:
        return result.returncode
    return subprocess.run([str(output)], cwd=root, timeout=30,
                          check=False).returncode


if __name__ == "__main__":
    sys.exit(main())
