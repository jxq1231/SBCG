"""Check a built STM32F746 ELF against CubeMX ETH addresses; no device access."""

import argparse
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
SRAM2_START = 0x2004C000
RAM_END = 0x20050000
DESCRIPTOR_BYTES = 160  # Current configuration: 4 F7 descriptors, 40 bytes each.


def tool_output(tool, *args):
    return subprocess.check_output(
        [tool, *map(str, args)], text=True, encoding="utf-8", timeout=30
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", choices=("Debug", "Release"), default="Debug")
    parser.add_argument("--elf", type=Path)
    parser.add_argument("--ioc", type=Path, default=ROOT / "SBCG.ioc")
    args = parser.parse_args()
    elf = args.elf or ROOT / "build" / args.preset / "SBCG.elf"
    try:
        fields = dict(line.split("=", 1) for line in
                      args.ioc.read_text(encoding="utf-8").splitlines() if "=" in line)
        symbols = {}
        for line in tool_output("arm-none-eabi-nm", "-n", "-S", elf).splitlines():
            parts = line.split()
            if len(parts) == 4:
                symbols[parts[3]] = (int(parts[0], 16), int(parts[1], 16))
            elif len(parts) == 3:
                symbols[parts[2]] = (int(parts[0], 16), 0)
        sections = {}
        pattern = re.compile(
            r"\[\s*\d+\]\s+(\S+)\s+(\S+)\s+([0-9a-fA-F]+)\s+"
            r"([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+\S+\s+(\S+)"
        )
        for line in tool_output("arm-none-eabi-readelf", "-SW", elf).splitlines():
            match = pattern.search(line)
            if match:
                name, kind, address, offset, size, flags = match.groups()
                sections[name] = (kind, int(address, 16), int(offset, 16),
                                  int(size, 16), flags)

        checks = []

        def check(condition, message):
            checks.append(bool(condition))
            print(("PASS: " if condition else "FAIL: ") + message)

        for symbol, section, config in (
            ("DMATxDscrTab", ".TxDecripSection", "ETH.TxDescAddress"),
            ("DMARxDscrTab", ".RxDecripSection", "ETH.RxDescAddress"),
        ):
            address, size = symbols[symbol]
            expected = int(fields[config], 0)
            check(address == expected,
                  f"{symbol}: actual=0x{address:08X}, CubeMX=0x{expected:08X}")
            check(size == DESCRIPTOR_BYTES and address % 32 == 0,
                  f"{symbol}: 160 bytes, base aligned to 32 bytes")
            check(SRAM2_START <= address and address + size <= RAM_END,
                  f"{symbol}: contained in SRAM2")
            check(sections[section][0] == "NOBITS" and sections[section][1] == address,
                  f"{section}: NOLOAD, same address as symbol")

        tx, tx_size = symbols["DMATxDscrTab"]
        rx, rx_size = symbols["DMARxDscrTab"]
        check(tx + tx_size <= rx or rx + rx_size <= tx,
              "Tx and Rx descriptors do not overlap")
        stack_top = symbols["_estack"][0]
        stack_bottom = symbols["_sstack"][0]
        check(stack_top == SRAM2_START and stack_bottom < stack_top,
              f"MSP range below SRAM2: 0x{stack_bottom:08X}..0x{stack_top:08X}")
        check(symbols["_end"][0] <= stack_bottom and symbols["_ebss"][0] <= stack_bottom,
              "static data and newlib heap start below reserved MSP stack")
        for name, (_, address, _, size, flags) in sections.items():
            if "A" in flags and 0x20000000 <= address < RAM_END and size:
                if name not in (".TxDecripSection", ".RxDecripSection"):
                    check(address + size <= stack_bottom,
                          f"{name}: outside reserved stack and ETH region")
        vector_offset = sections[".isr_vector"][2]
        vector_msp = int.from_bytes(elf.read_bytes()[vector_offset:vector_offset + 4], "little")
        check(vector_msp == stack_top,
              f"initial vector MSP=0x{vector_msp:08X}, matches _estack")
        success = all(checks)
        print("ETH_LAYOUT_OK (host only)" if success else "ETH_LAYOUT_FAILED")
        return 0 if success else 1
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as exc:
        print(f"ETH layout check failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
