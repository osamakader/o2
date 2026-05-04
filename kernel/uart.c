#include "uart.h"

static const uint32_t CR_TXEN = (1 << 8);
static const uint32_t CR_UARTEN = (1 << 0);

static const uint32_t LCR_FEN = (1 << 4);
static const uint32_t LCR_STP2 = (1 << 3);

static const uint32_t FR_TXFF = (1 << 5);


volatile uint32_t *reg(const struct pl011 *dev, uint32_t offset)
{
    const uint64_t addr = dev->base_address + offset;

    return (volatile uint32_t *)((void *)addr);
}

static void write_reg(const struct pl011 *dev, uint32_t offset, uint32_t value)
{
    *reg(dev, offset) = value;
}

static uint32_t read_reg(const struct pl011 *dev, uint32_t offset)
{
    return *reg(dev, offset);
}


static void wait_tx_ready(const struct pl011 *dev)
{
    while ((read_reg(dev, FR_OFFSET) & FR_TXFF) != 0) {}
}

static void calculate_divisors(
    const struct pl011 *dev, uint32_t *integer, uint32_t *fractional)
{
    // 64 * F_UARTCLK / (16 * B) = 4 * F_UARTCLK / B
    const uint32_t div = 4 * dev->base_clock / dev->baudrate;

    *fractional = div & 0x3f;
    *integer = (div >> 6) & 0xffff;
}

int pl011_reset(const struct pl011 *dev)
{
    uint32_t lcr = read_reg(dev, LCR_OFFSET);
    uint32_t ibrd, fbrd;

    // disable UART
    write_reg(dev, CR_OFFSET, 0);

    // wait for any ongoing transmissions
    wait_tx_ready(dev);

    // flush FIFOs
    write_reg(dev, LCR_OFFSET, (lcr & ~LCR_FEN));

    // set frequency divisors
    calculate_divisors(dev, &ibrd, &fbrd);
    write_reg(dev, IBRD_OFFSET, ibrd);
    write_reg(dev, FBRD_OFFSET, fbrd);

    // configure data frame format according to the parameters (UARTLCR_H).

    lcr = 0;
    // WLEN part of UARTLCR_H
    lcr |= ((dev->data_bits - 1) & 0x3) << 5;

    // configure the number of stop bits
    if (dev->stop_bits == 2)
        lcr |= LCR_STP2;

    write_reg(dev, LCR_OFFSET, lcr);
    // mask all interrupts 
    write_reg(dev, IMSC_OFFSET, 0x7ff);

    // disable DMA 
    write_reg(dev, DMACR_OFFSET, 0x0);

    // enable UART with tx 
    write_reg(dev, CR_OFFSET, CR_TXEN | CR_UARTEN);

    return 0;
}

int pl011_setup(struct pl011 *dev, uint64_t base_address, uint64_t base_clock)
{
    dev->base_address = base_address;
    dev->base_clock = base_clock;

    dev->baudrate = 115200;
    dev->data_bits = 8;
    dev->stop_bits = 1;
    return pl011_reset(dev);
}

int pl011_send(const struct pl011 *dev, const char *data)
{
    wait_tx_ready(dev);

    for (uint32_t i = 0; data[i] != '\0'; ++i) {
        if (data[i] == '\n') {
            write_reg(dev, DR_OFFSET, '\r');
            wait_tx_ready(dev);
        }
        write_reg(dev, DR_OFFSET, data[i]);
        wait_tx_ready(dev);
    }

    return 0;
}
