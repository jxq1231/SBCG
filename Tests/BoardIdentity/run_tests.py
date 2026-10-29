"""Compile and execute the real identity resolver/BSP with a console-info stub."""
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
    output_dir = root / "Logs/TimeSync/M2_3_1"
    output_dir.mkdir(parents=True, exist_ok=True)
    for name, sources in [
        ("board_identity", ["App/BSP/board_identity.c"]),
        ("bsp_board_identity", ["App/BSP/board_identity.c",
                                "App/BSP/bsp_board_identity.c",
                                "Tests/BoardIdentity/stubs/console_info_stub.c"]),
    ]:
        executable = output_dir / ("test_" + name + (".exe" if os.name == "nt" else ""))
        command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                   "-I", str(root / "App"),
                   "-I", str(root / "Tests/BoardIdentity/stubs"),
                   str(root / "Tests/BoardIdentity" / ("test_" + name + ".c"))]
        command += [str(root / source) for source in sources]
        command += ["-o", str(executable)]
        result = subprocess.run(command, cwd=root, check=False, timeout=30)
        if result.returncode:
            return result.returncode
        result = subprocess.run([str(executable)], cwd=root, check=False, timeout=30)
        if result.returncode:
            return result.returncode
    return 0


if __name__ == "__main__":
    sys.exit(main())
