
.global swtch

swtch:
    mov x9, sp
    stp x30,x9,[x0]
    stp x19,x20,[x0, #16]
    stp x21,x22,[x0, #32]
    stp x23,x24,[x0, #48]
    stp x25,x26,[x0, #64]
    stp x27,x28,[x0, #80]
    str x29,[x0, #96]

    ldp x30,x9,[x1]
    ldp x19,x20,[x1, #16]
    ldp x21,x22,[x1, #32]
    ldp x23,x24,[x1, #48]
    ldp x25,x26,[x1, #64]
    ldp x27,x28,[x1, #80]
    ldr x29,[x1, #96]
    mov sp, x9
    ret