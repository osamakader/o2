#ifndef LOCKSTAT_H
#define LOCKSTAT_H

#include "types.h"

// Spin lock statistics, per lock name (lockstat()); shared with user
// space.
struct lockstat {
    char name[16];
    uint64 acquire;     // acquire() calls
    uint64 contended;   // ... that found the lock held
    uint64 spin;        // failed attempts to take it
};

#endif
