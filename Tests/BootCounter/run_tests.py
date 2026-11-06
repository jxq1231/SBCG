"""Build the production boot-counter core and wait budgets natively, then run the host tests."""

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
    cases = (
        ("boot_counter_record",
         ["Tests/BootCounter/test_boot_counter_record.c", "App/BSP/boot_counter_record.c"]),
        ("flash_wait_budget",
         ["Tests/BootCounter/test_flash_wait_budget.c"]),
    )
    with tempfile.TemporaryDirectory(prefix="sbcg-boot-counter-") as temporary:
        out = Path(temporary)
        for name, sources in cases:
            executable = out / (name + (".exe" if os.name == "nt" else ""))
            subprocess.run(
                [*common, *[str(root / path) for path in sources], "-o", str(executable)],
                check=True,
            )
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
