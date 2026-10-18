#ifndef FS_H
#define FS_H

// On-disk file system format, from xv6.
// Both the kernel and user programs use this header file.

#include "types.h"

#define ROOTINO  1   // root i-number
#define BSIZE 1024  // block size

// Disk layout:
// [ boot block | super block | log | inode blocks |
//                                          free bit map | data blocks]
//
// mkfs computes the super block and builds an initial file system. The
// super block describes the disk layout:
struct superblock {
    uint magic;        // Must be FSMAGIC
    uint size;         // Size of file system image (blocks)
    uint nblocks;      // Number of data blocks
    uint ninodes;      // Number of inodes.
    uint nlog;         // Number of log blocks
    uint logstart;     // Block number of first log block
    uint inodestart;   // Block number of first inode block
    uint bmapstart;    // Block number of first free map block
};

#define FSMAGIC 0x10203042   // o2: 128-byte inodes, timestamps, double-indirect

#define NDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint))
#define NDINDIRECT (NINDIRECT * NINDIRECT)
#define MAXFILE (NDIRECT + NINDIRECT + NDINDIRECT)

// On-disk inode structure
struct dinode {
    short type;           // File type
    short major;          // Major device number (T_DEVICE only)
    short minor;          // Minor device number (T_DEVICE only)
    short nlink;          // Number of links to inode in file system
    uint size;            // Size of file (bytes)
    uint addrs[NDIRECT+2];   // Data block addresses: direct, then
                             // indirect and double-indirect blocks
    uint64 atime;         // seconds since the epoch: created (not
                          // updated by reads)
    uint64 mtime;         // contents last changed
    uint64 ctime;         // inode last changed (contents, links)
    uint spare[8];        // room to grow; zero
};

_Static_assert(sizeof(struct dinode) == 128, "dinode size");

// Inodes per block.
#define IPB           (BSIZE / sizeof(struct dinode))

// Block containing inode i
#define IBLOCK(i, sb)     ((i) / IPB + sb.inodestart)

// Bitmap bits per block
#define BPB           (BSIZE*8)

// Block of free map containing bit for block b
#define BBLOCK(b, sb) ((b)/BPB + sb.bmapstart)

// Directory is a file containing a sequence of dirent structures.
#define DIRSIZ 14

struct dirent {
    ushort inum;
    char name[DIRSIZ];
};

#endif
