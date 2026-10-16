#include "types.h"
#include "memlayout.h"
#include "proc.h"
#include "console.h"
#include "gicv3.h"
#include "timer.h"
#include "trap.h"
#include "defs.h"
#include "vm.h"
#include "psci.h"
#include "param.h"
#include "cpu.h"

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
        printf("%s iteration %d on cpu %d\n", name, i, cpuid());
        yield();
    }
    printf("%s exiting\n", name);
}

// Busy-waits without ever calling yield(), so it only makes progress
// alongside another spinner if the timer preempts it.
static void kthread_spin(void *arg) {
    const char *name = arg;
    for (int i = 0; i < 3; i++) {
        uint64 until = *(volatile uint64 *)&ticks + 10;
        while (*(volatile uint64 *)&ticks < until)
            ;
        printf("%s spin %d on cpu %d\n", name, i, cpuid());
    }
}

// Power on the other CPUs. Each enters at the physical _entry with the
// MMU off and comes up through entry.S into main(). PSCI rejects the
// first MPIDR that has no CPU behind it.
static void start_secondaries(void) {
    extern char _entry[];

    for (int i = 1; i < NCPU; i++) {
        long r = psci_cpu_on(i, V2P(_entry));
        if (r == PSCI_INVALID_PARAMETERS)
            break;
        if (r != PSCI_SUCCESS)
            printf("cpu %d: psci_cpu_on failed: %ld\n", i, r);
    }
}

int main() {
    if (cpuid() == 0) {
        log_msg("Starting kernel ...\n");
        check_level();

        kinit();

        void *p = kalloc();
        printf("Allocated page at: %p\n", p);

        kfree(p);
        log_msg("Freed page\n");

        procinit();
        kvminit();
        kvminithart();
        log_msg("Kernel page table installed\n");

        gic_init();
        gic_inithart();

        kthread_create(kthread_test, "A");
        kthread_create(kthread_test, "B");
        kthread_create(kthread_spin, "C");
        kthread_create(kthread_spin, "D");

        init_timer();

        start_secondaries();
    } else {
        kvminithart();
        gic_inithart();
        init_timer();
        printf("cpu %d starting\n", cpuid());
    }

    scheduler();
}
