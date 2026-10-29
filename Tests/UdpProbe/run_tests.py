"""Test the real probe application with real LwIP declarations and fake IO."""
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    root = Path(__file__).resolve().parents[2]
    gcc = shutil.which("gcc")
    if not gcc:
        print("Native GCC is required", file=sys.stderr)
        return 1
    output = root / "Logs/M1_3_UDP/test_udp_probe.exe"
    output.parent.mkdir(parents=True, exist_ok=True)
    command = [gcc, "-std=c11", "-Wall", "-Wextra", "-Werror"]
    for directory in ["App", "Tests/Network/stubs", "LWIP/Target",
                      "Middlewares/Third_Party/LwIP/src/include",
                      "Middlewares/Third_Party/LwIP/system"]:
        command += ["-I", str(root / directory)]
    command += [str(root / "Tests/UdpProbe/test_udp_probe.c"),
                str(root / "App/Network/udp_probe.c"), "-o", str(output)]
    result = subprocess.run(command, cwd=root, check=False, timeout=30)
    if result.returncode:
        return result.returncode
    return subprocess.run([str(output)], cwd=root, check=False, timeout=30).returncode


if __name__ == "__main__":
    sys.exit(main())
