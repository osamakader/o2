// A reader for the flattened device tree (FDT, "DTB") the firmware
// describes the machine with: qemu on arm64 places it at the start of
// RAM, OpenSBI passes its address on riscv64. Only read at boot, before
// kinit() may reuse its memory.
//
// Nodes are named by their offset in the structure block. All values
// in the blob are big-endian.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "defs.h"
#include "fdt.h"

#define FDT_MAGIC      0xd00dfeed
#define FDT_BEGIN_NODE 1
#define FDT_END_NODE   2
#define FDT_PROP       3
#define FDT_NOP        4
#define FDT_END        9

struct fdt_header {
    uint32 magic;
    uint32 totalsize;
    uint32 off_dt_struct;
    uint32 off_dt_strings;
    uint32 off_mem_rsvmap;
    uint32 version;
    uint32 last_comp_version;
    uint32 boot_cpuid_phys;
    uint32 size_dt_strings;
    uint32 size_dt_struct;
};

static const char *blob;      // kernel address of the FDT, or 0
static const char *structs;   // structure block
static const char *strings;   // strings block
static uint32 struct_size;

uint32 fdt32(const void *p) {
    const uchar *b = p;
    return (uint32)b[0] << 24 | (uint32)b[1] << 16 | (uint32)b[2] << 8 | b[3];
}

static uint64 fdt64(const void *p) {
    return (uint64)fdt32(p) << 32 | fdt32((const char *)p + 4);
}

static uint32 align4(uint32 x) {
    return (x + 3) & ~3;
}

// Is there a device tree at physical address pa? Remember it if so.
int fdt_init(uint64 pa) {
    const struct fdt_header *h = (const struct fdt_header *)P2V(pa);
    if (pa == 0 || fdt32(&h->magic) != FDT_MAGIC || fdt32(&h->last_comp_version) > 17)
        return -1;
    blob = (const char *)h;
    structs = blob + fdt32(&h->off_dt_struct);
    strings = blob + fdt32(&h->off_dt_strings);
    struct_size = fdt32(&h->size_dt_struct);
    return 0;
}

int fdt_present(void) {
    return blob != 0;
}

uint64 fdt_size(void) {
    return blob ? fdt32(&((const struct fdt_header *)blob)->totalsize) : 0;
}

// The i-th memory reservation (memreserve block): 0, or -1 past the
// last.
int fdt_memreserve(int i, uint64 *addr, uint64 *size) {
    const struct fdt_header *h = (const struct fdt_header *)blob;
    const char *r = blob + fdt32(&h->off_mem_rsvmap) + 16 * i;
    *addr = fdt64(r);
    *size = fdt64(r + 8);
    return *size == 0 && *addr == 0 ? -1 : 0;
}

// The token at structure offset off, skipping NOPs; *off moves to it.
static uint32 token(uint32 *off) {
    for (;;) {
        if (*off + 4 > struct_size)
            return FDT_END;
        uint32 t = fdt32(structs + *off);
        if (t != FDT_NOP)
            return t;
        *off += 4;
    }
}

// The offset after the token at off (and its name or value).
static uint32 skip(uint32 off) {
    uint32 t = fdt32(structs + off);
    if (t == FDT_BEGIN_NODE)
        return off + 4 + align4(strlen(structs + off + 4) + 1);
    if (t == FDT_PROP)
        return off + 12 + align4(fdt32(structs + off + 4));
    return off + 4;
}

// The node after node (in depth-first order; -1: the root), or -1.
// *depth (optional) tracks the depth: the root is at 0.
int fdt_next_node(int node, int *depth) {
    if (blob == 0)
        return -1;
    uint32 off = node < 0 ? 0 : skip(node);
    int d = depth ? *depth : 0;
    if (node < 0)
        d = -1;
    for (;;) {
        uint32 t = token(&off);
        if (t == FDT_BEGIN_NODE) {
            if (depth)
                *depth = d + 1;
            return off;
        }
        if (t == FDT_END_NODE)
            d--;
        else if (t != FDT_PROP)
            return -1;
        off = skip(off);
    }
}

const char *fdt_name(int node) {
    return structs + node + 4;
}

// Property name of node: its value and length, or 0.
const void *fdt_getprop(int node, const char *name, int *len) {
    uint32 off = skip(node);
    for (;;) {
        uint32 t = token(&off);
        if (t != FDT_PROP)
            return 0;   // a child node or the node's end: no more properties
        uint32 n = fdt32(structs + off + 4);
        if (strncmp(strings + fdt32(structs + off + 8), name, 64) == 0) {
            if (len)
                *len = n;
            return structs + off + 12;
        }
        off = skip(off);
    }
}

// The node's parent, or -1 for the root.
int fdt_parent(int node) {
    int stack[FDT_MAXDEPTH], depth = 0;
    for (int n = fdt_next_node(-1, &depth); n >= 0; n = fdt_next_node(n, &depth)) {
        if (depth >= FDT_MAXDEPTH)
            return -1;
        stack[depth] = n;
        if (n == node)
            return depth > 0 ? stack[depth - 1] : -1;
    }
    return -1;
}

// Does a string list property (like "compatible") contain s?
static int has_string(const char *list, int len, const char *s) {
    for (int i = 0; i < len; i += strlen(list + i) + 1)
        if (strncmp(list + i, s, len - i) == 0)
            return 1;
    return 0;
}

// The next node after node (-1: from the start) that is compatible
// with compat, or -1.
int fdt_find_compatible(int node, const char *compat) {
    while ((node = fdt_next_node(node, 0)) >= 0) {
        int len;
        const char *c = fdt_getprop(node, "compatible", &len);
        if (c && has_string(c, len, compat))
            return node;
    }
    return -1;
}

// A cells property of node (#address-cells and the like), or def.
uint32 fdt_cells(int node, const char *name, uint32 def) {
    int len;
    const void *p = node >= 0 ? fdt_getprop(node, name, &len) : 0;
    return p && len == 4 ? fdt32(p) : def;
}

static uint64 cells(const char *p, uint32 n) {
    return n == 2 ? fdt64(p) : n == 1 ? fdt32(p) : 0;
}

// The i-th (address, size) pair of node's "reg", with the parent's
// #address-cells and #size-cells. Returns 0, or -1 if there is none.
int fdt_reg(int node, int i, uint64 *addr, uint64 *size) {
    int parent = fdt_parent(node), len;
    uint32 ac = fdt_cells(parent, "#address-cells", 2);
    uint32 sc = fdt_cells(parent, "#size-cells", 1);
    const char *reg = fdt_getprop(node, "reg", &len);
    uint32 entry = 4 * (ac + sc);
    if (reg == 0 || ac > 2 || sc > 2 || entry * (i + 1) > len)
        return -1;
    *addr = cells(reg + entry * i, ac);
    *size = cells(reg + entry * i + 4 * ac, sc);
    return 0;
}
