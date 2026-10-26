"""Run application behavior tests on the host; never build or flash firmware."""

from pathlib import Path
import shutil
import subprocess
import sys


def main():
    root = Path(__file__).resolve().parents[2]
    compiler = shutil.which("gcc")
    if compiler is None:
        print("Host GCC is required for application behavior tests.", file=sys.stderr)
        return 1
    output_dir = root / "Logs" / "M0_2"
    output_dir.mkdir(parents=True, exist_ok=True)
    cases = [("test_app_main", []),
             ("test_app_identity", ["-DSBCG_USE_BOARD_IDENTITY=1"]),
             ("test_app_sync", ["-DSBCG_USE_BOARD_IDENTITY=1", "-DSBCG_USE_BOARD_SYNC=1"])]
    cases.append(("test_app_sync", ["-DSBCG_USE_BOARD_IDENTITY=1", "-DSBCG_USE_BOARD_SYNC=1", "-DSBCG_USE_PPS=1"]))
    for name, flags in cases:
        output = output_dir / (name + ".exe")
        sources = [root / "Tests/App" / (name + ".c"), root / "App/app_main.c"]
        if name in ("test_app_identity", "test_app_sync"):
            sources.append(root / "App/BSP/board_identity.c")
        command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic",
                   "-DSBCG_USE_LWIP=1", "-DSBCG_USE_AUDIO=1", "-DSBCG_USE_AUDIO_ANCHOR=1", "-DSBCG_USE_AUDIO_MAP=1",
                   *flags, "-I", str(root / "Tests/App/stubs"), "-I", str(root / "App"),
                   *map(str, sources), "-o", str(output)]
        result = subprocess.run(command, cwd=root, check=False)
        if result.returncode != 0:
            return result.returncode
        result = subprocess.run([str(output)], cwd=root, check=False)
        if result.returncode != 0:
            return result.returncode
    return 0


if __name__ == "__main__":
    sys.exit(main())
