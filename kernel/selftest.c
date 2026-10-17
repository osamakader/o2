// Boot-time self tests, run as kernel threads alongside everything else.

#include "types.h"
#include "proc.h"
#include "console.h"
#include "timer.h"
#include "cpu.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "defs.h"
#include "vm.h"
#include "memlayout.h"
#include "buf.h"

// Two threads that take turns via yield() to exercise the scheduler.
static void kthread_test(void *arg) {
    const char *name = arg;
    for (int i = 0; i < 3; i++) {
        printf("%s iteration %d on cpu %d\n", name, i, cpuid());
        yield();
    }
    printf("%s exiting\n", name);
}

// Busy-waits without ever calling yield(), so it only makes progress
// alongside another spinner if the timer preempts it.
static void kthread_spin(void *arg) {
    const char *name = arg;
    for (int i = 0; i < 3; i++) {
        uint64 until = *(volatile uint64 *)&ticks + 10;
        while (*(volatile uint64 *)&ticks < until)
            ;
        printf("%s spin %d on cpu %d\n", name, i, cpuid());
    }
}

// Checks that sleep_ticks() blocks for at least as long as asked.
static void kthread_sleeper(void *arg) {
    const char *name = arg;
    for (int i = 0; i < 3; i++) {
        uint64 t0 = *(volatile uint64 *)&ticks;
        sleep_ticks(20);
        uint64 dt = *(volatile uint64 *)&ticks - t0;
        if (dt < 20)
            panic("%s: sleep_ticks(20) returned after %lu ticks", name, dt);
        printf("%s slept %lu ticks on cpu %d\n", name, dt, cpuid());
    }
}

// Bounded buffer with a single slot, guarded by a spinlock. Producers
// sleep while it is full and the consumer sleeps while it is empty, so
// nearly every handoff goes through sleep()/wakeup(). A lost wakeup
// leaves both sides asleep and "pc done" never shows.
#define PC_PRODUCERS 3
#define PC_ITEMS     10000
#define PC_SLOTS     1

static struct {
    struct spinlock lock;
    int avail;
    int psleeps;   // times a producer slept on a full buffer
} pc = { .lock = { .name = "pc" } };

static void kthread_producer(void *arg) {
    for (int i = 0; i < PC_ITEMS; i++) {
        acquire(&pc.lock);
        while (pc.avail == PC_SLOTS) {
            pc.psleeps++;
            sleep(&pc.avail, &pc.lock);
        }
        pc.avail++;
        wakeup(&pc.avail);
        release(&pc.lock);
    }
}

static void kthread_consumer(void *arg) {
    int sleeps = 0;

    for (int got = 0; got < PC_PRODUCERS * PC_ITEMS; got++) {
        acquire(&pc.lock);
        while (pc.avail == 0) {
            sleeps++;
            sleep(&pc.avail, &pc.lock);
        }
        pc.avail--;
        wakeup(&pc.avail);
        release(&pc.lock);
    }

    acquire(&pc.lock);
    int psleeps = pc.psleeps;
    release(&pc.lock);
    printf("pc done: %d items, consumer slept %d times, producers %d times\n",
           PC_PRODUCERS * PC_ITEMS, sleeps, psleeps);
}

// Threads take a sleeplock and hold it across sleep_ticks(), as a
// driver caller would across disk I/O. Two holders at once would show
// up as inside != 1.
#define SL_THREADS 3
#define SL_ROUNDS  3

static struct sleeplock sl;
static int sl_inside;   // holders inside the critical section

static void kthread_sleeplock(void *arg) {
    for (int i = 0; i < SL_ROUNDS; i++) {
        acquiresleep(&sl);
        if (!holdingsleep(&sl))
            panic("sleeplock: not holding after acquiresleep");
        int inside = __atomic_add_fetch(&sl_inside, 1, __ATOMIC_SEQ_CST);
        if (inside != 1)
            panic("sleeplock: %d holders", inside);
        sleep_ticks(2);
        __atomic_sub_fetch(&sl_inside, 1, __ATOMIC_SEQ_CST);
        releasesleep(&sl);
    }
    printf("sleeplock thread %ld done on cpu %d\n", (long)arg, cpuid());
}

