"""主机波形服务测试，不连接或烧录设备。"""
import os
from pathlib import Path
import shutil
import subprocess
ROOT = Path(__file__).resolve().parents[2]
cc = os.environ.get("CC") or shutil.which("gcc")
out = ROOT / "Logs/Waveform"
out.mkdir(parents=True, exist_ok=True)
for name, extra, macros in [
    ("waveform", [], []),
    ("audio_capture", ["App/Audio/audio_capture.c", "App/Audio/audio_statistics.c"],
     ["-DSBCG_USE_ADVANCED_UI=1"]),
]:
    exe = out / (name + ".exe")
    sources = [f"Tests/Waveform/test_{name}.c", "App/UI/waveform_service.c",
               "App/Audio/audio_time_map.c", *extra]
    subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic", *macros,
                    "-I", str(ROOT / "App"), *[str(ROOT / p) for p in sources],
                    "-lm", "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
