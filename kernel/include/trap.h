#ifndef TRAP_H
#define TRAP_H

#include "types.h"

struct trapframe {
    uint64 x[31];
    uint64 elr;
    uint64 spsr;
    uint64 esr;
    uint64 far;
    uint64 pad;
};

void irq_handler(struct trapframe *tf);
void fiq_handler(struct trapframe *tf);
void serr_handler(struct trapframe *tf);
void sync_handler(struct trapframe *tf);
#endif