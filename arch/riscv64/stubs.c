// Arch hooks the riscv64 port doesn't implement yet. Each panics, so
// the kernel boots as far as the first missing piece.

#include "types.h"
#include "console.h"
#include "vm.h"
#include "proc.h"

#define STUB(proto) proto { panic("riscv64: %s not yet", __func__); }

STUB(void intc_init(void))
STUB(void intc_inithart(void))
STUB(void intc_enable(uint32 irq, int cpu))
STUB(void timerinithart(void))
STUB(void start_cpus(void))
STUB(void uvmswitch(pagetable_t pagetable))
STUB(void sync_icache(uint64 va, uint64 len))
STUB(void uvminit_root(uint64 *root))
STUB(void swtch(struct context *old, struct context *new))
STUB(void kthread_trampoline(void))
STUB(void usertrap(struct trapframe *tf))
STUB(void usertrapret(struct trapframe *tf))
