/* nRF51802 (256 KB flash / 16 KB RAM) MINIMAL standalone linker script.
 *
 * Used for the clock/GPIO/UART self-test: runs at 0x0 with NO SoftDevice,
 * so the whole flash and RAM belong to us.
 *   0x00000 - 0x3FFFF  minimal image (256 KB)
 *   0x20000000 - 0x20003FFF  RAM (16 KB, stack at the top)
 */
MEMORY
{
  FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 0x00040000
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 0x00004000
}

ENTRY(Reset_Handler)

SECTIONS
{
  .vector_table : ALIGN(4)
  {
    KEEP(*(.vector_table))
  } > FLASH

  .text : ALIGN(4)
  {
    *(.text*)
    *(.rodata*)
  } > FLASH

  .data : ALIGN(4)
  {
    __data_start__ = .;
    *(.data*)
    __data_end__ = .;
  } > RAM AT> FLASH

  .bss : ALIGN(4)
  {
    __bss_start__ = .;
    *(.bss*)
    *(COMMON)
    __bss_end__ = .;
  } > RAM

  /DISCARD/ :
  {
    *(.note*)
    *(.comment*)
    *(.eh_frame*)
  }
}

PROVIDE(__etext = .);
