// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.
//
// From xv6.

#include "types.h"
#include "param.h"
#include "defs.h"
#include "console.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "buf.h"

// Buffers are kept in NBUCKET hash buckets by block number, each with
// its own lock, so that looking up different blocks doesn't contend.
// A lookup that misses takes bcache.evict, so only one at a time
// moves a buffer between buckets; it is the only code that holds two
// bucket locks. The buffer recycled is the free one least recently
// released.
#define NBUCKET 31

static struct {
    struct spinlock evict;
    struct buf buf[NBUF];
    uint64 clock;   // brelse() count, for LRU

    struct {
        struct spinlock lock;
        struct buf head;   // circular list through prev/next
    } bucket[NBUCKET];
} bcache;

static void list_remove(struct buf *b)
{
    b->next->prev = b->prev;
    b->prev->next = b->next;
}

static void list_add(struct buf *head, struct buf *b)
{
    b->next = head->next;
    b->prev = head;
    head->next->prev = b;
    head->next = b;
}

static int hash(uint dev, uint blockno)
{
    return (dev * 7 + blockno) % NBUCKET;
}

void binit(void)
{
    initlock(&bcache.evict, "bcache.evict");
    for (int i = 0; i < NBUCKET; i++) {
        initlock(&bcache.bucket[i].lock, "bcache");
        bcache.bucket[i].head.prev = &bcache.bucket[i].head;
        bcache.bucket[i].head.next = &bcache.bucket[i].head;
    }
    // all buffers start in bucket 0
    for (struct buf *b = bcache.buf; b < bcache.buf + NBUF; b++) {
        initsleeplock(&b->lock, "buffer");
        list_add(&bcache.bucket[0].head, b);
    }
}

// The buffer for block blockno of dev in bucket h, with a reference
// added, or 0. The bucket's lock must be held.
static struct buf *lookup(int h, uint dev, uint blockno)
{
    struct buf *head = &bcache.bucket[h].head;
    for (struct buf *b = head->next; b != head; b = b->next) {
        if (b->dev == dev && b->blockno == blockno) {
            b->refcnt++;
            return b;
        }
    }
    return 0;
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf *bget(uint dev, uint blockno)
{
    int h = hash(dev, blockno);
    struct buf *b;

    acquire(&bcache.bucket[h].lock);
    b = lookup(h, dev, blockno);
    release(&bcache.bucket[h].lock);
    if (b) {
        acquiresleep(&b->lock);
        return b;
    }

    // Not cached. Look again holding evict: someone may have read it
    // in meanwhile.
    acquire(&bcache.evict);
    acquire(&bcache.bucket[h].lock);
    if ((b = lookup(h, dev, blockno)) != 0) {
        release(&bcache.bucket[h].lock);
        release(&bcache.evict);
        acquiresleep(&b->lock);
        return b;
    }

    // Recycle the least recently used free buffer, from any bucket.
    for (;;) {
        struct buf *best = 0;
        int besth = -1;
        for (int i = 0; i < NBUCKET; i++) {
            if (i != h)
                acquire(&bcache.bucket[i].lock);
            struct buf *head = &bcache.bucket[i].head;
            for (struct buf *c = head->next; c != head; c = c->next) {
                if (c->refcnt == 0 && (best == 0 || c->lastuse < best->lastuse)) {
                    best = c;
                    besth = i;
                }
            }
            if (i != h)
                release(&bcache.bucket[i].lock);
        }
        if (best == 0)
            panic("bget: no buffers");
        if (besth != h)
            acquire(&bcache.bucket[besth].lock);
        if (best->refcnt != 0) {
            // looked up meanwhile: pick again
            if (besth != h)
                release(&bcache.bucket[besth].lock);
            continue;
        }
        if (besth != h) {
            list_remove(best);
            release(&bcache.bucket[besth].lock);
            list_add(&bcache.bucket[h].head, best);
        }
        best->dev = dev;
        best->blockno = blockno;
        best->valid = 0;
        best->refcnt = 1;
        release(&bcache.bucket[h].lock);
        release(&bcache.evict);
        acquiresleep(&best->lock);
        return best;
    }
}

// Return a locked buf with the contents of the indicated block.
struct buf *bread(uint dev, uint blockno)
{
    struct buf *b;

    b = bget(dev, blockno);
    if (!b->valid) {
        virtio_disk_rw(b, 0);
        b->valid = 1;
    }
    return b;
}

// Write b's contents to disk.  Must be locked.
void bwrite(struct buf *b)
{
    if (!holdingsleep(&b->lock))
        panic("bwrite");
    virtio_disk_rw(b, 1);
}

// Release a locked buffer; once free, it may be recycled, least
// recently released first.
void brelse(struct buf *b)
{
    if (!holdingsleep(&b->lock))
        panic("brelse");

    releasesleep(&b->lock);

    int h = hash(b->dev, b->blockno);
    acquire(&bcache.bucket[h].lock);
    b->refcnt--;
    if (b->refcnt == 0)
        b->lastuse = __atomic_add_fetch(&bcache.clock, 1, __ATOMIC_RELAXED);
    release(&bcache.bucket[h].lock);
}

void bpin(struct buf *b)
{
    int h = hash(b->dev, b->blockno);
    acquire(&bcache.bucket[h].lock);
    b->refcnt++;
    release(&bcache.bucket[h].lock);
}

void bunpin(struct buf *b)
{
    int h = hash(b->dev, b->blockno);
    acquire(&bcache.bucket[h].lock);
    b->refcnt--;
    release(&bcache.bucket[h].lock);
}
