// Kernel backtraces: walk the frame-pointer chain (the kernel is built
// with -fno-omit-frame-pointer) and name each return address from the
// kernel's symbol table, which the build generates and links into the
// kernel itself (Makefile: ksyms.S).

#include "types.h"
#include "memlayout.h"
#include "console.h"
#include "vm.h"
#include "mmu.h"
#include "cpu.h"
#include "backtrace.h"

#define MAXDEPTH 24

// The function containing pc and the offset into it, or 0 if pc isn't
// in the table's range.
const char *ksym_lookup(uint64 pc, uint64 *off)
{
    int lo = 0, hi = nksyms - 1, best = -1;

    if (nksyms == 0 || pc < ksyms[0].addr)
        return 0;
    // last symbol with addr <= pc
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (ksyms[mid].addr <= pc) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    extern char etext[];  // kernel.ld: end of kernel text
    if (best < 0 || pc >= (uint64)etext)
        return 0;
    *off = pc - ksyms[best].addr;
    return ksyms[best].name;
}

// Print one backtrace line for pc. A return address is looked up at
// pc - 1, the call instruction: after a call to a noreturn function
// (panic), it can be the first byte of the next function.
static void print_pc(int i, uint64 pc, int is_return)
{
    uint64 off;
    const char *name = ksym_lookup(pc - is_return, &off);
    if (name)
        printf("  #%d %p %s+0x%lx\n", i, (void *)pc, name, off + is_return);
    else
        printf("  #%d %p\n", i, (void *)pc);
}

// Can we read 8 bytes at kernel address va without faulting?
static int readable(uint64 va)
{
    if (kernel_pagetable == 0 || (va & 7) || !VA_VALID(va) || va < KVA_OFFSET)
        return 0;
    pte_t *pte = walk(kernel_pagetable, va, 0);
    return pte && (*pte & PTE_VALID);
}

// Is pc an address in kernel text?
static int in_text(uint64 pc)
{
    extern char stext[], etext[];  // kernel.ld
    return pc >= (uint64)stext && pc < (uint64)etext;
}

// Print the call chain: pc (if non-zero), the function whose frame is
// fp, then its callers. Stops at a zero or unreadable frame pointer,
// or a return address outside kernel text (e.g. into user code).
void backtrace_from(uint64 fp, uint64 pc)
{
    int i = 0;

    printf("backtrace:\n");
    if (pc)
        print_pc(i++, pc, 0);
    for (; i < MAXDEPTH && fp != 0; i++) {
        if (!readable(FRAME_FP_SLOT(fp)) || !readable(FRAME_RA_SLOT(fp)))
            break;
        uint64 next = *(uint64 *)FRAME_FP_SLOT(fp);
        uint64 ra = *(uint64 *)FRAME_RA_SLOT(fp);
        if (!in_text(ra - 1))
            break;
        print_pc(i, ra, 1);
        fp = next;
    }
}

// Backtrace of backtrace()'s caller and up.
void backtrace(void)
{
    backtrace_from((uint64)__builtin_frame_address(0), 0);
}
