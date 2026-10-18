//
// Driver for an SD card behind a standard SD host controller (SDHCI):
// the Raspberry Pi 4's EMMC2, where its card slot is (or under qemu's
// raspi4b, the other one, board.sdhci). Polled PIO, one
// 512-byte sector at a time, with 32-bit register accesses only (the
// Pi's controllers ignore narrower ones). The card is brought up as the
// SD spec says: CMD0, CMD8, ACMD41 until ready, CMD2, CMD3, CMD7.
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "defs.h"
#include "console.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "board.h"
#include "timer.h"

#define SD_ARG2       0x00
#define SD_BLKSIZECNT 0x04
#define SD_ARG1       0x08
#define SD_CMDTM      0x0c   // transfer mode (low half), command (high)
#define SD_RESP0      0x10
#define SD_RESP1      0x14
#define SD_RESP2      0x18
#define SD_RESP3      0x1c
#define SD_DATA       0x20
#define SD_STATUS     0x24   // present state
#define SD_CONTROL0   0x28   // host control, power control
#define SD_CONTROL1   0x2c   // clock control, timeout, software reset
#define SD_INTERRUPT  0x30   // normal (low half) and error (high) status
#define SD_IRPT_MASK  0x34   // status enable
#define SD_IRPT_EN    0x38   // signal enable
#define SD_CAPS       0x40

#define STATUS_CMD_INHIBIT 0x1
#define STATUS_DAT_INHIBIT 0x2

#define INT_CMD_DONE  0x0001
#define INT_DATA_DONE 0x0002
#define INT_WRITE_RDY 0x0010
#define INT_READ_RDY  0x0020
#define INT_ERROR     0x8000

#define C1_CLK_INTLEN  0x0001   // internal clock on
#define C1_CLK_STABLE  0x0002
#define C1_CLK_EN      0x0004   // clock to the card
#define C1_TOUNIT_MAX  0x000e0000
#define C1_SRST_HC     0x01000000

// command register flags (in the high half of SD_CMDTM)
#define RESP_NONE   0x0
#define RESP_136    0x1
#define RESP_48     0x2
#define RESP_48BUSY 0x3
#define CMD_CRCCHK  0x08
#define CMD_IXCHK   0x10
#define CMD_DATA    0x20
// transfer mode (low half)
#define TM_READ     0x10

#define R1  (RESP_48 | CMD_CRCCHK | CMD_IXCHK)
#define R1b (RESP_48BUSY | CMD_CRCCHK | CMD_IXCHK)
#define R2  (RESP_136 | CMD_CRCCHK)
#define R3  RESP_48
#define R6  (RESP_48 | CMD_CRCCHK | CMD_IXCHK)
#define R7  (RESP_48 | CMD_CRCCHK | CMD_IXCHK)

static struct {
    uint64 base;    // kernel address of the registers; 0 if no card
    int sdhc;       // block addressing (SDHC/SDXC), not bytes
    uint32 rca;
    uint64 sectors; // 0 if not known
    struct sleeplock lock;
} sd;

static uint32 rd(uint32 r)
{
    return *(volatile uint32 *)(sd.base + r);
}

static void wr(uint32 r, uint32 v)
{
    *(volatile uint32 *)(sd.base + r) = v;
}

#define TIMEOUT_NS 1000000000UL   // a second

// Wait until (reg & mask) == want; returns 0, or -1 after TIMEOUT_NS.
static int waitfor(uint32 r, uint32 mask, uint32 want)
{
    uint64 t0 = timer_ns();
    while ((rd(r) & mask) != want)
        if (timer_ns() - t0 > TIMEOUT_NS)
            return -1;
    return 0;
}

// Send command cmd with argument arg; for data commands, the caller
// moves the data. Returns 0, or -1 on an error or timeout.
static int command(int cmd, uint32 flags, uint32 arg, int data_read)
{
    uint32 inhibit = STATUS_CMD_INHIBIT | ((flags & (CMD_DATA | RESP_48BUSY)) ? STATUS_DAT_INHIBIT : 0);
    if (waitfor(SD_STATUS, inhibit, 0) < 0)
        return -1;
    wr(SD_INTERRUPT, 0xffffffff);   // clear old status
    wr(SD_ARG1, arg);
    uint32 tm = (flags & CMD_DATA) && data_read ? TM_READ : 0;
    wr(SD_CMDTM, (uint32)cmd << 24 | flags << 16 | tm);
    for (uint64 t0 = timer_ns(); timer_ns() - t0 < TIMEOUT_NS; ) {
        uint32 s = rd(SD_INTERRUPT);
        if (s & INT_ERROR) {
            wr(SD_INTERRUPT, 0xffffffff);
            return -1;
        }
        if (s & INT_CMD_DONE) {
            wr(SD_INTERRUPT, INT_CMD_DONE);
            return 0;
        }
    }
    return -1;
}

// An application command (ACMD): CMD55 first.
static int appcommand(int cmd, uint32 flags, uint32 arg)
{
    if (command(55, R1, sd.rca << 16, 0) < 0)
        return -1;
    return command(cmd, flags, arg, 0);
}

#define RPI_CLOCK_EMMC2 12

// The controller's base clock: from its capabilities, or else (the Pi
// 4's EMMC2, 200MHz there) from the firmware, or a guess.
static uint32 baseclock(void)
{
    uint32 base = ((rd(SD_CAPS) >> 8) & 0xff) * 1000000;
    if (base == 0)
        base = rpi_clock_rate(RPI_CLOCK_EMMC2);
    return base ? base : 100000000;
}

