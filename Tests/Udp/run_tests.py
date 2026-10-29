"""Native sender tests with real LwIP declarations and substituted IO only."""
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    root = Path(__file__).resolve().parents[2]
    compiler = shutil.which("gcc")
    if not compiler:
        print("Native GCC is required", file=sys.stderr)
        return 1
    output = root / "Logs/M1_2_UDP/test_udp_heartbeat.exe"
    output.parent.mkdir(parents=True, exist_ok=True)
    includes = ["App", "Tests/Network/stubs", "LWIP/Target",
                "Middlewares/Third_Party/LwIP/src/include",
                "Middlewares/Third_Party/LwIP/system"]
    command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror"]
    for include in includes:
        command.extend(["-I", str(root / include)])
    command += [str(root / "Tests/Udp/test_udp_heartbeat.c"),
                str(root / "App/Network/udp_heartbeat.c"), "-o", str(output)]
    result = subprocess.run(command, cwd=root, timeout=30, check=False)
    if result.returncode:
        return result.returncode
    return subprocess.run([str(output)], cwd=root, timeout=30, check=False).returncode


if __name__ == "__main__":
    sys.exit(main())
