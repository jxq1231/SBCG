"""Native PTP tests with real vendor declarations and replaced register IO."""
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
    for name, source in [("bsp_ptp", "App/BSP/bsp_ptp.c"),
                         ("bsp_ptp_fine", None),
                         ("ptp_clock", "App/Sync/ptp_clock.c"),
                         ("ptp_rx", "App/Sync/ptp_rx.c"),
                         ("bsp_ptp_tx", "App/BSP/bsp_ptp_tx.c"),
                         ("ptp_tx", "App/Sync/ptp_tx.c")]:
        output = root / "Logs/M2_1_PTP" / ("test_" + name + ".exe")
        output.parent.mkdir(parents=True, exist_ok=True)
        command = [gcc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                   "-DSTM32F746xx", "-DHAL_ETH_USE_PTP", "-I", str(root / "App"),
                   "-I", str(root / "Tests/Ptp/stubs")]
        for directory in ["Core/Inc", "Drivers/STM32F7xx_HAL_Driver/Inc",
                          "Drivers/CMSIS/Device/ST/STM32F7xx/Include",
                          "Drivers/CMSIS/Include"]:
            command += ["-isystem", str(root / directory)]
        if name == "ptp_clock":
            command += ["-Wl,--wrap=__mingw_snprintf", "-Wl,--wrap=snprintf"]
        if name == "bsp_ptp_tx":
            command += ["-DSBCG_TEST_PTP_TX"]
        if name == "bsp_ptp_fine":
            command += ["-DSBCG_TEST_PTP_FINE"]
        if name in ("ptp_rx", "ptp_tx"):
            for directory in ["LWIP/Target", "Middlewares/Third_Party/LwIP/src/include",
                              "Middlewares/Third_Party/LwIP/system"]:
                command += ["-I", str(root / directory)]
        command += [str(root / "Tests/Ptp" / ("test_" + name + ".c"))]
        if source is not None:
            command += [str(root / source)]
        command += ["-o", str(output)]
        result = subprocess.run(command, cwd=root, check=False, timeout=30)
        if result.returncode:
            return result.returncode
        result = subprocess.run([str(output)], cwd=root, check=False, timeout=30)
        if result.returncode:
            return result.returncode
    return 0


if __name__ == "__main__":
    sys.exit(main())
