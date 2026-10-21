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


def lwip_layout_checks(symbols, sections, heap_address, heap_size, required_stack_bytes=0):
    """Validate the fixed heap and RX pool independently of external tools."""
    checks = []

    def check(condition, message):
        checks.append((bool(condition), message))

    heap = sections.get(".LwipHeapSection")
    pool = sections.get(".RxPoolSection")
    check(heap is not None, "LwIP heap has a reserved ELF section")
    check(pool is not None, "LwIP RX pool has a reserved ELF section")
    if heap is None or pool is None:
        return checks
    heap_end = heap[1] + heap[3]
    pool_end = pool[1] + pool[3]
    check(heap[0] == "NOBITS" and heap[1] == heap_address == 0x20048000
          and heap[3] == 0x4000 and heap_end == SRAM2_START,
          "LwIP heap: matching fixed address, 16 KiB NOLOAD reservation")
    # This integration pins MEM_SIZE=4096/MEM_ALIGNMENT=4, with two 8-byte
    # allocator headers in the accompanying LwIP 2.1.2 mem.c implementation.
    check(heap_size > 0 and heap_size + 16 <= heap[3],
          "LwIP heap reservation includes allocator overhead")
    stack_top = symbols["_estack"][0]
    stack_bottom = symbols["_sstack"][0]
    check(stack_top == heap_address and stack_bottom < stack_top,
          "MSP is below the fixed LwIP heap")
    check(stack_top - stack_bottom == symbols['_Min_Stack_Size'][0]
          and stack_top - stack_bottom >= required_stack_bytes,
          f"MSP reservation matches size and is at least {required_stack_bytes} bytes")
    check(symbols["_end"][0] <= stack_bottom,
          "newlib heap starts below reserved MSP stack")
    rx_address, rx_size = symbols["DMARxDscrTab"]
    check(pool[0] == "NOBITS" and pool[1] % 32 == 0 and pool[3] > 0
          and rx_address + rx_size <= pool[1] and pool_end <= RAM_END,
          "RX pool: aligned NOLOAD region after descriptors, within SRAM2")
    storage = symbols.get("memp_memory_RX_POOL_base")
    check(storage is not None and storage[0] == pool[1] and storage[1] > 0
          and storage[0] + storage[1] <= pool_end,
          "RX pool storage symbol is contained in its reserved region")
    reserved = {".LwipHeapSection", ".RxPoolSection", ".TxDecripSection", ".RxDecripSection"}
    for name, (_, address, _, size, flags) in sections.items():
        if "A" in flags and 0x20000000 <= address < RAM_END and size and name not in reserved:
            check(address + size <= stack_bottom,
                  f"{name}: outside reserved stack, LwIP heap and ETH regions")
    return checks


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
        has_lwip = "LWIP.Version" in fields
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
        expected_stack_top = 0x20048000 if has_lwip else SRAM2_START
        check(stack_top == expected_stack_top and stack_bottom < stack_top,
              f"MSP range below reserved network memory: 0x{stack_bottom:08X}..0x{stack_top:08X}")
        check(symbols["_end"][0] <= stack_bottom and symbols["_ebss"][0] <= stack_bottom,
              "static data and newlib heap start below reserved MSP stack")
        reserved = {".TxDecripSection", ".RxDecripSection"}
        if has_lwip:
            reserved.update({".LwipHeapSection", ".RxPoolSection"})
        for name, (_, address, _, size, flags) in sections.items():
            if "A" in flags and 0x20000000 <= address < RAM_END and size:
                if name not in reserved:
                    check(address + size <= stack_bottom,
                          f"{name}: outside reserved stack and ETH region")
        if has_lwip:
            options = (args.ioc.parent / "LWIP/Target/lwipopts.h").read_text(encoding="utf-8")
            heap_address = int(re.search(
                r"^#define\s+LWIP_RAM_HEAP_POINTER\s+(0x[0-9a-fA-F]+)\s*$", options, re.M)[1], 0)
            heap_size = int(re.search(r"^#define\s+MEM_SIZE\s+(\d+)\s*$", options, re.M)[1])
            minimum_stack = 4096 if fields.get('NVIC.USART1_IRQn', '').startswith('true') else 0
            for condition, message in lwip_layout_checks(
                    symbols, sections, heap_address, heap_size, minimum_stack):
                check(condition, message)
        vector_offset = sections[".isr_vector"][2]
        vector_msp = int.from_bytes(elf.read_bytes()[vector_offset:vector_offset + 4], "little")
        check(vector_msp == stack_top,
              f"initial vector MSP=0x{vector_msp:08X}, matches _estack")
        success = all(checks)
        print("ETH_LAYOUT_OK (host only)" if success else "ETH_LAYOUT_FAILED")
        return 0 if success else 1
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as exc:
        print(f"ETH layout check failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
