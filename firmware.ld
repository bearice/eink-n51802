/* nRF51802 (256 KB flash / 16 KB RAM) application linker script.
 *
 * Flash layout (matches the S110 8.0.0 Keil pack armgcc linker script):
 *   0x00000 - 0x17FFF  S110 SoftDevice (96 KB)
 *   0x18000 - 0x3FFFF  application (160 KB)
 * RAM layout:
 *   0x20000000 - 0x20001FFF  SoftDevice RAM (8 KB reserved)
 *   0x20002000 - 0x20003FFF  application RAM (8 KB, stack at the top)
 */
MEMORY
{
  FLASH (rx)  : ORIGIN = 0x00018000, LENGTH = 0x00028000
  RAM   (rwx) : ORIGIN = 0x20002000, LENGTH = 0x00002000
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
