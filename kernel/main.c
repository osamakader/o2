#include "types.h"
#include "memlayout.h"
#include "proc.h"
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

void proc0_main() {
    log_msg("Proc0 main\n");
}

void proc1_main(void *arg) {
    log_msg("Proc1 main\n");
    yield();
    log_msg("Proc1 resumed, exiting\n");
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

    init_gicv3();

    log_msg("Testing context switching\n");

    struct proc *proc1 = kthread_create(proc1_main, 0);
    sched(proc1);
    sched(proc1);
    if (proc1->state != UNUSED)
        panic("proc1 not reaped");
    log_msg("Context switching test completed\n");

    init_timer();
    enable_irqs();
    log_msg("IRQs are enabled\n");

    halt();
    return 0;
}
