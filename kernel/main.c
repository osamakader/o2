#include <stdint.h>
#include "console.h"
#include "gicv3.h"
#include "timer.h"

static void setup_serial() {
    pl011_init(0x9000000, 24000000);
}

void irq_handler() {
    setup_serial();
    log_msg("IRQ handler\n");
    halt();
}

void fiq_handler() {
    setup_serial();
    log_msg("FIQ handler\n");
    halt();
}

void sync_handler() {
    setup_serial();
    log_msg("Synchronous handler\n");
    halt();
}

void serr_handler() {
    setup_serial();
    log_msg("SError handler\n");
    halt();
}

void check_level() {
    uint64_t el;
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

    halt();
    return 0;
}