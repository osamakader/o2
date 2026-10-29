#ifndef FILE_H
#define FILE_H

#include "types.h"
#include "sleeplock.h"
#include "fs.h"

struct pipe;
struct sock;
struct shm;

struct file {
    enum { FD_NONE, FD_PIPE, FD_INODE, FD_DEVICE, FD_SOCK, FD_SHM } type;
    int ref; // reference count
    char readable;
    char writable;
    char nonblock;     // O_NONBLOCK: reads don't wait
    struct pipe *pipe; // FD_PIPE
    struct inode *ip;  // FD_INODE and FD_DEVICE
    uint off;          // FD_INODE
    short major;       // FD_DEVICE
    struct sock *sock; // FD_SOCK
    struct shm *shm;   // FD_SHM
};

// in-memory copy of an inode
struct inode {
    uint dev;           // Device number
    uint inum;          // Inode number
    int ref;            // Reference count
    struct sleeplock lock; // protects everything below here
    int valid;          // inode has been read from disk?

    short type;         // copy of disk inode
    short major;
    short minor;
    short nlink;
    uint size;
    uint addrs[NDIRECT+2];
    uint64 atime, mtime, ctime;
};

// map major device number to device functions.
struct devsw {
    int (*read)(int user_dst, uint64 dst, int n, int nonblock);
    int (*write)(int user_src, uint64 src, int n);
    int (*ioctl)(int req, uint64 arg);   // may be 0
    uint64 (*mmap)(uint64 pgoff);        // physical address of page pgoff
                                         // to map (or 0); may be 0
    int uncached;                        // map those uncached
    int (*poll)(void);                   // POLLIN if a read wouldn't
                                         // wait; 0: always
};

extern struct devsw devsw[];

#define CONSOLE 1

#endif
