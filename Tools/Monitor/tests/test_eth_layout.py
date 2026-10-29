import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    "check_eth_layout", Path(__file__).resolve().parents[1] / "check_eth_layout.py")
layout = importlib.util.module_from_spec(spec)
spec.loader.exec_module(layout)


class LwipLayoutTests(unittest.TestCase):
    def setUp(self):
        self.symbols = {
            "_estack": (0x20048000, 0), "_sstack": (0x20047C00, 0),
            "_Min_Stack_Size": (0x400, 0), "_end": (0x20004000, 0),
            "DMARxDscrTab": (0x2004C0A0, 160),
            "memp_memory_RX_POOL_base": (0x2004C140, 6275),
        }
        self.sections = {
            ".LwipHeapSection": ("NOBITS", 0x20048000, 0, 0x4000, "WA"),
            ".RxPoolSection": ("NOBITS", 0x2004C140, 0, 6304, "WA"),
            ".bss": ("NOBITS", 0x20000100, 0, 0x3000, "WA"),
            ".RxDecripSection": ("NOBITS", 0x2004C0A0, 0, 160, "WA"),
        }

    def check(self, valid, symbols=None, sections=None, address=0x20048000, size=4096):
        checks = layout.lwip_layout_checks(
            symbols or self.symbols, sections or self.sections, address, size)
        self.assertEqual(all(ok for ok, _ in checks), valid, checks)

    def test_reserved_heap_and_pool_pass(self):
        self.check(True)

    def test_stack_above_fixed_heap_fails(self):
        self.symbols["_estack"] = (0x2004C000, 0)
        self.check(False)

    def test_unreserved_fixed_heap_fails(self):
        del self.sections[".LwipHeapSection"]
        self.check(False)

    def test_allocator_overhead_must_fit(self):
        self.check(False, size=0x4000)

    def test_cube_heap_address_must_match_reservation(self):
        self.check(False, address=0x20047000)

    def test_pool_cannot_exceed_sram(self):
        self.sections[".RxPoolSection"] = ("NOBITS", 0x2004C140, 0, 0x4000, "WA")
        self.check(False)

    def test_pool_cannot_overlap_descriptors(self):
        self.sections[".RxPoolSection"] = ("NOBITS", 0x2004C100, 0, 6304, "WA")
        self.symbols["memp_memory_RX_POOL_base"] = (0x2004C100, 6275)
        self.check(False)

    def test_pool_symbol_must_fit_section(self):
        self.symbols["memp_memory_RX_POOL_base"] = (0x2004C140, 7000)
        self.check(False)

    def test_unknown_ram_section_cannot_enter_reservation(self):
        self.sections[".unexpected"] = ("NOBITS", 0x20049000, 0, 64, "WA")
        self.check(False)

    def test_static_end_cannot_enter_reserved_stack(self):
        self.symbols["_end"] = (0x20047C04, 0)
        self.check(False)

    def test_uart_irq_requires_larger_stack_reservation(self):
        checks = layout.lwip_layout_checks(self.symbols, self.sections,
                                          0x20048000, 4096, required_stack_bytes=4096)
        self.assertFalse(all(ok for ok, _ in checks))
        self.symbols['_sstack'] = (0x20047000, 0)
        self.symbols['_Min_Stack_Size'] = (0x1000, 0)
        checks = layout.lwip_layout_checks(self.symbols, self.sections,
                                          0x20048000, 4096, required_stack_bytes=4096)
        self.assertTrue(all(ok for ok, _ in checks), checks)
