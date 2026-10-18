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
#include "fpu.h"
#include "board.h"

// Every CPU enters here from arch/<arch>/entry.S with the MMU on, on its
// boot stack. boot is set on the one CPU that does the global setup
// and then starts the others; it gets the device tree's physical
// address in dtb, or 0.
int main(int boot, uint64 dtb) {
    if (boot) {
        board_init(dtb);
        uartinit();
        printf("cpu %d: starting kernel ...\n", cpuid());
        board_report();

        kinit();

        void *p = kalloc();
        printf("Allocated page at: %p\n", p);

        kfree(p);
        log_msg("Freed page\n");

        procinit();
        kvminit();
        kvminithart();
        fpinithart();
        log_msg("Kernel page table installed\n");

        intc_init();
        intc_inithart();

        // console input: UART receive interrupts, handled on this CPU
        uart_enable_rx_irq();
        irq_register(board.uart_irq, uartintr, cpuid());

        virtio_disk_init();
        binit();
        iinit();
        fileinit();
        consoleinit();
        timeinit();

        userinit();   // first, so init is pid 1
#ifdef SELFTEST
        selftest();   // before any CPU schedules: init hasn't run yet
#endif

        timerinithart();

        start_cpus();
    } else {
        kvminithart();
        fpinithart();
        intc_inithart();
        timerinithart();
        printf("cpu %d starting\n", cpuid());
    }

    scheduler();
}
