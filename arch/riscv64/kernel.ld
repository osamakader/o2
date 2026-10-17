OUTPUT_ARCH(riscv)

/* must match KVA_OFFSET in kernel/include/memlayout.h */
KVA_OFFSET = 0xFFFFFFC000000000;

/* OpenSBI jumps to the physical entry point with the MMU off */
ENTRY( _entry_phys )

/* separate load segments so none is writable and executable */
PHDRS
{
  text   PT_LOAD FLAGS(5);   /* R-X */
  rodata PT_LOAD FLAGS(4);   /* R-- */
  data   PT_LOAD FLAGS(6);   /* RW- */
}

SECTIONS
{
  /*
   * The kernel runs in the high half at physical + KVA_OFFSET but is
   * loaded at 0x80200000, after OpenSBI.
   */
  . = 0x80200000 + KVA_OFFSET;
  _entry_phys = _entry - KVA_OFFSET;

  .text : AT(ADDR(.text) - KVA_OFFSET) {
    PROVIDE(stext = .);
    KEEP(*(.text.boot))
    *(.text .text.*)
    /* the trampoline: exactly one page, mapped into user page tables */
    . = ALIGN(0x1000);
    _trampoline = .;
    KEEP(*(trampsec))
    . = ALIGN(0x1000);
    ASSERT(. - _trampoline == 0x1000, "error: trampoline larger than one page");
    PROVIDE(etext = .);
  } :text

  .rodata : AT(ADDR(.rodata) - KVA_OFFSET) {
    . = ALIGN(16);
    *(.srodata .srodata.*)
    . = ALIGN(16);
    *(.rodata .rodata.*)
  } :rodata

  /* data starts on its own page, apart from read-only data */
  . = ALIGN(0x1000);
  PROVIDE(sdata = .);

  .data : AT(ADDR(.data) - KVA_OFFSET) {
    . = ALIGN(16);
    *(.sdata .sdata.*)
    . = ALIGN(16);
    *(.data .data.*)
  } :data

  .stack (NOLOAD) : AT(ADDR(.stack) - KVA_OFFSET) {
    . = ALIGN(16);
    KEEP(*(.stack))
    . = ALIGN(16);
  }

  .bss : AT(ADDR(.bss) - KVA_OFFSET) {
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
