// SBI calls: extension id in a7, function id in a6, arguments in
// a0-a2; error comes back in a0, value in a1.

#include "types.h"
#include "sbi.h"
#include "param.h"
#include "memlayout.h"
#include "defs.h"
#include "console.h"
#include "board.h"
#include "cpu.h"

#define SBI_EXT_TIME 0x54494D45   // "TIME"
#define SBI_EXT_HSM  0x48534D     // "HSM"
#define SBI_EXT_RFNC 0x52464E43   // "RFNC"
#define SBI_EXT_SRST 0x53525354   // "SRST"

struct sbiret {
    long error;
    long value;
};

static struct sbiret sbi_call5(long ext, long fid, uint64 arg0, uint64 arg1,
                               uint64 arg2, uint64 arg3, uint64 arg4)
{
    register uint64 a0 asm("a0") = arg0;
    register uint64 a1 asm("a1") = arg1;
    register uint64 a2 asm("a2") = arg2;
    register uint64 a3 asm("a3") = arg3;
    register uint64 a4 asm("a4") = arg4;
    register uint64 a6 asm("a6") = fid;
    register uint64 a7 asm("a7") = ext;

    asm volatile("ecall"
                 : "+r"(a0), "+r"(a1)
                 : "r"(a2), "r"(a3), "r"(a4), "r"(a6), "r"(a7)
                 : "memory");
    return (struct sbiret){ .error = a0, .value = a1 };
}

static struct sbiret sbi_call(long ext, long fid, uint64 arg0, uint64 arg1, uint64 arg2)
{
    return sbi_call5(ext, fid, arg0, arg1, arg2, 0, 0);
}

// Program this hart's next timer interrupt for time value stime; also
// clears the pending timer interrupt.
void sbi_set_timer(uint64 stime)
{
    sbi_call(SBI_EXT_TIME, 0, stime, 0, 0);
}

// Start a stopped hart at the physical address start_addr in S-mode
// with the MMU off, a0 = hartid, a1 = opaque.
long sbi_hart_start(uint64 hartid, uint64 start_addr, uint64 opaque)
{
    return sbi_call(SBI_EXT_HSM, 0, hartid, start_addr, opaque).error;
}

// Start the other harts. Each enters at the physical _entry_secondary
// with the MMU off and comes up through entry.S into main(0). Under
// OpenSBI the boot hart can be any of them, so try every id but our
// own; ids with no hart behind them are rejected.
void start_cpus(void)
{
    extern char _entry_secondary[];

    if (board.ncpu == 0) {
        // no device tree: try every hart id
        for (int i = 0; i < NCPU; i++) {
            if (i == cpuid())
                continue;
            long r = sbi_hart_start(i, V2P(_entry_secondary), 0);
            if (r != SBI_SUCCESS && r != SBI_ERR_INVALID_PARAM)
                printf("cpu %d: sbi_hart_start failed: %ld\n", i, r);
        }
        return;
    }
    // the harts the device tree lists. cpuid() is the hart id, which
    // must be below NCPU.
    for (int i = 0; i < board.ncpu && i < NCPU; i++) {
        uint64 hart = board.cpu[i];
        if (hart == cpuid())
            continue;
        if (hart >= NCPU) {
            printf("hart %lu: id too large (NCPU %d), not started\n", hart, NCPU);
            continue;
        }
        long r = sbi_hart_start(hart, V2P(_entry_secondary), 0);
        if (r != SBI_SUCCESS)
            printf("hart %lu: sbi_hart_start failed: %ld\n", hart, r);
    }
}

// sfence.vma for every address in ASID asid, on all harts (this one
// included): RFENCE remote_sfence_vma_asid with hart_mask_base = -1.
long sbi_remote_sfence_vma_asid_all(uint64 asid)
{
    return sbi_call5(SBI_EXT_RFNC, 2, 0, -1UL, 0, -1UL, asid).error;
}

// Power off, or restart (cold); returns only if the SBI can't.
void arch_power(int reboot)
{
    sbi_call(SBI_EXT_SRST, 0, reboot ? 1 : 0, 0, 0);
}
