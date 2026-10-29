"""Run native PPS register-model and PTP stop-guard tests using vendor headers."""
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    root = Path(__file__).resolve().parents[2]
    output = root / "Logs/PPS/M2_3_3"
    output.mkdir(parents=True, exist_ok=True)
    gcc = shutil.which("gcc")
    if not gcc:
        raise RuntimeError("Native GCC required")
    sources = [root / "Tests/PpsBsp/test_bsp_pps.c"]
    production = root / "App/BSP/bsp_pps.c"
    phase = "GREEN" if production.exists() else "RED"
    if production.exists():
        sources.append(production)
    else:
        # A compiling missing-feature substitute makes RED an assertion failure,
        # rather than a compiler/linker failure; it is never a firmware source.
        missing = output / "pps_missing.c"
        missing.write_text('#include "BSP/bsp_pps.h"\n'
            'BSP_PpsStatus BSP_PPS_Init(void){return BSP_PPS_NOT_READY;}\n'
            'BSP_PpsStatus BSP_PPS_Stop(void){return BSP_PPS_OK;}\n'
            'BSP_PpsStatus BSP_PPS_Process(uint32_t n,bool b){(void)n;(void)b;return BSP_PPS_NOT_READY;}\n'
            'bool BSP_PPS_GetSnapshot(BSP_PpsSnapshot *s){if(!s)return false;*s=(BSP_PpsSnapshot){0};return true;}\n', encoding="utf-8")
        sources.append(missing)
    command = [gcc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-DSTM32F746xx", "-DHAL_ETH_USE_PTP",
               "-DSBCG_USE_PPS=1", "-I", str(root / "Tests/PpsBsp/stubs"), "-I", str(root / "App")]
    for directory in ["Core/Inc", "Drivers/STM32F7xx_HAL_Driver/Inc", "Drivers/CMSIS/Device/ST/STM32F7xx/Include", "Drivers/CMSIS/Include"]:
        command += ["-isystem", str(root / directory)]
    exe = output / "test_bsp_pps.exe"
    command += list(map(str, sources)) + ["-o", str(exe)]
    built = subprocess.run(command, cwd=root, capture_output=True, text=True, timeout=40)
    log = "BUILD\n" + built.stdout + built.stderr
    if built.returncode:
        (output / f"bsp_{phase}.log").write_text(log, encoding="utf-8")
        print(log)
        return built.returncode
    result = subprocess.run([str(exe)], cwd=root, capture_output=True, text=True, timeout=40)
    log += "RUN\n" + result.stdout + result.stderr + f"\nexit={result.returncode}\n"
    (output / f"bsp_{phase}.log").write_text(log, encoding="utf-8")
    print(log)
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
