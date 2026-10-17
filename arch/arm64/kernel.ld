OUTPUT_ARCH(aarch64)

/* must match KVA_OFFSET in kernel/include/memlayout.h */
KVA_OFFSET = 0xFFFFFFC000000000;

/* qemu jumps to the physical entry point with the MMU off */
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
   * loaded at 0x40080000, where qemu's -kernel puts it.
   */
  . = 0x40080000 + KVA_OFFSET;
  _entry_phys = _entry - KVA_OFFSET;

  .text : AT(ADDR(.text) - KVA_OFFSET) {
    PROVIDE(stext = .);
    KEEP(*(.text.boot))
    *(.text .text.*)
    /* VBAR_EL1 requires 2KB alignment; keep the vectors with the
     * rest of the code so everything below etext can be mapped RX. */
    . = ALIGN(2048);
    KEEP(*(.vectors .vectors.*))
    /* the trampoline: exactly one page, the only kernel text mapped
     * while user code runs */
    . = ALIGN(0x1000);
    _trampoline = .;
    KEEP(*(trampsec))
    . = ALIGN(0x1000);
    ASSERT(. - _trampoline == 0x1000, "error: trampoline larger than one page");
    PROVIDE(etext = .);
  } :text

  .rodata : AT(ADDR(.rodata) - KVA_OFFSET) {
    . = ALIGN(16);
    *(.srodata .srodata.*) /* do not need to distinguish this from .rodata */
    . = ALIGN(16);
    *(.rodata .rodata.*)
  } :rodata

  /* data starts on its own page, apart from read-only data */
  . = ALIGN(0x1000);
  PROVIDE(sdata = .);

  .data : AT(ADDR(.data) - KVA_OFFSET) {
    . = ALIGN(16);
    *(.sdata .sdata.*) /* do not need to distinguish this from .data */
    . = ALIGN(16);
    *(.data .data.*)
  } :data

  .stack (NOLOAD) : AT(ADDR(.stack) - KVA_OFFSET) {
    . = ALIGN(16);
    __stack_start = .;
    KEEP(*(.stack))
    . = ALIGN(16);
    __stack_end = .;
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
