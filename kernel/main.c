#include "types.h"
#include "memlayout.h"
#include "proc.h"
#include "console.h"
#include "intc.h"
#include "timer.h"
#include "defs.h"
#include "vm.h"
#include "param.h"
#include "cpu.h"
#include "uart.h"

// Every CPU enters here from arch/<arch>/entry.S with the MMU on, on its
// boot stack. boot is set on the one CPU that does the global setup
// and then starts the others.
int main(int boot) {
    if (boot) {
        uartinit();
        printf("cpu %d: starting kernel ...\n", cpuid());

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

        // console input: UART receive interrupts, handled on this CPU
        uart_enable_rx_irq();
        intc_enable(UART0_IRQ, cpuid());

        userinit();   // first, so init is pid 1
        selftest();   // before any CPU schedules: init hasn't run yet

        timerinithart();

        start_cpus();
    } else {
        kvminithart();
        intc_inithart();
        timerinithart();
        printf("cpu %d starting\n", cpuid());
    }

    scheduler();
}
