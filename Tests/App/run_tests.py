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
    output = root / "Logs" / "M0_2" / "test_app_main.exe"
    output.parent.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(
        [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic",
         "-DSBCG_USE_LWIP=1", "-DSBCG_USE_AUDIO=1", "-DSBCG_USE_AUDIO_ANCHOR=1", "-DSBCG_USE_AUDIO_MAP=1", "-I", str(root / "Tests/App/stubs"),
         "-I", str(root / "App"), str(root / "Tests/App/test_app_main.c"),
         str(root / "App/app_main.c"), "-o", str(output)], cwd=root,
        check=False,
    )
    if result.returncode != 0:
        return result.returncode
    return subprocess.run([str(output)], cwd=root, check=False).returncode


if __name__ == "__main__":
    sys.exit(main())
