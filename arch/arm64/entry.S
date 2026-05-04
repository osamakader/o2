.section .text.boot, "ax"
.global _entry
_entry:
        # mask interrupts
        msr daifset, #0xf
        # set up a stack for C.
        ldr x0, =stack0
        mov x1, #4096
        mrs x2, MPIDR_EL1
        and x2, x2, #0xff
        add x2, x2, #1
        mul x1, x1, x2
        add sp, x0, x1
        cmp x2, #1
        b.ne _start
        // Clear BSS section
        ldr x0, =__bss_start
        ldr x1, =__bss_end
        mov x2, #0
        clear_loop:
            cmp x0, x1
            b.ge clear_done
            str x2, [x0], #8
            b clear_loop
        clear_done:
        # jump to start() in start.c
        _start:
            bl start
            b .
