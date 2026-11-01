/* CPU-only PCM history; existing SDRAM MPU/cache configuration is unchanged. */
MEMORY
{
  WAVEFORM_STATE (rw) : ORIGIN = 0xC00A0000, LENGTH = 128K
}
SECTIONS
{
  .WaveformState (NOLOAD) : ALIGN(32)
  {
    __waveform_start__ = .;
    *(.WaveformState .WaveformState.*)
    . = ALIGN(32);
    __waveform_end__ = .;
  } > WAVEFORM_STATE
}
INSERT AFTER .TouchGFXState;
ASSERT(SIZEOF(.WaveformState) <= LENGTH(WAVEFORM_STATE), "Waveform CPU state exceeds SDRAM reservation")
