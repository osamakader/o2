OUTPUT_ARCH(aarch64)
ENTRY( _start )

/* User programs run at virtual address 0. */
PHDRS
{
  text PT_LOAD FLAGS(5);   /* R-X */
  data PT_LOAD FLAGS(6);   /* RW- */
}

SECTIONS
{
  . = 0x0;

  .text : {
    /* no function at 0 or 1, which signal() takes for SIG_DFL and
       SIG_IGN: start with 16 bytes of zeros (not an instruction, on
       either arch) */
    . += 16;
    *(.text .text.*)
  } :text

  .rodata : {
    . = ALIGN(16);
    *(.srodata .srodata.*)
    . = ALIGN(16);
    *(.rodata .rodata.*)
  } :text

  . = ALIGN(0x1000);

  .data : {
    . = ALIGN(16);
    *(.sdata .sdata.*)
    . = ALIGN(16);
    *(.data .data.*)
  } :data

  .bss : {
    . = ALIGN(16);
    *(.sbss .sbss.*)
    *(.bss .bss.*)
    *(COMMON)
  } :data

  PROVIDE(end = .);
}
