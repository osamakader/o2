#include "types.h"
#include "console.h"
#include "gicv3.h"
#include "timer.h"
#include "trap.h"
#include "defs.h"

static void setup_serial() {
    pl011_init(0x9000000, 24000000);
}

void fiq_handler(struct trapframe *tf) {
    setup_serial();
    log_msg("FIQ handler\n");
    halt();
}

void sync_handler(struct trapframe *tf) {
    setup_serial();
    log_msg("Synchronous handler\n");
    log_msg("ESR: ");
    log_hex(tf->esr);
    log_msg("\n");
    log_msg("ELR: ");
    log_hex(tf->elr);
    log_msg("\n");
    log_msg("FAR: ");
    log_hex(tf->far);
    log_msg("\n");
    halt();
}

void serr_handler(struct trapframe *tf) {
    setup_serial();
    log_msg("SError handler\n");
    halt();
}

void check_level() {
    uint64 el;
    asm volatile("mrs %0, CurrentEL" : "=r"(el));
    el = (el >> 2) & 3;
    switch (el) {
        case 0:
            log_msg("EL0\n");
            break;
        case 1:
            log_msg("EL1\n");
            break;
        case 2:
            log_msg("EL2\n");
            break;
        case 3:
            log_msg("EL3\n");
            break;
        default:
            log_msg("Unknown EL\n");
            break;
    }
}

int main() {
    log_msg("Starting kernel ...\n");
    check_level();
    init_gicv3();
    init_timer();
    enable_irqs();
    log_msg("IRQs are enabled\n");
    kinit();

    void *p = kalloc();
    log_msg("Allocated page at: ");
    log_hex((uint64)p);
    log_msg("\n");

    halt();
    return 0;
}