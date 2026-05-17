#include "types.h"

struct pl011 {
    uint64 base_address;
    uint64 base_clock;
    uint32 baudrate;
    uint32 data_bits;
    uint32 stop_bits;
};

static const uint32 DR_OFFSET = 0x000;
static const uint32 FR_OFFSET = 0x018;
static const uint32 IBRD_OFFSET = 0x024;
static const uint32 FBRD_OFFSET = 0x028;
static const uint32 LCR_OFFSET = 0x02c;
static const uint32 CR_OFFSET = 0x030;
static const uint32 IMSC_OFFSET = 0x038;
static const uint32 DMACR_OFFSET = 0x048;

int pl011_setup(struct pl011 *dev, uint64 base_address, uint64 base_clock);
int pl011_send(const char *data);
void pl011_init(uint64 base_address, uint64 base_clock);