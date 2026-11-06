"""Native command and UART receive tests; no hardware access."""
from pathlib import Path
import os
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
compiler = os.environ.get("CC") or shutil.which("gcc")
if not compiler:
    raise SystemExit("Set CC to a native GCC")
output = root / "Logs/Temperature/T1_2/native"
output.mkdir(parents=True, exist_ok=True)
environment = dict(os.environ)
environment["PATH"] = str(Path(compiler).parent) + os.pathsep + environment["PATH"]
for name, source, mode in [
    ("temperature_console", "App/Console/temperature_console.c", False),
    ("temperature_console", "App/Console/temperature_console.c", True),
    ("bsp_console_rx", "App/BSP/bsp_console.c", False),
]:
    suffix = "_mode" if mode else ""
    executable = output / (name + suffix + ".exe")
    flags = ["-DSBCG_MODE_TEST_CONTROL=1", "-DSBCG_USE_MODE_OBSERVATION=1"] if mode else []
    command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic", *flags,
               "-I", str(root / "Tests/Console/stubs"), "-I", str(root / "App"),
               str(root / f"Tests/Console/test_{name}.c"), str(root / source),
               "-o", str(executable)]
    result = subprocess.run(command, env=environment, timeout=30)
    if result.returncode:
        sys.exit(result.returncode)
    result = subprocess.run([str(executable)], env=environment, timeout=30)
    if result.returncode:
        sys.exit(result.returncode)
