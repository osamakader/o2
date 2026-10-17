// SBI calls: extension id in a7, function id in a6, arguments in
// a0-a2; error comes back in a0, value in a1.

#include "types.h"
#include "sbi.h"

#define SBI_EXT_TIME 0x54494D45   // "TIME"
#define SBI_EXT_HSM  0x48534D     // "HSM"

struct sbiret {
    long error;
    long value;
};

static struct sbiret sbi_call(long ext, long fid, uint64 arg0, uint64 arg1, uint64 arg2)
{
    register uint64 a0 asm("a0") = arg0;
    register uint64 a1 asm("a1") = arg1;
    register uint64 a2 asm("a2") = arg2;
    register uint64 a6 asm("a6") = fid;
    register uint64 a7 asm("a7") = ext;

    asm volatile("ecall"
                 : "+r"(a0), "+r"(a1)
                 : "r"(a2), "r"(a6), "r"(a7)
                 : "memory");
    return (struct sbiret){ .error = a0, .value = a1 };
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
