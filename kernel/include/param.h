#ifndef PARAM_H
#define PARAM_H

#define NCPU  8   // maximum number of CPUs
#define NPROC 64  // maximum number of processes and kernel threads
#define MAXARG 32 // max exec arguments
#define USERSTACK 1 // user stack pages
#define ROOTDEV       1  // device number of file system root disk
#define MAXOPBLOCKS  20  // max # of blocks any FS op writes (deleting a
                         // MAXFILE file frees blocks in ~10 bitmap blocks)
#define LOGBLOCKS    (MAXOPBLOCKS*3)  // max data blocks in on-disk log
#define NBUF         (LOGBLOCKS+MAXOPBLOCKS*3)  // size of disk block cache:
                         // the log pins up to LOGBLOCKS between commits
#define FSSIZE      70000  // size of file system in blocks (> MAXFILE)
#define NINODE       50  // maximum number of active i-nodes
#define NFILE       100  // open files per system
#define NOFILE       16  // open files per process
#define NDEV         10  // maximum major device number
#define MAXPATH     128  // maximum file path name
#define NVMA         16  // memory mappings (mmap) per process

#endif
