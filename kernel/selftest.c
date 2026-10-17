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
#include "fs.h"
#include "file.h"
#include "stat.h"

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
    initsleeplock(&sl, "selftest");
    for (long i = 0; i < SL_THREADS; i++)
        spawn(kthread_sleeplock, (void *)i);
    for (int i = 0; i < PC_PRODUCERS; i++)
        spawn(kthread_producer, 0);
}

// File system, read-only: list the root directory mkfs made, and read
// README through the inode layer.
static void kthread_fs_read(void *arg) {
    struct dirent de;
    char buf[8];
    int n = 0;

    begin_op();
    struct inode *dp = namei("/");
    if (dp == 0)
        panic("fs test: no root");
    ilock(dp);
    printf("fs: /");
    for (uint off = 0; off < dp->size; off += sizeof(de)) {
        if (readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
            panic("fs test: readi dir");
        if (de.inum == 0)
            continue;
        char name[DIRSIZ + 1];
        memmove(name, de.name, DIRSIZ);
        name[DIRSIZ] = 0;
        printf(" %s", name);
        n++;
    }
    printf("\n");
    iunlockput(dp);

    struct inode *ip = namei("/README");
    if (ip == 0)
        panic("fs test: no /README");
    ilock(ip);
    if (ip->type != T_FILE || readi(ip, 0, (uint64)buf, 0, 4) != 4 ||
        memcmp(buf, "# o2", 4) != 0)
        panic("fs test: README content");
    uint size = ip->size;
    iunlockput(ip);
    end_op();

    printf("fs read ok: %d entries, README %d bytes\n", n, size);
}

// Free data blocks (bitmap) and free inodes, read from disk.
static void fs_count_free(int *blocks, int *inodes) {
    *blocks = *inodes = 0;
    for (uint b = 0; b < sb.size; b++) {
        struct buf *bp = bread(ROOTDEV, BBLOCK(b, sb));
        if ((bp->data[(b % BPB) / 8] & (1 << (b % 8))) == 0)
            (*blocks)++;
        brelse(bp);
    }
    for (uint inum = 1; inum < sb.ninodes; inum++) {
        struct buf *bp = bread(ROOTDEV, IBLOCK(inum, sb));
        if (((struct dinode *)bp->data + inum % IPB)->type == 0)
            (*inodes)++;
        brelse(bp);
    }
}

#define FSW_THREADS 2
#define FSW_BLOCKS  20   // past NDIRECT: needs the indirect block
#define FSW_CHUNK   (3 * BSIZE)   // bytes per transaction

static struct {
    struct spinlock lock;
    int done;
} fsw = { .lock = { .name = "fsw" } };

// Create /ktN, write it in several transactions, read it back, unlink
// it (freeing its inode and blocks in iput()).
static void kthread_fs_write(void *arg) {
    long t = (long)arg;
    char path[] = "/kt0", *name = path + 1;
    uchar *buf = kalloc();
    struct inode *dp, *ip;

    name[2] = '0' + t;
    if (!buf)
        panic("fs write test: kalloc");

    begin_op();
    dp = namei("/");
    ilock(dp);
    if ((ip = ialloc(ROOTDEV, T_FILE)) == 0)
        panic("fs write test: ialloc");
    ilock(ip);
    ip->nlink = 1;
    iupdate(ip);
    if (dirlink(dp, name, ip->inum) < 0)
        panic("fs write test: dirlink %s", name);
    iunlockput(dp);
    iunlock(ip);
    end_op();

    // write FSW_BLOCKS blocks of a pattern, a few blocks per transaction
    for (uint off = 0; off < FSW_BLOCKS * BSIZE; off += FSW_CHUNK) {
        for (int k = 0; k < FSW_CHUNK; k++)
            buf[k] = (uchar)(t * 101 + off + k);
        begin_op();
        ilock(ip);
        if (writei(ip, 0, (uint64)buf, off, FSW_CHUNK) != FSW_CHUNK)
            panic("fs write test: writei");
        iunlock(ip);
        end_op();
    }

    // read it back through a fresh lookup
    begin_op();
    struct inode *ip2 = namei(path);
    if (ip2 != ip)
        panic("fs write test: namei %s", name);
    ilock(ip2);
    if (ip2->size < FSW_BLOCKS * BSIZE)
        panic("fs write test: size %d", ip2->size);
    for (uint off = 0; off < FSW_BLOCKS * BSIZE; off += FSW_CHUNK) {
        if (readi(ip2, 0, (uint64)buf, off, FSW_CHUNK) != FSW_CHUNK)
            panic("fs write test: readi");
        for (int k = 0; k < FSW_CHUNK; k++)
            if (buf[k] != (uchar)(t * 101 + off + k))
                panic("fs write test: %s byte %d", name, off + k);
    }
    iunlockput(ip2);
    end_op();

    // unlink: clear the directory entry, drop the link; the last iput()
    // truncates and frees the inode.
    begin_op();
    uint off;
    struct dirent de;
    dp = namei("/");
    ilock(dp);
    struct inode *victim = dirlookup(dp, name, &off);
    if (victim != ip)
        panic("fs write test: dirlookup %s", name);
    memset(&de, 0, sizeof(de));
    if (writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
        panic("fs write test: unlink writei");
    iunlockput(dp);
    ilock(victim);
    victim->nlink--;
    iupdate(victim);
    iunlockput(victim);
    iput(ip);
    end_op();

    kfree(buf);

    acquire(&fsw.lock);
    fsw.done++;
    wakeup(&fsw.done);
    release(&fsw.lock);
}

// Run the writers concurrently; afterwards the free block and inode
// counts must be back where they started.
static void kthread_fs_writes(void *arg) {
    int blocks0, inodes0, blocks1, inodes1;

    fs_count_free(&blocks0, &inodes0);
    for (long i = 0; i < FSW_THREADS; i++)
        spawn(kthread_fs_write, (void *)i);

    acquire(&fsw.lock);
    while (fsw.done < FSW_THREADS)
        sleep(&fsw.done, &fsw.lock);
    release(&fsw.lock);

    fs_count_free(&blocks1, &inodes1);
    if (blocks1 != blocks0 || inodes1 != inodes0)
        panic("fs write test: free blocks %d -> %d, inodes %d -> %d",
              blocks0, blocks1, inodes0, inodes1);
    printf("fs write ok: %d free blocks, %d free inodes before and after\n",
           blocks0, inodes0);
}

// Called once the file system is up, by the first process in forkret()
// before it enters user mode: nothing else uses the file system until
// these return, so the free counts in the write test are exact.
void selftest_fs(void) {
    kthread_fs_read(0);
    kthread_fs_writes(0);
}
