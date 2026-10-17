#include "types.h"
#include "param.h"
#include "defs.h"
#include "proc.h"

uint64 sys_getpid(void) {
    return myproc()->pid;
}
