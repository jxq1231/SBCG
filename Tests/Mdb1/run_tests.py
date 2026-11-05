"""Build the production MDB1 business wrapper natively and run its host tests."""

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
    with tempfile.TemporaryDirectory(prefix="sbcg-mdb1-") as temporary:
        out = Path(temporary)
        executable = out / ("business_wire.exe" if os.name == "nt" else "business_wire")
        subprocess.run(
            [*common, str(root / "Tests/Mdb1/test_business_wire.c"),
             str(root / "App/Network/business_wire.c"), "-o", str(executable)],
            check=True,
        )
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
