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

// broken-down time, from gmtime() in the user library
struct tm {
    int tm_sec;    // 0..59
    int tm_min;    // 0..59
    int tm_hour;   // 0..23
    int tm_mday;   // 1..31
    int tm_mon;    // 0..11
    int tm_year;   // years since 1900
    int tm_wday;   // 0..6, Sunday = 0
    int tm_yday;   // 0..365
};

#endif
