// PSCI (Power State Coordination Interface) calls.
//
// qemu virt without EL2/EL3 firmware implements PSCI itself and uses
// hvc as the conduit; with firmware this would be smc.

#include "types.h"
#include "psci.h"

#define PSCI_CPU_ON_64 0xC4000003

// SMCCC: function id and arguments in x0-x3, result in x0;
// x4-x17 may be clobbered.
static long psci_call(uint64 fn, uint64 a1, uint64 a2, uint64 a3)
{
    register uint64 x0 asm("x0") = fn;
    register uint64 x1 asm("x1") = a1;
    register uint64 x2 asm("x2") = a2;
    register uint64 x3 asm("x3") = a3;

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
