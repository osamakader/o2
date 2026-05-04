#include <stdint.h>

struct pl011 {
    uint64_t base_address;
    uint64_t base_clock;
    uint32_t baudrate;
    uint32_t data_bits;
    uint32_t stop_bits;
};

static const uint32_t DR_OFFSET = 0x000;
static const uint32_t FR_OFFSET = 0x018;
static const uint32_t IBRD_OFFSET = 0x024;
static const uint32_t FBRD_OFFSET = 0x028;
static const uint32_t LCR_OFFSET = 0x02c;
static const uint32_t CR_OFFSET = 0x030;
static const uint32_t IMSC_OFFSET = 0x038;
static const uint32_t DMACR_OFFSET = 0x048;

int pl011_setup(struct pl011 *dev, uint64_t base_address, uint64_t base_clock);
int pl011_send(const struct pl011 *dev, const char *data);
