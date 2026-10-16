#include "types.h"
#include "memlayout.h"
#include "proc.h"
#include "console.h"
#include "gicv3.h"
#include "timer.h"
#include "trap.h"
#include "defs.h"
#include "vm.h"

static void setup_serial() {
    pl011_init(P2V(UART0), 24000000);
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

// Two threads that take turns via yield() to exercise the scheduler.
static void kthread_test(void *arg) {
    const char *name = arg;
    for (int i = 0; i < 3; i++) {
        log_msg(name);
        log_msg(" iteration ");
        log_uint(i);
        log_msg("\n");
        yield();
    }
    log_msg(name);
    log_msg(" exiting\n");
}

// Busy-waits without ever calling yield(), so it only makes progress
// alongside another spinner if the timer preempts it.
static void kthread_spin(void *arg) {
    const char *name = arg;
    for (int i = 0; i < 3; i++) {
        uint64 until = *(volatile uint64 *)&ticks + 10;
        while (*(volatile uint64 *)&ticks < until)
            ;
        log_msg(name);
        log_msg(" spin ");
        log_uint(i);
        log_msg("\n");
    }
}

int main() {
    log_msg("Starting kernel ...\n");
    check_level();

    kinit();
    
    void *p = kalloc();
    log_msg("Allocated page at: ");
    log_hex((uint64)p);
    log_msg("\n");

    kfree(p);
    log_msg("Freed page\n");

    procinit();
    kvminit();
    kvminithart();
    log_msg("Kernel page table installed\n");

    init_gicv3();

    kthread_create(kthread_test, "A");
    kthread_create(kthread_test, "B");
    kthread_create(kthread_spin, "C");
    kthread_create(kthread_spin, "D");

    init_timer();
    enable_irqs();
    log_msg("IRQs are enabled\n");

    scheduler();
}
