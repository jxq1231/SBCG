"""Inspect ELF/map for the bounded, CPU-only TouchGFX SDRAM state reservation."""
from pathlib import Path
import argparse
import re
import subprocess

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build', choices=['Debug', 'Release'], default='Debug')
args = parser.parse_args()
mapping = (root/f'build/{args.build}/SBCG.map').read_text()
section = re.search(r'^\.TouchGFXState\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)', mapping, re.M)
assert section, 'Missing independent TouchGFX SDRAM storage'
address, length = (int(value, 16) for value in section.groups())
assert address == 0xC0080000 and 0 < length <= 8192, (address, length)
assert 0xC0040000 + 480*272*2 <= address
assert address+length <= 0xC0800000
reader = Path('D:/arm-gnu-toolchain-13.2.Rel1-mingw-w64-i686-arm-none-eabi/bin/arm-none-eabi-readelf.exe')
sections = subprocess.check_output([str(reader), '-SW', str(root/f'build/{args.build}/SBCG.elf')], text=True)
assert re.search(r'\.TouchGFXState\s+NOBITS\s+c0080000', sections)
symbols = subprocess.check_output([str(reader), '-sW', str(root/f'build/{args.build}/SBCG.elf')], text=True)
assert re.search(r'c0080000\s+\d+\s+OBJECT.*FrontendHeap.*storage', symbols)
print(f'PASS {args.build}: TouchGFX NOLOAD state {length} bytes at 0x{address:08X}; outside both framebuffers')
