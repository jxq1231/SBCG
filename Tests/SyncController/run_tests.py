"""Compile the controller against actual interfaces and isolated dependencies."""
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    root = Path(__file__).resolve().parents[2]
    gcc = shutil.which("gcc")
    if not gcc:
        print("Native GCC is required", file=sys.stderr)
        return 1
    output = root / "Logs/TimeSync/M2_3_2/test_sync_controller.exe"
    output.parent.mkdir(parents=True, exist_ok=True)
    command = [gcc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(root / "App"),
               str(root / "Tests/SyncController/test_time_sync.c"),
               str(root / "App/Sync/time_sync.c"), "-o", str(output)]
    subprocess.run(command, cwd=root, check=True, timeout=30)
    subprocess.run([str(output)], cwd=root, check=True, timeout=30)
    return 0


if __name__ == "__main__":
    sys.exit(main())
