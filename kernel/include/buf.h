#ifndef BUF_H
#define BUF_H

#include "types.h"
#include "sleeplock.h"
#include "fs.h"

struct buf {
    int valid;   // has data been read from disk?
    int disk;    // does disk "own" buf?
    uint dev;
    uint blockno;
    struct sleeplock lock;
    uint refcnt;
    uint64 lastuse;   // when last released (bio.c), for LRU
    struct buf *prev; // its hash bucket's list
    struct buf *next;
    uchar data[BSIZE];
};

#endif
