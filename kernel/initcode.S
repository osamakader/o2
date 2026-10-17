// The first user program, user/_init, embedded as an ELF image until
// there is a file system to load it from.

.section .rodata
.balign 8
.global initcode
.global initcode_end
initcode:
    .incbin "user/_init"
initcode_end:
