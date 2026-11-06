/* User-owned extension of the unchanged CubeMX memory/section definitions. */
INCLUDE STM32F746xx_FLASH.ld

/* The diagnostic image adds 8 KiB below MSP for the paint/observe sweep.
   The production image reserves 6 KiB instead of the CubeMX 4 KiB: measured
   MSP high-water marks are 3668-3812 B (perf review 2026-11-06) and the
   deepest publish chain adds ISR frames on the same MSP, so 4 KiB leaves only
   284-428 B. The extra 2 KiB reduces the newlib heap, not the fixed regions.
   Keep the CubeMX base script and the fixed network regions independent. */
PROVIDE(__sbcg_stack_diagnostic = 0);
/* Optional probe reserve lets the paint/observe image measure the exact
   6 KiB production reserve instead of the default 8 KiB diagnostic window. */
PROVIDE(__sbcg_stack_reserve = 0);
_Min_Stack_Size = __sbcg_stack_reserve ? __sbcg_stack_reserve :
    (__sbcg_stack_diagnostic ? 0x2000 : 0x1800);

/* CMake defines this only for the generated LwIP configuration. Keep legacy
   PHY-only snapshots buildable with their original stack location. */
PROVIDE(__sbcg_has_lwip = 0);
/* sysmem.c derives the newlib heap limit from _estack - _Min_Stack_Size. */
_estack = __sbcg_has_lwip ? 0x20048000 : 0x2004C000;
_sstack = _estack - _Min_Stack_Size;

SECTIONS
{
  /* Includes allocator headers/alignment and leaves growth room; the reviewed
     LwIP MEM_SIZE is 4096, not the size of this reserved window. */
  .LwipHeapSection 0x20048000 (NOLOAD) : ALIGN(32)
  {
    __lwip_heap_start__ = .;
    . += __sbcg_has_lwip ? 0x4000 : 0;
    __lwip_heap_end__ = .;
  } >RAM
  /* Match the current CubeMX Tx/Rx addresses and exact generated section names.
     NOLOAD avoids a Flash load image; HAL_ETH_Init initializes the descriptors. */
  .TxDecripSection 0x2004C000 (NOLOAD) : ALIGN(32)
  {
    KEEP(*(.TxDecripSection))
    KEEP(*(.TxDescripSection))
  } >RAM

  .RxDecripSection 0x2004C0A0 (NOLOAD) : ALIGN(32)
  {
    KEEP(*(.RxDecripSection))
    KEEP(*(.RxDescripSection))
  } >RAM

  .RxPoolSection 0x2004C140 (NOLOAD) : ALIGN(32)
  {
    KEEP(*(.RxPoolSection))
    . = ALIGN(32);
  } >RAM
}

ASSERT(ORIGIN(RAM) == 0x20000000 && ORIGIN(RAM) + LENGTH(RAM) == 0x20050000,
       "ETH layout expects STM32F746 320 KiB RAM; review generated memory map")
ASSERT(SIZEOF(.TxDecripSection) == 160 && SIZEOF(.RxDecripSection) == 160,
       "ETH descriptor count/size changed; review CubeMX addresses and reserved spans")
ASSERT(DMATxDscrTab == ADDR(.TxDecripSection) && DMARxDscrTab == ADDR(.RxDecripSection),
       "ETH descriptor symbols do not match reserved section starts")
ASSERT(ADDR(.TxDecripSection) + SIZEOF(.TxDecripSection) <= ADDR(.RxDecripSection),
       "ETH Tx and Rx descriptors overlap")
ASSERT(ADDR(.RxDecripSection) + SIZEOF(.RxDecripSection) <= ORIGIN(RAM) + LENGTH(RAM),
       "ETH descriptors exceed SRAM2")
ASSERT(!__sbcg_has_lwip || SIZEOF(.LwipHeapSection) == 0x4000,
       "LwIP fixed heap window is not fully reserved")
ASSERT(!__sbcg_has_lwip || _estack == ADDR(.LwipHeapSection),
       "MSP must stay below the fixed LwIP heap")
ASSERT(!__sbcg_has_lwip || __lwip_heap_end__ <= ADDR(.TxDecripSection),
       "LwIP heap overlaps Ethernet descriptors")
ASSERT(ADDR(.RxDecripSection) + SIZEOF(.RxDecripSection) <= ADDR(.RxPoolSection),
       "RX pool overlaps Ethernet descriptors")
ASSERT(!__sbcg_has_lwip || SIZEOF(.RxPoolSection) > 0,
       "LwIP RX pool must be placed in reserved SRAM2")
ASSERT(ADDR(.RxPoolSection) + SIZEOF(.RxPoolSection) <= ORIGIN(RAM) + LENGTH(RAM),
       "LwIP RX pool exceeds SRAM2")
ASSERT((_sstack % 8) == 0 && (_estack % 8) == 0,
       "MSP reserve must have aligned bounds")
ASSERT(_ebss <= _sstack && _edata <= _sstack && _end + _Min_Heap_Size <= _sstack,
       "Static data or minimum heap overlaps reserved MSP/ETH memory")
ASSERT(ADDR(._user_heap_stack) + SIZEOF(._user_heap_stack) <= _estack,
       "Insufficient application RAM below ETH reservation")

/* Boot-counter sectors 6/7 start at 0x080C0000 (see App/BSP/bsp_boot_counter.c);
   erasing them to rotate a record would delete the code that is executing, and
   the next reset would not run. The generated FLASH region is a full 1 MiB, so
   only this explicit end-of-image bound keeps the image out of the reservation.
   __fini_array_end closes .text/.rodata/.ARM*; the .data/.tdata load image that
   follows is the highest allocated FLASH content (orphan read-only input such
   as itcm_code_src is placed with .text by the linker). */
ASSERT(MAX(__fini_array_end, _sidata + SIZEOF(.data) + SIZEOF(.tdata)) <= 0x080C0000,
       "App image overlaps boot-counter sectors 6/7 at 0x080C0000")
