#ifndef TIME_H
#define TIME_H

#include "types.h"

// clock_gettime(), shared with user space
#define CLOCK_REALTIME  0   // wall-clock time since the epoch (UTC)
#define CLOCK_MONOTONIC 1   // time since boot

struct timespec {
    uint64 tv_sec;
    uint64 tv_nsec;   // 0..999999999
};

#endif