// The card's clock: base / (2 * div), at most hz.
static int setclock(uint32 hz)
{
    uint32 base = baseclock();
    uint32 div = (base + 2 * hz - 1) / (2 * hz);
    if (div > 0x3ff)
        div = 0x3ff;
    uint32 c1 = rd(SD_CONTROL1) & ~0xffff;
    wr(SD_CONTROL1, c1);   // the card's clock off while changing it
    c1 |= (div & 0xff) << 8 | ((div >> 8) & 0x3) << 6 | C1_CLK_INTLEN | C1_TOUNIT_MAX;
    wr(SD_CONTROL1, c1);
    if (waitfor(SD_CONTROL1, C1_CLK_STABLE, C1_CLK_STABLE) < 0)
        return -1;
    wr(SD_CONTROL1, c1 | C1_CLK_EN);
    return 0;
}

static int card_init(void)
{
    // reset the controller, power at 3.3V, a slow clock
    wr(SD_CONTROL1, C1_SRST_HC);
    if (waitfor(SD_CONTROL1, C1_SRST_HC, 0) < 0)
        return -1;
    wr(SD_CONTROL0, 0x0f00);     // bus power on, 3.3V
    if (setclock(400000) < 0)
        return -1;
    wr(SD_IRPT_MASK, 0xffffffff);
    wr(SD_IRPT_EN, 0);           // polled: no interrupts

    sd.rca = 0;
    if (command(0, RESP_NONE, 0, 0) < 0)
        return -1;
    // CMD8: SD 2.0 or later at 2.7-3.6V?
    int v2 = command(8, R7, 0x1aa, 0) == 0 && (rd(SD_RESP0) & 0xfff) == 0x1aa;
    uint32 ocr = 0;
    for (int i = 0; i < 1000; i++) {
        if (appcommand(41, R3, 0x00ff8000 | (v2 ? 0x40000000 : 0)) < 0)
            return -1;
        ocr = rd(SD_RESP0);
        if (ocr & 0x80000000)
            break;   // powered up
        sleep_ticks(1);
    }
    if (!(ocr & 0x80000000))
        return -1;
    sd.sdhc = (ocr & 0x40000000) != 0;
    if (command(2, R2, 0, 0) < 0 || command(3, R6, 0, 0) < 0)
        return -1;
    sd.rca = rd(SD_RESP0) >> 16;

    // its size, from the CSD (CMD9), while it is still in standby. The
    // controller drops the CSD's 8 CRC bits: CSD bit n is response bit
    // n - 8, in RESP[(n - 8) / 32].
    if (command(9, R2, sd.rca << 16, 0) == 0) {
        uint64 r1 = rd(SD_RESP1), r2 = rd(SD_RESP2);
        if (sd.sdhc) {
            // CSD 2.0: C_SIZE, bits 69:48; (C_SIZE + 1) * 512KB
            sd.sectors = (((r1 >> 8) & 0x3fffff) + 1) * 1024;
        } else {
            // CSD 1.0: C_SIZE bits 73:62, C_SIZE_MULT 49:47, READ_BL_LEN
            // 83:80; (C_SIZE + 1) << (C_SIZE_MULT + 2) blocks of
            // 2^READ_BL_LEN bytes
            uint64 csize = (r2 & 3) << 10 | r1 >> 22;
            uint64 mult = (r1 >> 7) & 7, bl = (r2 >> 8) & 0xf;
            sd.sectors = ((csize + 1) << (mult + 2 + bl)) / 512;
        }
    }
    if (command(7, R1b, sd.rca << 16, 0) < 0)
        return -1;
    if (!sd.sdhc && command(16, R1, 512, 0) < 0)
        return -1;
    return setclock(25000000);
}

// Find the controller with the card (board.sdhci); returns 0 if there
// is a card, and its size in sectors in *sectors (0 if unknown).
int sd_init(uint64 *sectors)
{
    initsleeplock(&sd.lock, "sd");
    for (int i = 0; i < NELEM(board.sdhci); i++) {
        if (board.sdhci[i] == 0)
            continue;
        sd.base = P2V(board.sdhci[i]);
        if (card_init() == 0) {
            *sectors = sd.sectors;
            printf("sd: %s card at %p, %lu MB, base clock %d MHz\n", sd.sdhc ? "SDHC" : "SD",
                   (void *)board.sdhci[i], sd.sectors / 2048, baseclock() / 1000000);
            return 0;
        }
    }
    sd.base = 0;
    return -1;
}

// Read (write = 0) or write the 512-byte sector at data.
int sd_rw(uint64 sector, char *data, int write)
{
    uint32 *w = (uint32 *)data;
    int r = 0;

    acquiresleep(&sd.lock);
    wr(SD_BLKSIZECNT, 1 << 16 | 512);
    uint32 arg = sd.sdhc ? sector : sector * 512;
    if (command(write ? 24 : 17, R1 | CMD_DATA, arg, !write) < 0) {
        r = -1;
        goto out;
    }
    uint32 ready = write ? INT_WRITE_RDY : INT_READ_RDY;
    if (waitfor(SD_INTERRUPT, ready | INT_ERROR, ready) < 0) {
        r = -1;
        goto out;
    }
    wr(SD_INTERRUPT, ready);
    for (int i = 0; i < 128; i++) {
        if (write)
            wr(SD_DATA, w[i]);
        else
            w[i] = rd(SD_DATA);
    }
    if (waitfor(SD_INTERRUPT, INT_DATA_DONE | INT_ERROR, INT_DATA_DONE) < 0)
        r = -1;
    wr(SD_INTERRUPT, 0xffffffff);
out:
    releasesleep(&sd.lock);
    return r;
}
