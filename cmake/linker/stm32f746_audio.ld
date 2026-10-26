/* User-owned reservation: DMA2 cannot access DTCM. A separate region avoids
   advancing the generated RAM cursor: BSS remains in fast DTCM. The runtime
   newlib heap starts above audio; it cannot grow across the reserved buffer.
   BSP_Audio_Start initializes NOLOAD storage before DMA starts. */
MEMORY
{
  AUDIO_DMA (rw) : ORIGIN = 0x20010000, LENGTH = 64K
  /* CPU-only chirp capture window in the SRAM after audio DMA; newlib heap
     (_end) follows it so the heap never grows into the reservation. */
  CAPTURE (rw) : ORIGIN = 0x20020000, LENGTH = 17K
}
SECTIONS
{
  .AudioDmaSection (NOLOAD) : ALIGN(32)
  {
    __audio_dma_start__ = .;
    KEEP(*(.AudioDmaSection))
    __audio_dma_end__ = .;
  } >AUDIO_DMA
  .ChirpCaptureSection (NOLOAD) : ALIGN(32)
  {
    __chirp_capture_start__ = .;
    KEEP(*(.ChirpCaptureSection))
    __chirp_capture_end__ = .;
  } >CAPTURE
}
/* Override the generated PROVIDE values used by sysmem.c's _sbrk. This is
   deliberately outside the generated linker script. */
_end = __chirp_capture_end__;
end = _end;
ASSERT(SIZEOF(.AudioDmaSection) == 0x10000,
       "Audio DMA buffer must be exactly 64 KiB")
ASSERT(_edata <= __audio_dma_start__ && _ebss <= __audio_dma_start__,
       "Initialized data/BSS overlaps audio DMA")
ASSERT(ADDR(._user_heap_stack) + SIZEOF(._user_heap_stack) <= __audio_dma_start__,
       "Generated minimum heap/stack section overlaps audio DMA")
ASSERT(__audio_dma_end__ <= __chirp_capture_start__,
       "Chirp capture reservation must follow the audio DMA buffer")
ASSERT(__chirp_capture_end__ - __chirp_capture_start__ == 0x42C0,
       "Chirp capture buffer must be exactly 17 088 bytes")
ASSERT(_end >= __audio_dma_end__ && _end + _Min_Heap_Size <= _sstack,
       "Runtime heap must follow audio DMA and remain below reserved stack")
