// PSCI (Power State Coordination Interface) calls.
//
// qemu virt without EL2/EL3 firmware implements PSCI itself and uses
// hvc as the conduit; with firmware it is smc. The device tree's /psci
// node says which (board.psci_smc).

#include "types.h"
#include "psci.h"
#include "param.h"
#include "memlayout.h"
#include "defs.h"
#include "console.h"
#include "board.h"
#include "cpu.h"

#define PSCI_CPU_ON_64 0xC4000003

// SMCCC: function id and arguments in x0-x3, result in x0;
// x4-x17 may be clobbered.
static long psci_call(uint64 fn, uint64 a1, uint64 a2, uint64 a3)
{
    register uint64 x0 asm("x0") = fn;
    register uint64 x1 asm("x1") = a1;
    register uint64 x2 asm("x2") = a2;
    register uint64 x3 asm("x3") = a3;

    if (board.psci_smc)
        asm volatile("smc #0"
                     : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
                     :
                     : "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11",
                       "x12", "x13", "x14", "x15", "x16", "x17", "memory");
    else
        asm volatile("hvc #0"
                     : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
                     :
                     : "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11",
                       "x12", "x13", "x14", "x15", "x16", "x17", "memory");
    return (long)x0;
}

// Power on the CPU with affinity mpidr and start it at the physical
// address entry with the MMU off. Returns a PSCI status (0 = success).
long psci_cpu_on(uint64 mpidr, uint64 entry)
{
    return psci_call(PSCI_CPU_ON_64, mpidr, entry, 0);
}

// Start the other CPUs. Each enters at the physical _entry with the
// MMU off and comes up through entry.S into main(). Without a device
// tree, through PSCI, which rejects the first MPIDR that has no CPU
// behind it.
void start_cpus(void)
{
    extern char _entry[];

    if (board.ncpu == 0) {
        // no device tree: until PSCI rejects an MPIDR
        for (int i = 1; i < NCPU; i++) {
            long r = psci_cpu_on(i, V2P(_entry));
            if (r == PSCI_INVALID_PARAMETERS)
                break;
            if (r != PSCI_SUCCESS)
                printf("cpu %d: psci_cpu_on failed: %ld\n", i, r);
        }
        return;
    }
    // the CPUs the device tree lists, through PSCI or the spin table
    // the firmware keeps them in. Their index (cpuid(): Aff0 + Aff1)
    // must be below NCPU.
    for (int i = 0; i < board.ncpu && i < NCPU; i++) {
        uint64 mpidr = board.cpu[i];
        int id = (mpidr & 0xff) + ((mpidr >> 8) & 0xff);
        if (id == cpuid())
            continue;
        if (id >= NCPU) {
            printf("cpu %p: index too large (NCPU %d), not started\n", (void *)mpidr, NCPU);
            continue;
        }
        if (board.cpu_release[i]) {
            // it waits (wfe, MMU off) for an address there: write it
            // through to memory and wake it
            volatile uint64 *rel = (uint64 *)P2V(board.cpu_release[i]);
            *rel = V2P(_entry);
            asm volatile("dc civac, %0; dsb sy; sev" : : "r"(rel) : "memory");
            continue;
        }
        long r = psci_cpu_on(mpidr, V2P(_entry));
        if (r != PSCI_SUCCESS)
            printf("cpu %p: psci_cpu_on failed: %ld\n", (void *)mpidr, r);
    }
}
