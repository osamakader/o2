// The first user program, user/_initcode, embedded as an ELF image:
// it execs /init from the file system. The Makefile passes its path in
// the build directory as INITCODE.

.section .rodata
.balign 8
.global initcode
.global initcode_end
initcode:
    .incbin INITCODE
initcode_end:
