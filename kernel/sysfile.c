// File-related system calls. There is no file layer yet: fds 0-2 are
// the console.

#include "types.h"
#include "param.h"
#include "defs.h"
#include "console.h"
#include "proc.h"

uint64 sys_read(void) {
    int fd, n;
    uint64 p;

    argint(0, &fd);
    argaddr(1, &p);
    argint(2, &n);
    if (fd != 0)
        return -1;
    if (n < 0)
        return -1;
    return consoleread(1, p, n);
}

uint64 sys_write(void) {
    int fd, n;
    uint64 p;

    argint(0, &fd);
    argaddr(1, &p);
    argint(2, &n);
    if (fd != 1 && fd != 2)
        return -1;
    if (n < 0)
        return -1;
    return consolewrite(1, p, n);
}
