// Lazy user FP state on riscv64, with sstatus.FS: Off makes FP
// instructions trap (illegal instruction); Clean/Dirty enable them, and
// the hardware sets Dirty when an FP register is written. FS != Off on
// a hart means the running process's FP registers are live there.

#include "types.h"
#include "param.h"
#include "proc.h"
#include "defs.h"
#include "spinlock.h"
#include "cpu.h"
#include "fpu.h"

#define FS_MASK  (3UL << 13)
#define FS_OFF   (0UL << 13)
#define FS_CLEAN (2UL << 13)
#define FS_DIRTY (3UL << 13)

void fp_save(struct fpstate *s);
void fp_load(const struct fpstate *s);

static uint64 fs(void) {
    return csr_read(sstatus) & FS_MASK;
}

static void set_fs(uint64 v) {
    csr_clear(sstatus, FS_MASK);
    csr_set(sstatus, v);
}

void fpinithart(void) {
    set_fs(FS_OFF);
}

// p is leaving this hart (sched(), interrupts off): save its FP
// registers if they changed since loaded, and turn FP off.
void fp_switch_out(struct proc *p) {
    if (fs() == FS_DIRTY)
        fp_save(&p->fp);
    if (fs() != FS_OFF)
        set_fs(FS_OFF);
}

// Save the current process p's FP registers into p->fp if they changed
// since loaded; they stay live.
void fp_sync(struct proc *p) {
    push_off();   // no preemption between checking and saving
    if (fs() == FS_DIRTY) {
        fp_save(&p->fp);
        set_fs(FS_CLEAN);
    }
    pop_off();
}

// p->fp has new contents: if p is running here, turn FP off so the next
// user FP instruction traps and loads p->fp.
void fp_reload(struct proc *p) {
    if (p == myproc()) {
        push_off();
        set_fs(FS_OFF);
        pop_off();
    }
}

// fork(): the child starts with the parent's FP registers.
void fp_fork(struct proc *parent, struct proc *child) {
    fp_sync(parent);
    child->fp = parent->fp;
}

// exec(): the new program starts with zeroed FP registers.
void fp_exec(struct proc *p) {
    memset(&p->fp, 0, sizeof(p->fp));
    fp_reload(p);
}

// An illegal-instruction trap from user mode (interrupts off): if FP
// was off, it may be p's first FP instruction on this hart. Load its
// registers and enable FP; the instruction is retried, and if it wasn't
// an FP instruction it traps again with FP on. Returns 1 if handled.
int fp_trap(struct proc *p) {
    if (fs() != FS_OFF)
        return 0;
    set_fs(FS_CLEAN);
    fp_load(&p->fp);
    set_fs(FS_CLEAN);   // the load dirtied it
    return 1;
}

// After a kernel trap that may have switched processes, keep the
// current FS, not the one saved before the switch.
uint64 fp_fixup_status(uint64 status) {
    return (status & ~FS_MASK) | fs();
}
