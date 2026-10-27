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
  /* CPU-only transactional classification tails, separate from DMA/capture. */
  SOUND_CLASS (rw) : ORIGIN = 0x20024400, LENGTH = 10K
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
  .SoundClassSection (NOLOAD) : ALIGN(32)
  {
    __sound_class_start__ = .;
    KEEP(*(.SoundClassSection))
    __sound_class_end__ = .;
  } >SOUND_CLASS
}
/* Override the generated PROVIDE values used by sysmem.c's _sbrk. This is
   deliberately outside the generated linker script. */
_end = __sound_class_end__;
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
ASSERT(ORIGIN(CAPTURE) + LENGTH(CAPTURE) <= ORIGIN(SOUND_CLASS) &&
       __chirp_capture_end__ <= __sound_class_start__,
       "Classification PCM must follow the complete capture reservation")
ASSERT(ADDR(.SoundClassSection) == 0x20024400 &&
       (ADDR(.SoundClassSection) % 32) == 0 && SIZEOF(.SoundClassSection) == 0x2580,
       "Classification PCM must be aligned and exactly 9600 bytes at 0x20024400")
ASSERT(__sound_class_end__ <= ORIGIN(SOUND_CLASS) + LENGTH(SOUND_CLASS),
       "Classification PCM exceeds its 10 KiB reservation")
ASSERT(_end >= __sound_class_end__ && _end + _Min_Heap_Size <= _sstack,
       "Runtime heap must follow classification PCM and remain below reserved stack")
