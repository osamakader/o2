#ifndef TRAP_H
#define TRAP_H

#include <stdint.h>

struct trapframe {
    uint64_t x[31];
    uint64_t elr;
    uint64_t spsr;
    uint64_t esr;
    uint64_t far;
    uint64_t pad;
};

void irq_handler(struct trapframe *tf);
void fiq_handler(struct trapframe *tf);
void serr_handler(struct trapframe *tf);
void sync_handler(struct trapframe *tf);
#endif