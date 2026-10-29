"""Build and run the actual pure-C servo; output stays in ignored Logs/."""
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
    output = root / "Logs/TimeSync/M2_3_2" / (
        "test_clock_servo.exe" if os.name == "nt" else "test_clock_servo")
    output.parent.mkdir(parents=True, exist_ok=True)
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               "-I", str(root / "App"),
               str(root / "Tests/ClockServo/test_clock_servo.c"),
               str(root / "App/Sync/clock_servo.c"), "-o", str(output)]
    result = subprocess.run(command, cwd=root, timeout=30, check=False)
    if result.returncode:
        return result.returncode
    return subprocess.run([str(output)], cwd=root, timeout=30,
                          check=False).returncode


if __name__ == "__main__":
    sys.exit(main())
