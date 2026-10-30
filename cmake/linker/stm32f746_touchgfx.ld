/* User-owned CPU-only display state. Both RGB565 framebuffers end below
   0xC0080000. No startup clearing: FrontendHeap explicitly clears this state
   and placement-constructs after BSP_SDRAM_IsDisplayReady has passed.
   Cache/MPU/FMC and the existing framebuffers remain CubeMX/BSP-owned. */
MEMORY
{
  TOUCHGFX_STATE (rw) : ORIGIN = 0xC0080000, LENGTH = 8K
}
SECTIONS
{
  .TouchGFXState (NOLOAD) : ALIGN(32)
  {
    KEEP(*(.TouchGFXState))
    . = ALIGN(32);
  } >TOUCHGFX_STATE
}
ASSERT(ADDR(.TouchGFXState) == 0xC0080000 && SIZEOF(.TouchGFXState) > 0 &&
       SIZEOF(.TouchGFXState) <= 0x2000,
       "TouchGFX CPU state exceeds its independent 8 KiB SDRAM window")
ASSERT(0xC0040000 + 480 * 272 * 2 <= ADDR(.TouchGFXState),
       "TouchGFX CPU state overlaps the second RGB565 framebuffer")
