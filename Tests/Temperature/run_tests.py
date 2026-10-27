"""Build and run native temperature tests; outputs stay under ignored Logs/."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "Logs" / "Temperature" / "T1_1"
SUITES = {
    "pure": [
        ("dht11_frame", ["Tests/Temperature/test_dht11_frame.c",
                         "App/Temperature/dht11_frame.c"]),
        ("sound_speed", ["Tests/Temperature/test_sound_speed.c",
                         "App/Temperature/sound_speed.c"]),
        ("temperature_wire", ["Tests/Temperature/test_temperature_wire.c",
                              "App/Network/temperature_wire.c",
                              "App/Temperature/sound_speed.c"]),
    ],
    "bsp": [("bsp_dht11", ["Tests/Temperature/test_bsp_dht11.c"])],
    "service": [("temperature_service", ["Tests/Temperature/test_temperature_service.c",
                   "App/Temperature/temperature_service.c",
                   "App/Temperature/dht11_frame.c", "App/Temperature/sound_speed.c"])],
}


def find_gcc():
    candidates = [os.environ.get("CC"), shutil.which("gcc"),
                  r"C:\msys64\ucrt64\bin\gcc.exe",
                  r"C:\msys64\mingw64\bin\gcc.exe"]
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            result = subprocess.run([candidate, "-dumpmachine"],
                                    capture_output=True, text=True, check=True,
                                    timeout=10)
            if not any(word in result.stdout for word in ("arm", "aarch64")):
                return str(candidate)
    raise RuntimeError("Native GCC not found; set CC to a native gcc executable")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--suite", choices=["pure", "bsp", "service", "all"],
                        default="all")
    args = parser.parse_args()
    gcc = find_gcc()
    OUTPUT.mkdir(parents=True, exist_ok=True)
    # Native MinGW executables may require runtime DLLs beside their compiler.
    environment = dict(os.environ)
    environment["PATH"] = str(Path(gcc).parent) + os.pathsep + environment["PATH"]
    failures = 0
    selected = SUITES if args.suite == "all" else [args.suite]
    for suite in selected:
        if not SUITES[suite]:
            message = f"{suite}: no test cases registered yet"
            raise RuntimeError(message)
        for name, sources in SUITES[suite]:
            executable = OUTPUT / (name + (".exe" if os.name == "nt" else ""))
            command = [gcc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic",
                       "-I", str(ROOT / "Tests/Temperature/stubs"),
                       "-I", str(ROOT / "App"), "-I", str(ROOT / "Tests/Temperature"),
                       *(str(ROOT / source) for source in sources),
                       "-o", str(executable)]
            print(f"BUILD {suite}/{name}", flush=True)
            built = subprocess.run(command, env=environment, check=False,
                                   timeout=30)
            if built.returncode:
                failures += 1
                continue
            result = subprocess.run([str(executable)], env=environment,
                                    check=False, timeout=30)
            if result.returncode:
                failures += 1
                print(f"FAIL {suite}/{name}: exit {result.returncode}", flush=True)
    print(f"Temperature tests: {failures} failed", flush=True)
    return int(failures != 0)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(1)
