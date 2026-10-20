"""Native tests of the real, hardware-independent four-timestamp estimator."""
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
    output = root / "Logs/M2_2_Sync_Estimator" / (
        "test_sync_estimator.exe" if os.name == "nt" else "test_sync_estimator")
    output.parent.mkdir(parents=True, exist_ok=True)
    command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
               "-I", str(root / "App"),
               str(root / "Tests/Sync/test_sync_estimator.c"),
               str(root / "App/Sync/sync_estimator.c"), "-o", str(output)]
    result = subprocess.run(command, cwd=root, timeout=30, check=False)
    if result.returncode:
        return result.returncode
    return subprocess.run([str(output)], cwd=root, timeout=30, check=False).returncode


if __name__ == "__main__":
    sys.exit(main())
