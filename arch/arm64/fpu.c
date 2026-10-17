// Lazy user FP/SIMD state on arm64. CPACR_EL1.FPEN = 01 lets EL1 use
// FP but traps EL0 (exception class 0x07); 11 lets both. FPEN = 11 on
// a CPU means the running process's FP registers are live there.

#include "types.h"
#include "param.h"
#include "proc.h"
#include "defs.h"
#include "spinlock.h"
#include "fpu.h"

#define CPACR_FPEN_MASK (3UL << 20)
#define FPEN_TRAP_EL0   (1UL << 20)
#define FPEN_NONE       (3UL << 20)

void fp_save(struct fpstate *s);
void fp_load(const struct fpstate *s);

static void set_fpen(uint64 fpen) {
    uint64 cpacr;
    asm volatile("mrs %0, cpacr_el1" : "=r"(cpacr));
    cpacr = (cpacr & ~CPACR_FPEN_MASK) | fpen;
    asm volatile("msr cpacr_el1, %0; isb" : : "r"(cpacr) : "memory");
}

// Are the running process's FP registers live on this CPU?
static int fp_live(void) {
    uint64 cpacr;
    asm volatile("mrs %0, cpacr_el1" : "=r"(cpacr));
    return (cpacr & CPACR_FPEN_MASK) == FPEN_NONE;
}

void fpinithart(void) {
    set_fpen(FPEN_TRAP_EL0);
}

// p is leaving this CPU (sched(), interrupts off): save its live FP
// registers and trap the next user FP access, whoever it is.
void fp_switch_out(struct proc *p) {
    if (fp_live()) {
        fp_save(&p->fp);
        set_fpen(FPEN_TRAP_EL0);
    }
}

// fork(): the child starts with the parent's FP registers.
void fp_fork(struct proc *parent, struct proc *child) {
    push_off();   // no preemption between checking and saving
    if (fp_live())
        fp_save(&parent->fp);
    pop_off();
    child->fp = parent->fp;
}

// exec(): the new program starts with zeroed FP registers.
void fp_exec(struct proc *p) {
    memset(&p->fp, 0, sizeof(p->fp));
    if (p == myproc()) {
        push_off();
        if (fp_live())
            set_fpen(FPEN_TRAP_EL0);
        pop_off();
    }
}

// EL0 FP/SIMD access trapped (usertrap(), interrupts off): load p's FP
// registers and let it use them; the instruction is retried.
void fp_trap(struct proc *p) {
    fp_load(&p->fp);
    set_fpen(FPEN_NONE);
}
