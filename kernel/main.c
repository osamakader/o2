#include <stdint.h>
#include "uart.h"

static struct pl011 serial;

static void setup_serial() {
    pl011_setup(
        &serial, 
        /* base_address = */0x9000000, 
        /* base_clock = */24000000);
}

void halt() {
    while (1) {
        asm volatile("wfi");
    }
}

void irq_handler() {
    setup_serial();
    pl011_send(&serial, "IRQ handler\n");
    halt();
}

void fiq_handler() {
    setup_serial();
    pl011_send(&serial, "FIQ handler\n");
    halt();
}

void sync_handler() {
    setup_serial();
    pl011_send(&serial, "Synchronous handler\n");
    halt();
}

void serr_handler() {
    setup_serial();
    pl011_send(&serial, "SError handler\n");
    halt();
}

int main() {
    setup_serial();
    pl011_send(&serial, "Hello, from main!\n");
    halt();
    return 0;
}