#ifndef BACKTRACE_H
#define BACKTRACE_H

#include "types.h"

// Kernel symbol table, generated at link time (Makefile), sorted by
// address.
struct ksym {
    uint64 addr;
    const char *name;
};
extern const struct ksym ksyms[];
extern const uint64 nksyms;

const char *ksym_lookup(uint64 pc, uint64 *off);
void backtrace_from(uint64 fp, uint64 pc);
void backtrace(void);

#endif
