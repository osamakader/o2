#ifndef FPU_H
#define FPU_H

// User floating-point/SIMD state, switched lazily (arch/<arch>/fpu.c).
// A process's FP registers are live in its CPU's registers only while
// user FP access is enabled there; the first FP instruction after the
// process (re)gains the CPU traps and loads them. The kernel itself
// never touches FP registers.

struct proc;

void fpinithart(void);                               // FP disabled for user mode
void fp_switch_out(struct proc *p);                  // p leaves its CPU: save if live
void fp_fork(struct proc *parent, struct proc *child); // child gets parent's state
void fp_exec(struct proc *p);                        // fresh state for a new program
void fp_sync(struct proc *p);    // current process: bring p->fp up to date
void fp_reload(struct proc *p);  // p->fp was changed: drop live registers

#endif
