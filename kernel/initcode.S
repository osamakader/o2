// The first user program, user/_init, embedded as an ELF image until
// there is a file system to load it from. The Makefile passes its
// path in the build directory as INITCODE.

.section .rodata
.balign 8
.global initcode
.global initcode_end
initcode:
    .incbin INITCODE
initcode_end:
