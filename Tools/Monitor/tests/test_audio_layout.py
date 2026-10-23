import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    "check_audio_layout", Path(__file__).resolve().parents[1] / "check_audio_layout.py")
layout = importlib.util.module_from_spec(spec)
spec.loader.exec_module(layout)


class AudioLayoutTests(unittest.TestCase):
    def setUp(self):
        self.symbols = {"audio_dma_buffer": (0x20010000, 65536),
                        "__audio_dma_start__": (0x20010000, 0),
                        "__audio_dma_end__": (0x20020000, 0),
                        "_edata": (0x20000100, 0), "_sbss": (0x20000100, 0),
                        "_ebss": (0x20005000, 0), "_end": (0x20020000, 0),
                        "end": (0x20020000, 0), "_Min_Heap_Size": (512, 0),
                        "_sstack": (0x20047000, 0), "_estack": (0x20048000, 0)}
        self.sections = {".AudioDmaSection": ("NOBITS", 0x20010000, 0, 65536, "WA"),
                         ".bss": ("NOBITS", 0x20000100, 0, 0x4F00, "WA"),
                         ".data": ("PROGBITS", 0x20000000, 0, 256, "WA")}

    def good(self):
        return all(ok for ok, _ in layout.audio_layout_checks(self.symbols, self.sections))

    def test_isolated_dma_and_heap_pass(self):
        self.assertTrue(self.good())

    def test_missing_section_fails(self):
        del self.sections[".AudioDmaSection"]
        self.assertFalse(self.good())

    def test_no_dma_in_dtcm_or_flash_load_image(self):
        for kind, address in (("NOBITS", 0x20000000), ("PROGBITS", 0x20010000)):
            self.sections[".AudioDmaSection"] = (kind, address, 0, 65536, "WA")
            self.assertFalse(self.good())

    def test_wrong_size_or_symbol_fails(self):
        self.symbols["audio_dma_buffer"] = (0x20010002, 65534)
        self.assertFalse(self.good())

    def test_heap_cannot_start_below_or_inside_dma(self):
        for address in (0x20005000, 0x2001FFFF, 0x20047000):
            self.symbols["_end"] = (address, 0)
            self.assertFalse(self.good())

    def test_unexpected_allocated_section_cannot_overlap_dma(self):
        self.sections[".other"] = ("NOBITS", 0x2001FFE0, 0, 64, "WA")
        self.assertFalse(self.good())

    def test_startup_bss_clear_cannot_cross_dma(self):
        self.symbols["_ebss"] = (0x20010004, 0)
        self.assertFalse(self.good())

    def test_unexpected_static_storage_cannot_enter_runtime_heap(self):
        self.sections[".future_static"] = ("NOBITS", 0x20021000, 0, 64, "WA")
        self.assertFalse(self.good())

    def test_missing_symbol_and_small_stack_fail(self):
        self.symbols["_sstack"] = (0x20047C00, 0)
        self.assertFalse(self.good())
        del self.symbols["audio_dma_buffer"]
        self.assertFalse(self.good())
