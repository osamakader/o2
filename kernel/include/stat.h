#ifndef STAT_H
#define STAT_H

#include "types.h"

#define T_DIR     1   // Directory
#define T_FILE    2   // File
#define T_DEVICE  3   // Device

struct stat {
    int dev;     // File system's disk device
    uint ino;    // Inode number
    short type;  // Type of file
    short nlink; // Number of links to file
    uint64 size; // Size of file in bytes
    uint64 atime;   // seconds since the epoch: created (o2 doesn't
                    // update it on reads)
    uint64 mtime;   // contents last changed
    uint64 ctime;   // inode last changed (contents, links)
};

#endif
