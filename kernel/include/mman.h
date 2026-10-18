#ifndef MMAN_H
#define MMAN_H

// mmap(), shared with user space; Linux's values.

// prot
#define PROT_NONE  0x0
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4

// flags: MAP_SHARED or MAP_PRIVATE, optionally MAP_ANONYMOUS
#define MAP_SHARED    0x01   // writes go to the file, and are seen by
                             // children sharing the mapping
#define MAP_PRIVATE   0x02   // writes are the process's own
#define MAP_ANONYMOUS 0x20   // zeroed memory, no file

#define MAP_FAILED ((void *)-1)

#endif
