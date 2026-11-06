"""Build the ETH link-guard decision core natively and run its host tests."""

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
    with tempfile.TemporaryDirectory(prefix="sbcg-eth-link-guard-") as temporary:
        out = Path(temporary)
        executable = out / ("eth_link_guard_tests.exe" if os.name == "nt" else "eth_link_guard_tests")
        subprocess.run(
            [*common, str(root / "Tests/EthLinkGuard/test_eth_link_guard.c"),
             str(root / "App/Network/eth_link_guard.c"), "-o", str(executable)],
            check=True,
        )
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
