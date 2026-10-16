OUTPUT_ARCH(aarch64)
ENTRY( _entry )

SECTIONS
{
  /*
   * ensure that entry.S / _entry is at 0x40080000,
   * where qemu's -kernel jumps.
   */
  . = 0x40080000;

  .text : {
    PROVIDE(stext = .);
    KEEP(*(.text.boot))
    *(.text .text.*)
    /* VBAR_EL1 requires 2KB alignment; keep the vectors with the
     * rest of the code so everything below etext can be mapped RX. */
    . = ALIGN(2048);
    KEEP(*(.vectors .vectors.*))
    . = ALIGN(0x1000);
    PROVIDE(etext = .);
  }

  .rodata : {
    . = ALIGN(16);
    *(.srodata .srodata.*) /* do not need to distinguish this from .rodata */
    . = ALIGN(16);
    *(.rodata .rodata.*)
  }

  .data : {
    . = ALIGN(16);
    *(.sdata .sdata.*) /* do not need to distinguish this from .data */
    . = ALIGN(16);
    *(.data .data.*)
  }

  .stack (NOLOAD) : {
    . = ALIGN(16);
    __stack_start = .;
    KEEP(*(.stack))
    . = ALIGN(16);
    __stack_end = .;
  }

  .bss : {
    . = ALIGN(16);
    __bss_start = .;
    *(.sbss .sbss.*)
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(16);
    __bss_end = .;
  }

  PROVIDE(end = .);
}
