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
    output = ROOT / "Logs/Audio/M3_1/tests"
    output.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ)
    environment["PATH"] = str(Path(compiler).parent) + os.pathsep + environment["PATH"]
    failures = 0
    cases = [
        ("audio_frame_index", "stubs", ["App/Audio/audio_frame_index.c"]),
        ("bsp_audio_codec", "stubs", ["App/BSP/bsp_audio_codec.c"]),
        ("codec_monitor", "stubs", ["App/Audio/codec_monitor.c"]),
        ("codec_setup", "codec_stubs", ["App/BSP/bsp_audio_codec.c"]),
        ("audio_statistics", "stubs", ["App/Audio/audio_statistics.c"]),
        ("bsp_audio", "dma_stubs", ["App/BSP/bsp_audio.c", "App/Audio/audio_frame_index.c"]),
        ("audio_capture", "stubs", ["App/Audio/audio_capture.c", "App/Audio/audio_statistics.c"]),
    ]
    for name, stubs, sources in cases:
        executable = output / (name + (".exe" if os.name == "nt" else ""))
        command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic",
                   "-I", str(ROOT / "Tests/Audio" / stubs), "-I", str(ROOT / "App"),
                   str(ROOT / f"Tests/Audio/test_{name}.c"), *map(str, (ROOT / s for s in sources)),
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
