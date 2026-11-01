"""Check the actual ELF reservation for CPU-only waveform history."""
from pathlib import Path
import argparse
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build', choices=('Debug', 'Release'), default='Debug')
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
mapping = (root / 'build' / args.build / 'SBCG.map').read_text()
match = re.search(r'^\.WaveformState\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)', mapping, re.M)
assert match, 'Missing CPU waveform reservation'
address, size = (int(value, 16) for value in match.groups())
assert address == 0xC00A0000 and 0 < size <= 128 * 1024, (address, size)
assert 0xC0080000 + 8192 <= address
assert address + 128 * 1024 <= 0xC0800000
reader = Path('D:/arm-gnu-toolchain-13.2.Rel1-mingw-w64-i686-arm-none-eabi/bin/arm-none-eabi-readelf.exe')
sections = subprocess.check_output([str(reader), '-SW', str(root / 'build' / args.build / 'SBCG.elf')], text=True)
assert re.search(r'\.WaveformState\s+NOBITS\s+c00a0000', sections)
print(f'PASS {args.build}: waveform CPU NOLOAD state {size} bytes at 0x{address:08X}; outside framebuffers and frontend')
