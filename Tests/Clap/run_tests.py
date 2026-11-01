"""Native CLAP pure logic tests, without hardware or network."""
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "Logs/Clap/Tests"
OUT.mkdir(parents=True, exist_ok=True)
compiler = shutil.which("gcc")
if not compiler:
    raise SystemExit("gcc not found")
result = 0
for opt in ("-O0", "-O2"):
    for name, sources in (
        ("test_pair_clap", ["App/Measure/sound_pair.c"]),
        ("test_clap_ranging", ["App/Measure/clap_ranging.c"]),
        ("test_clap_wire", ["App/Network/clap_wire.c"]),
    ):
        exe = OUT / (name + opt[1:] + ".exe")
        built = subprocess.run([compiler, "-std=c11", opt, "-Wall", "-Wextra",
                                "-Werror", "-pedantic", "-I", str(ROOT / "App"),
                                str(ROOT / "Tests/Clap" / (name + ".c")),
                                *[str(ROOT / path) for path in sources], "-o", str(exe)],
                               capture_output=True, text=True)
        print(built.stdout + built.stderr, end="")
        if built.returncode:
            result = result or built.returncode
            continue
        ran = subprocess.run([str(exe)], capture_output=True, text=True)
        print(ran.stdout + ran.stderr, end="")
        result = result or ran.returncode
sys.exit(result)
