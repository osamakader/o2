// The Raspberry Pi firmware's mailbox property interface (channel 8):
// messages of tagged requests the VideoCore firmware answers in place.
// It needs the message's physical address, below 4GB, 16-byte aligned:
// rpi_mbox_call() copies each message to a buffer of its own for that.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "defs.h"
#include "console.h"
#include "spinlock.h"
#include "board.h"
#include "vm.h"

#define MBOX_READ    0x00
#define MBOX_STATUS  0x18   // of the mailbox we read
#define MBOX_WRITE   0x20
#define MBOX_STATUS1 0x38   // of the one we write
#define MBOX_FULL    0x80000000
#define MBOX_EMPTY   0x40000000
#define CHANNEL      8      // property tags, ARM to VideoCore
#define RESPONSE_OK  0x80000000

#define TAG_GET_CLOCK_RATE 0x00030002

static uint32 buf[256] __attribute__((aligned(64)));
static struct spinlock lock = { .name = "mbox" };

static uint32 rd(uint32 r)
{
    return *(volatile uint32 *)(P2V(board.mbox) + r);
}

static void wr(uint32 r, uint32 v)
{
    *(volatile uint32 *)(P2V(board.mbox) + r) = v;
}

// Make buf visible to the GPU, or the GPU's answer to us.
static void sync_buf(void)
{
    for (uint64 a = (uint64)buf; a < (uint64)buf + sizeof(buf); a += 64)
        dcache_flush_line(a);
}

// Send the message msg (words long, msg[0] its size in bytes) and put
// the answer back in it. Returns 0 if the firmware answered.
int rpi_mbox_call(uint32 *msg, int words)
{
    if (board.mbox == 0 || words > NELEM(buf))
        return -1;
    acquire(&lock);
    memmove(buf, msg, 4 * words);
    uint32 addr = V2P(buf);
    sync_buf();
    while (rd(MBOX_STATUS1) & MBOX_FULL)
        ;
    wr(MBOX_WRITE, addr | CHANNEL);
    for (;;) {
        while (rd(MBOX_STATUS) & MBOX_EMPTY)
            ;
        if (rd(MBOX_READ) == (addr | CHANNEL))
            break;
    }
    sync_buf();
    memmove(msg, buf, 4 * words);
    release(&lock);
    return msg[1] == RESPONSE_OK ? 0 : -1;
}

// The rate in Hz of firmware clock id (12: EMMC2), or 0.
uint32 rpi_clock_rate(uint32 id)
{
    uint32 m[8] = { sizeof(m), 0, TAG_GET_CLOCK_RATE, 8, 0, id, 0, 0 };
    return rpi_mbox_call(m, NELEM(m)) == 0 ? m[6] : 0;
}
