"""Native tests of the unchanged estimator and deferred observation queue."""
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
    result = subprocess.run([str(output)], cwd=root, timeout=30, check=False)
    if result.returncode:
        return result.returncode
    observation_output = root / "Logs/M2_2_Board_Observation" / (
        "test_sync_observation.exe" if os.name == "nt" else "test_sync_observation")
    observation_output.parent.mkdir(parents=True, exist_ok=True)
    # The observation test includes its real .c to reach the private formatter;
    # the estimator is linked normally and its original test path is retained.
    command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
               "-I", str(root / "App"),
               str(root / "Tests/Sync/test_sync_observation.c"),
               str(root / "App/Sync/sync_estimator.c"),
               "-o", str(observation_output)]
    result = subprocess.run(command, cwd=root, timeout=30, check=False)
    if result.returncode:
        return result.returncode
    return subprocess.run([str(observation_output)], cwd=root, timeout=30,
                          check=False).returncode


if __name__ == "__main__":
    sys.exit(main())
