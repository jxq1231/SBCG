/* User-owned extension of the unchanged CubeMX memory/section definitions. */
INCLUDE STM32F746xx_FLASH.ld

/* Keep newlib heap and MSP below SRAM2, which is reserved for ETH resources.
   sysmem.c derives the heap limit from _estack - _Min_Stack_Size. */
_estack = 0x2004C000;
_sstack = _estack - _Min_Stack_Size;

SECTIONS
{
  /* Match the current CubeMX Tx/Rx addresses and exact generated section names.
     NOLOAD avoids a Flash load image; HAL_ETH_Init initializes the descriptors. */
  .TxDecripSection 0x2004C000 (NOLOAD) : ALIGN(32)
  {
    KEEP(*(.TxDecripSection))
  } >RAM

  .RxDecripSection 0x2004C0A0 (NOLOAD) : ALIGN(32)
  {
    KEEP(*(.RxDecripSection))
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
ASSERT(_ebss <= _sstack && _edata <= _sstack && _end + _Min_Heap_Size <= _sstack,
       "Static data or minimum heap overlaps reserved MSP/ETH memory")
ASSERT(ADDR(._user_heap_stack) + SIZEOF(._user_heap_stack) <= _estack,
       "Insufficient application RAM below ETH reservation")
