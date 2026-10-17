// The xv6 memlayout.h constants xv6 user tests use, with o2's values.
// See riscv.h for the user address-space ones.

#include "riscv.h"

// Start of RAM as the kernel maps it (KVA_OFFSET + physical start):
// user code must not be able to touch it.
#if defined(__aarch64__)
#define KERNBASE 0xffffffc040000000UL
#else
#define KERNBASE 0xffffffc080000000UL
#endif
