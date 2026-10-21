"""Native integration: real old/new wrappers, vendor LwIP headers, replaced IO."""
from pathlib import Path
import argparse
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--log", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    gcc = shutil.which("gcc")
    if not gcc:
        print("Native GCC is required", file=sys.stderr)
        return 1
    output = root / "Logs/M2_2_Time_Exchange/test_udp_time_exchange.exe"
    output.parent.mkdir(parents=True, exist_ok=True)
    command = [gcc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(root / "App"),
               "-I", str(root / "Tests/Ptp/stubs"), "-DSTM32F746xx", "-DHAL_ETH_USE_PTP"]
    for directory in ["Core/Inc", "Drivers/STM32F7xx_HAL_Driver/Inc",
                      "Drivers/CMSIS/Device/ST/STM32F7xx/Include", "Drivers/CMSIS/Include"]:
        command += ["-isystem", str(root / directory)]
    for directory in ["LWIP/Target", "Middlewares/Third_Party/LwIP/src/include",
                      "Middlewares/Third_Party/LwIP/system"]:
        command += ["-I", str(root / directory)]
    command += [str(root / source) for source in [
        "Tests/TimeExchange/test_udp_time_exchange.c", "App/Network/udp_time_exchange.c",
        "App/Sync/ptp_rx.c", "App/Sync/ptp_tx.c"]]
    command += ["-o", str(output)]
    chunks = []
    for action in [command, [str(output)]]:
        result = subprocess.run(action, cwd=root, text=True, capture_output=True,
                                check=False, timeout=30)
        chunks.append(result.stdout + result.stderr)
        print(chunks[-1], end="", flush=True)
        if result.returncode:
            chunks.append(f"Exit status: {result.returncode}\n")
            break
    if args.log:
        target = args.log if args.log.is_absolute() else root / args.log
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text("".join(chunks), encoding="utf-8")
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
