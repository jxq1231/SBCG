"""Native codec tests; no board access. Outputs stay in ignored Logs/."""
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]

def main():
    compiler = os.environ.get("CC") or shutil.which("gcc")
    if not compiler:
        raise RuntimeError("Native GCC required; set CC")
    output = ROOT / "Logs/Audio/M3_0/tests"
    output.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ)
    environment["PATH"] = str(Path(compiler).parent) + os.pathsep + environment["PATH"]
    failures = 0
    for name, source in [("bsp_audio_codec", "App/BSP/bsp_audio_codec.c"),
                         ("codec_monitor", "App/Audio/codec_monitor.c")]:
        executable = output / (name + (".exe" if os.name == "nt" else ""))
        command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic",
                   "-I", str(ROOT / "Tests/Audio/stubs"), "-I", str(ROOT / "App"),
                   str(ROOT / f"Tests/Audio/test_{name}.c"), str(ROOT / source),
                   "-o", str(executable)]
        built = subprocess.run(command, env=environment, timeout=30, check=False)
        if built.returncode:
            failures += 1
            continue
        checked = subprocess.run([str(executable)], env=environment, timeout=30, check=False)
        failures += checked.returncode != 0
    print(f"Audio tests: {failures} failed", flush=True)
    return int(failures != 0)

if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(1)
