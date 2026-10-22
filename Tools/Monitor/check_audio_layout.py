"""Validate the linked DMA buffer, startup clearing and newlib heap; host only."""
import argparse
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def audio_layout_checks(symbols, sections):
    checks = []
    def check(ok, message):
        checks.append((bool(ok), message))
    try:
        kind, start, _, size, flags = sections[".AudioDmaSection"]
        finish = start + size
        check(kind == "NOBITS" and "A" in flags and "W" in flags,
              "audio is writable, allocated NOLOAD storage")
        check(start == 0x20010000 and size == 65536 and start % 32 == 0,
              "64 KiB audio buffer is aligned in DMA-accessible SRAM1")
        check(symbols["audio_dma_buffer"] == (start, size), "DMA object fills its section")
        check(symbols["__audio_dma_start__"][0] == start and
              symbols["__audio_dma_end__"][0] == finish, "reservation symbols match DMA section")
        check(symbols["_edata"][0] <= start and symbols["_ebss"][0] <= start and
              symbols["_sbss"][0] <= symbols["_ebss"][0],
              "startup data copying and BSS clearing stay below audio")
        heap = symbols["_end"][0]
        check(heap >= finish and symbols["end"][0] == heap and
              heap + symbols["_Min_Heap_Size"][0] <= symbols["_sstack"][0],
              "newlib heap starts after audio and fits below MSP")
        check(symbols["_estack"][0] == 0x20048000 and
              symbols["_estack"][0] - symbols["_sstack"][0] >= 4096,
              "at least 4 KiB stack remains below fixed network memory")
        for name, (_, address, _, length, attrs) in sections.items():
            if name != ".AudioDmaSection" and "A" in attrs and length:
                check(address + length <= start or address >= finish,
                      f"{name} does not overlap audio DMA")
                check(address + length <= heap or address >= symbols["_sstack"][0],
                      f"{name} does not overlap the growing newlib heap")
    except KeyError as error:
        check(False, f"missing required section/symbol: {error}")
    return checks


def read_elf(elf):
    def output(tool, *args):
        return subprocess.check_output([tool, *map(str, args), str(elf)],
                                       text=True, encoding="utf-8", timeout=30)
    symbols = {}
    for line in output("arm-none-eabi-nm", "-n", "-S").splitlines():
        parts = line.split()
        if len(parts) == 4:
            symbols[parts[3]] = (int(parts[0], 16), int(parts[1], 16))
        elif len(parts) == 3:
            symbols[parts[2]] = (int(parts[0], 16), 0)
    sections = {}
    pattern = re.compile(r"\[\s*\d+\]\s+(\S+)\s+(\S+)\s+([0-9a-fA-F]+)\s+"
                         r"([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+\S+\s+(\S+)")
    for line in output("arm-none-eabi-readelf", "-SW").splitlines():
        match = pattern.search(line)
        if match:
            name, kind, address, offset, size, flags = match.groups()
            sections[name] = kind, int(address, 16), int(offset, 16), int(size, 16), flags
    return symbols, sections


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", choices=("Debug", "Release"), default="Debug")
    parser.add_argument("--elf", type=Path)
    args = parser.parse_args()
    try:
        checks = audio_layout_checks(*read_elf(args.elf or ROOT / "build" / args.preset / "SBCG.elf"))
        for ok, message in checks:
            print(("PASS: " if ok else "FAIL: ") + message)
        success = all(ok for ok, _ in checks)
        print("AUDIO_LAYOUT_OK (host only)" if success else "AUDIO_LAYOUT_FAILED")
        return 0 if success else 1
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"Audio layout check failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
