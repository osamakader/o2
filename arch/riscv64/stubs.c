// Arch hooks the riscv64 port doesn't implement yet. Each panics, so
// the kernel boots as far as the first missing piece.

#include "types.h"
#include "console.h"
#include "vm.h"
#include "proc.h"

#define STUB(proto) proto { panic("riscv64: %s not yet", __func__); }

STUB(void uvmswitch(pagetable_t pagetable))
STUB(void usertrap(struct trapframe *tf))
STUB(void usertrapret(struct trapframe *tf))

// Secondary harts aren't started yet: run on the boot hart only.
void start_cpus(void) { }
