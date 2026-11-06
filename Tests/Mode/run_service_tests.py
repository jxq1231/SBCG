"""Build two independent instances of the production mode service and run its host tests."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parents[2]
    compiler = os.environ.get("CC") or shutil.which("gcc")
    if compiler is None:
        raise SystemExit("Native GCC required; set CC")

    service = root / "App/Mode/mode_service.c"
    test = root / "Tests/Mode/test_mode_service.c"
    common = [
        compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
        "-pedantic", "-I", str(root / "App"),
    ]
    methods = ("Init", "SetBoot", "Request", "OnControl", "Process", "GetSnapshot")
    rename = [f"-DModeService_{method}=ModeServiceB_{method}" for method in methods]
    with tempfile.TemporaryDirectory(prefix="sbcg-mode-service-") as temporary:
        out = Path(temporary)
        peer = out / "mode_b.o"
        executable = out / ("mode_service.exe" if os.name == "nt" else "mode_service")
        subprocess.run([*common, *rename, "-c", str(service), "-o", str(peer)], check=True)
        subprocess.run(
            [*common, str(test), str(service), str(peer), "-o", str(executable)], check=True
        )
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
