// The disk the file system is on: a virtio block device (qemu virt), or
// an SD card (the Raspberry Pi), where it is the partition of type
// O2_PARTITION in the card's MBR, so that the card can also hold the
// firmware's FAT partition. Blocks are BSIZE; the devices' sectors 512.

#include "types.h"
#include "param.h"
#include "defs.h"
#include "console.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

#define O2_PARTITION 0x6f   // 'o'
#define SPB (BSIZE / 512)   // sectors per block

static int sdcard;          // the file system is on the SD card
static uint64 start;        // ... from this sector

static char mbr[512];

// Called once, by the first process (the SD card's setup sleeps).
void disk_init(void)
{
    uint64 sectors;

    if (virtio_disk_present() || sd_init(&sectors) < 0)
        return;
    if (sd_rw(0, mbr, 0) < 0)
        panic("disk_init: can't read the SD card");
    if ((uchar)mbr[510] == 0x55 && (uchar)mbr[511] == 0xaa) {
        for (int i = 0; i < 4; i++) {
            uchar *e = (uchar *)mbr + 446 + 16 * i;
            if (e[4] == O2_PARTITION) {
                start = e[8] | e[9] << 8 | e[10] << 16 | (uint64)e[11] << 24;
                break;
            }
        }
    }
    if (start == 0)
        panic("disk_init: no o2 partition (type 0x%x) on the SD card", O2_PARTITION);
    printf("sd: o2 partition at sector %lu\n", start);
    sdcard = 1;
}

// Read (write = 0) or write b->blockno from/to b->data.
void disk_rw(struct buf *b, int write)
{
    disk_rwv(&b, 1, write);
}

// The same for n buffers, as one batch if the device can.
void disk_rwv(struct buf **bufs, int n, int write)
{
    if (!sdcard) {
        virtio_disk_rwv(bufs, n, write);
        return;
    }
    for (int i = 0; i < n; i++)
        for (int s = 0; s < SPB; s++)
            if (sd_rw(start + bufs[i]->blockno * SPB + s, (char *)bufs[i]->data + 512 * s, write) < 0)
                panic("disk: SD card %s error, block %d", write ? "write" : "read", bufs[i]->blockno);
}
