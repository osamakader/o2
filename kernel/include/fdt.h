#ifndef FDT_H
#define FDT_H

#include "types.h"

// Flattened device tree reader (fdt.c). Nodes are offsets; -1 is none.

#define FDT_MAXDEPTH 16

int fdt_init(uint64 pa);
int fdt_present(void);
uint64 fdt_size(void);
int fdt_memreserve(int i, uint64 *addr, uint64 *size);
int fdt_next_node(int node, int *depth);
const char *fdt_name(int node);
const void *fdt_getprop(int node, const char *name, int *len);
int fdt_parent(int node);
int fdt_find_compatible(int node, const char *compat);
uint32 fdt_cells(int node, const char *name, uint32 def);
int fdt_reg(int node, int i, uint64 *addr, uint64 *size);
uint32 fdt32(const void *p);
int fdt_okay(int node);

#endif
