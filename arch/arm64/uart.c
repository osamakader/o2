#include "uart.h"
#include "console.h"

static const uint32 CR_RXEN = (1 << 9);
static const uint32 CR_TXEN = (1 << 8);
static const uint32 CR_UARTEN = (1 << 0);

static const uint32 LCR_FEN = (1 << 4);
static const uint32 LCR_STP2 = (1 << 3);

static const uint32 FR_TXFF = (1 << 5);
static const uint32 FR_RXFE = (1 << 4);

// interrupt bits, shared by IMSC (mask: 1 = enabled) and ICR (clear)
static const uint32 INT_RX = (1 << 4);   // RX FIFO reached its trigger level
static const uint32 INT_RT = (1 << 6);   // RX timeout: data sitting in the FIFO

static struct pl011 sdev;

static volatile uint32 *reg(const struct pl011 *dev, uint32 offset)
{
    const uint64 addr = dev->base_address + offset;

    return (volatile uint32 *)((void *)addr);
}

static void write_reg(const struct pl011 *dev, uint32 offset, uint32 value)
{
    *reg(dev, offset) = value;
}

static uint32 read_reg(const struct pl011 *dev, uint32 offset)
{
    return *reg(dev, offset);
}


static void wait_tx_ready(const struct pl011 *dev)
{
    while ((read_reg(dev, FR_OFFSET) & FR_TXFF) != 0) {}
}

static void calculate_divisors(
    const struct pl011 *dev, uint32 *integer, uint32 *fractional)
{
    // 64 * F_UARTCLK / (16 * B) = 4 * F_UARTCLK / B
    const uint32 div = 4 * dev->base_clock / dev->baudrate;

    *fractional = div & 0x3f;
    *integer = (div >> 6) & 0xffff;
}

int pl011_reset(const struct pl011 *dev)
{
    uint32 lcr = read_reg(dev, LCR_OFFSET);
    uint32 ibrd, fbrd;

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

    // FIFOs on: the RX timeout interrupt picks up partly filled ones
    lcr |= LCR_FEN;

    write_reg(dev, LCR_OFFSET, lcr);
    // mask all interrupts: a set IMSC bit enables that interrupt
    write_reg(dev, IMSC_OFFSET, 0);

    // disable DMA 
    write_reg(dev, DMACR_OFFSET, 0x0);

    // enable UART with tx and rx
    write_reg(dev, CR_OFFSET, CR_RXEN | CR_TXEN | CR_UARTEN);

    return 0;
}

int pl011_setup(struct pl011 *dev, uint64 base_address, uint64 base_clock)
{
    dev->base_address = base_address;
    dev->base_clock = base_clock;

    dev->baudrate = 115200;
    dev->data_bits = 8;
    dev->stop_bits = 1;
    return pl011_reset(dev);
}

int pl011_send(const char *data)
{
    struct pl011 *dev = &sdev;
    wait_tx_ready(dev);

    for (uint32 i = 0; data[i] != '\0'; ++i) {
        if (data[i] == '\n') {
            write_reg(dev, DR_OFFSET, '\r');
            wait_tx_ready(dev);
        }
        write_reg(dev, DR_OFFSET, data[i]);
        wait_tx_ready(dev);
    }

    return 0;
}

void pl011_init(uint64 base_address, uint64 base_clock) {
    pl011_setup(&sdev, base_address, base_clock);
}

// Read one input character, or -1 if the RX FIFO is empty.
int pl011_getc(void)
{
    struct pl011 *dev = &sdev;

    if (read_reg(dev, FR_OFFSET) & FR_RXFE)
        return -1;
    return read_reg(dev, DR_OFFSET) & 0xff;
}

// Unmask the receive interrupts. The UART's SPI must also be enabled
// in the GIC.
void pl011_enable_rx_irq(void)
{
    struct pl011 *dev = &sdev;

    write_reg(dev, ICR_OFFSET, INT_RX | INT_RT);
    write_reg(dev, IMSC_OFFSET, INT_RX | INT_RT);
}

// UART interrupt: drain the RX FIFO into the console.
void uartintr(void)
{
    int c;

    while ((c = pl011_getc()) >= 0)
        consoleintr(c);

    // reading the FIFO empty already deasserts RX/RT; clear any left
    write_reg(&sdev, ICR_OFFSET, INT_RX | INT_RT);
}
