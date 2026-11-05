"""Build the production boot-counter record core natively and run its host tests."""

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

    common = [
        compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
        "-pedantic", "-I", str(root / "App"),
    ]
    with tempfile.TemporaryDirectory(prefix="sbcg-boot-counter-") as temporary:
        out = Path(temporary)
        executable = out / ("boot_counter_record.exe" if os.name == "nt" else "boot_counter_record")
        subprocess.run(
            [*common, str(root / "Tests/BootCounter/test_boot_counter_record.c"),
             str(root / "App/BSP/boot_counter_record.c"), "-o", str(executable)],
            check=True,
        )
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
