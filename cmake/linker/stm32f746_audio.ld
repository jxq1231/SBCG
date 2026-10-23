/* User-owned reservation: DMA2 cannot access DTCM. A separate region avoids
   advancing the generated RAM cursor: BSS remains in fast DTCM. The runtime
   newlib heap starts above audio; it cannot grow across the reserved buffer.
   BSP_Audio_Start initializes NOLOAD storage before DMA starts. */
MEMORY
{
  AUDIO_DMA (rw) : ORIGIN = 0x20010000, LENGTH = 64K
}
SECTIONS
{
  .AudioDmaSection (NOLOAD) : ALIGN(32)
  {
    __audio_dma_start__ = .;
    KEEP(*(.AudioDmaSection))
    __audio_dma_end__ = .;
  } >AUDIO_DMA
}
/* Override the generated PROVIDE values used by sysmem.c's _sbrk. This is
   deliberately outside the generated linker script. */
_end = __audio_dma_end__;
end = _end;
ASSERT(SIZEOF(.AudioDmaSection) == 0x10000,
       "Audio DMA buffer must be exactly 64 KiB")
ASSERT(_edata <= __audio_dma_start__ && _ebss <= __audio_dma_start__,
       "Initialized data/BSS overlaps audio DMA")
ASSERT(ADDR(._user_heap_stack) + SIZEOF(._user_heap_stack) <= __audio_dma_start__,
       "Generated minimum heap/stack section overlaps audio DMA")
ASSERT(_end >= __audio_dma_end__ && _end + _Min_Heap_Size <= _sstack,
       "Runtime heap must follow audio DMA and remain below reserved stack")