// User page-table helpers on a scratch address space: grow, copy in
// and out across page boundaries, fork-style copy, shrink, free, and
// no pages leaked. The leak check counts free pages globally, so this
// runs from main() before anything else can allocate.
static void uvm_selftest(void) {
    uint64 free0 = kfreepages();
    char buf[64], out[64];

    pagetable_t pt = uvmcreate();
    uint64 sz = uvmalloc(pt, 0, 3 * PGSIZE + 100, PERM_W);
    if (pt == 0 || sz != 3 * PGSIZE + 100)
        panic("uvm: alloc");

    // straddle the first page boundary
    safestrcpy(buf, "hello, user memory", sizeof(buf));
    if (copyout(pt, PGSIZE - 5, buf, strlen(buf) + 1) != 0)
        panic("uvm: copyout");
    if (copyinstr(pt, out, PGSIZE - 5, sizeof(out)) != 0 || strncmp(buf, out, sizeof(out)) != 0)
        panic("uvm: copyinstr");

    // beyond sz (page 4 was never mapped)
    if (copyout(pt, 4 * PGSIZE, buf, 1) == 0 || copyin(pt, out, 4 * PGSIZE, 1) == 0)
        panic("uvm: access past sz succeeded");

    // fork-style copy keeps contents
    pagetable_t pt2 = uvmcreate();
    if (uvmcopy(pt, pt2, sz) != 0)
        panic("uvm: uvmcopy");
    memset(out, 0, sizeof(out));
    if (copyin(pt2, out, PGSIZE - 5, strlen(buf) + 1) != 0 || strncmp(buf, out, sizeof(out)) != 0)
        panic("uvm: copy contents");

    // a cleared (guard) page rejects user access
    uvmclear(pt2, 0);
    if (copyin(pt2, out, 0, 1) == 0)
        panic("uvm: guard page readable");

    // shrink to one page, then free everything
    sz = uvmdealloc(pt, sz, PGSIZE);
    if (copyin(pt, out, PGSIZE, 1) == 0)
        panic("uvm: dealloc'd page still mapped");
    uvmfree(pt, sz);
    uvmfree(pt2, 3 * PGSIZE + 100);

    uint64 free1 = kfreepages();
    if (free1 != free0)
        panic("uvm: leaked %ld pages", (long)(free0 - free1));
    printf("uvm ok\n");
}

// Concurrent disk I/O: each thread owns a range of blocks; for each it
// saves the contents, writes a pattern unique to (thread, block), reads
// it back into another buffer, compares, and restores the original, so
// the disk image ends up unchanged.
#define DISK_THREADS 4
#define DISK_BLOCKS  20

static void kthread_disk(void *arg) {
    long t = (long)arg;
    struct buf *orig = kalloc(), *pat = kalloc(), *back = kalloc();

    if (!orig || !pat || !back)
        panic("disk test: kalloc");

    for (int i = 0; i < DISK_BLOCKS; i++) {
        uint blockno = 100 + t * DISK_BLOCKS + i;
        orig->blockno = pat->blockno = back->blockno = blockno;

        virtio_disk_rw(orig, 0);

        for (int k = 0; k < BSIZE; k++)
            pat->data[k] = (uchar)(t * 31 + blockno * 7 + k);
        virtio_disk_rw(pat, 1);

        memset(back->data, 0, BSIZE);
        virtio_disk_rw(back, 0);
        if (memcmp(back->data, pat->data, BSIZE) != 0)
            panic("disk test: block %d read back wrong", blockno);

        virtio_disk_rw(orig, 1);
    }

    kfree(orig);
    kfree(pat);
    kfree(back);
    printf("disk thread %ld ok on cpu %d\n", t, cpuid());
}

// Buffer cache: two threads read 100 blocks, more than NBUF, in
// opposite orders through bread() and compare each with a direct disk
// read, so buffers get evicted and shared while in use.
#define BIO_THREADS 2
#define BIO_BLOCKS  100

static void kthread_bio(void *arg) {
    long t = (long)arg;
    struct buf *direct = kalloc();

    if (!direct)
        panic("bio test: kalloc");
    for (int i = 0; i < BIO_BLOCKS; i++) {
        uint blockno = t == 0 ? i : BIO_BLOCKS - 1 - i;
        struct buf *b = bread(ROOTDEV, blockno);
        direct->blockno = blockno;
        virtio_disk_rw(direct, 0);
        if (b->blockno != blockno || memcmp(b->data, direct->data, BSIZE) != 0)
            panic("bio test: block %d differs", blockno);
        brelse(b);
    }
    kfree(direct);
    printf("bio thread %ld ok on cpu %d\n", t, cpuid());
}

static void spawn(void (*fn)(void *), void *arg) {
    if (kthread_create(fn, arg) == 0)
        panic("selftest: kthread_create");
}

// Run the synchronous tests and create the self-test threads. Called
// once from main() on CPU 0, before any CPU runs the scheduler.
void selftest(void) {
    uvm_selftest();

    spawn(kthread_test, "A");
    spawn(kthread_test, "B");
    spawn(kthread_spin, "C");
    spawn(kthread_spin, "D");
    spawn(kthread_sleeper, "E");
    spawn(kthread_sleeper, "F");
    spawn(kthread_consumer, 0);
    if (virtio_disk_present()) {
        for (long i = 0; i < DISK_THREADS; i++)
            spawn(kthread_disk, (void *)i);
        for (long i = 0; i < BIO_THREADS; i++)
            spawn(kthread_bio, (void *)i);
    }
    initsleeplock(&sl, "selftest");
    for (long i = 0; i < SL_THREADS; i++)
        spawn(kthread_sleeplock, (void *)i);
    for (int i = 0; i < PC_PRODUCERS; i++)
        spawn(kthread_producer, 0);
}
