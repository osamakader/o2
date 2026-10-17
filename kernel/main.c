#include "types.h"
#include "memlayout.h"
#include "proc.h"
#include "console.h"
#include "intc.h"
#include "timer.h"
#include "trap.h"
#include "defs.h"
#include "vm.h"
#include "psci.h"
#include "param.h"
#include "cpu.h"
#include "uart.h"

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

        intc_init();
        intc_inithart();

        // console input: UART receive interrupts, handled on CPU 0
        uart_enable_rx_irq();
        intc_enable(UART0_IRQ, 0);

        userinit();   // first, so init is pid 1
        selftest();   // before any CPU schedules: init hasn't run yet

        timerinithart();

        start_secondaries();
    } else {
        kvminithart();
        intc_inithart();
        timerinithart();
        printf("cpu %d starting\n", cpuid());
    }

    scheduler();
}
