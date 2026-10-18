// Simple logging that allows concurrent FS system calls. From xv6.
//
// A log transaction contains the updates of multiple FS system
// calls. The logging system only commits when there are
// no FS system calls active. Thus there is never
// any reasoning required about whether a commit might
// write an uncommitted system call's updates to disk.
//
// A system call should call begin_op()/end_op() to mark
// its start and end. Usually begin_op() just increments
// the count of in-progress FS system calls and returns.
// But if it thinks the log is close to running out, it
// commits, or sleeps until the last outstanding end_op().
//
// Unlike xv6, end_op() doesn't commit: finished system calls
// collect in the log until it is full (begin_op()), a second
// has passed (logflusher()), or sync() asks. Their updates are
// in the buffer cache meanwhile, so everyone sees them; a crash
// loses at most the last second's, never half of one. Blocks
// many calls write (an inode, the bitmap) go to the disk once
// per commit rather than once per call.
//
// The log is a physical re-do log containing disk blocks.
// The on-disk log format:
//   header block, containing block #s for block A, B, C, ...
//   block A
//   block B
//   block C
//   ...
// Log appends are synchronous.

#include "types.h"
#include "param.h"
#include "defs.h"
#include "console.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "fs.h"
#include "buf.h"
#include "timer.h"

// Contents of the header block, used for both the on-disk header block
// and to keep track in memory of logged block# before commit.
struct logheader {
    int n;
    int block[LOGBLOCKS];
};

struct log {
    struct spinlock lock;
    int start;
    int outstanding; // how many FS sys calls are executing.
    int committing;  // in commit(), please wait.
    int ncommit;     // number of completed commits
    int dev;
    struct logheader lh;
};
static struct log log;

static void recover_from_log(void);
static void commit(void);
static void logflusher(void *arg);

void initlog(int dev, struct superblock *sb)
{
    if (sizeof(struct logheader) >= BSIZE)
        panic("initlog: too big logheader");

    initlock(&log.lock, "log");
    log.start = sb->logstart;
    log.dev = dev;
    recover_from_log();
    if (kthread_create(logflusher, 0) == 0)
        panic("initlog: logflusher");
}

// Copy committed blocks from log to their home location
static void install_trans(int recovering)
{
    if (!recovering) {
        // the blocks are pinned in the cache, with what the log holds:
        // write them home from there, all at once
        struct buf *home[LOGBLOCKS];
        for (int tail = 0; tail < log.lh.n; tail++)
            home[tail] = bread(log.dev, log.lh.block[tail]);
        bwritev(home, log.lh.n);
        for (int tail = 0; tail < log.lh.n; tail++) {
            bunpin(home[tail]);
            brelse(home[tail]);
        }
        return;
    }
    for (int tail = 0; tail < log.lh.n; tail++) {
        struct buf *lbuf = bread(log.dev, log.start + tail + 1); // read log block
        struct buf *dbuf = bread(log.dev, log.lh.block[tail]); // read dst
        memmove(dbuf->data, lbuf->data, BSIZE);  // copy block to dst
        bwrite(dbuf);  // write dst to disk
        brelse(lbuf);
        brelse(dbuf);
    }
}

// Read the log header from disk into the in-memory log header
static void read_head(void)
{
    struct buf *buf = bread(log.dev, log.start);
    struct logheader *lh = (struct logheader *)(buf->data);
    log.lh.n = lh->n;
    for (int i = 0; i < log.lh.n; i++)
        log.lh.block[i] = lh->block[i];
    brelse(buf);
}

// Write in-memory log header to disk.
// This is the true point at which the
// current transaction commits.
static void write_head(void)
{
    struct buf *buf = bread(log.dev, log.start);
    struct logheader *hb = (struct logheader *)(buf->data);
    hb->n = log.lh.n;
    for (int i = 0; i < log.lh.n; i++)
        hb->block[i] = log.lh.block[i];
    bwrite(buf);
    brelse(buf);
}

static void recover_from_log(void)
{
    read_head();
    if (log.lh.n > 0)
        printf("log: recovering %d blocks\n", log.lh.n);
    install_trans(1); // if committed, copy from log to disk
    log.lh.n = 0;
    write_head(); // clear the log
}

// called at the start of each FS system call.
// Commit the finished system calls in the log. Called with log.lock
// held, no system call in progress and none committing; releases the
// lock while writing to the disk.
static void commit_locked(void)
{
    log.committing = 1;
    release(&log.lock);
    commit();
    acquire(&log.lock);
    log.committing = 0;
    log.ncommit++;
    wakeup(&log);
}

void begin_op(void)
{
    acquire(&log.lock);
    while (1) {
        if (log.committing) {
            sleep(&log, &log.lock);
        } else if (log.lh.n + (log.outstanding + 1) * MAXOPBLOCKS > LOGBLOCKS) {
            // this op might exhaust log space: commit what is there,
            // or wait for the ops in progress to end first.
            if (log.outstanding == 0)
                commit_locked();
            else
                sleep(&log, &log.lock);
        } else {
            log.outstanding += 1;
            release(&log.lock);
            break;
        }
    }
}

// called at the end of each FS system call.
// commits if this was the last outstanding operation.
void end_op(void)
{
    acquire(&log.lock);
    log.outstanding -= 1;
    if (log.committing)
        panic("log.committing");
    // begin_op() may be waiting for log space, and decrementing
    // log.outstanding has decreased the amount of reserved space;
    // sync() may be waiting for no op to be in progress.
    wakeup(&log);
    release(&log.lock);
}

// Commit whatever the log holds, once no op is in progress.
static void flush(void)
{
    acquire(&log.lock);
    while (log.committing || log.outstanding > 0)
        sleep(&log, &log.lock);
    if (log.lh.n > 0)
        commit_locked();
    release(&log.lock);
}

// Kernel thread: commit at least once a second.
static void logflusher(void *arg)
{
    for (;;) {
        sleep_ticks(HZ);
        flush();
    }
}

// Copy modified blocks from cache to log.
static void write_log(void)
{
    struct buf *to[LOGBLOCKS];

    for (int tail = 0; tail < log.lh.n; tail++) {
        to[tail] = bclaim(log.dev, log.start + tail + 1); // log block
        struct buf *from = bread(log.dev, log.lh.block[tail]); // cache block
        memmove(to[tail]->data, from->data, BSIZE);
        brelse(from);
    }
    bwritev(to, log.lh.n);  // write the log, all at once
    for (int tail = 0; tail < log.lh.n; tail++)
        brelse(to[tail]);
}

static void commit(void)
{
    if (log.lh.n > 0) {
        write_log();     // Write modified blocks from cache to log
        write_head();    // Write header to disk -- the real commit
        install_trans(0); // Now install writes to home locations
        log.lh.n = 0;
        write_head();    // Erase the transaction from the log
    }
}

// Caller has modified b->data and is done with the buffer.
// Record the block number and pin in the cache by increasing refcnt.
// commit()/write_log() will do the disk write.
//
// log_write() replaces bwrite(); a typical use is:
//   bp = bread(...)
//   modify bp->data[]
//   log_write(bp)
//   brelse(bp)
void log_write(struct buf *b)
{
    int i;

    acquire(&log.lock);
    if (log.lh.n >= LOGBLOCKS)
        panic("too big a transaction");
    if (log.outstanding < 1)
        panic("log_write outside of trans");

    for (i = 0; i < log.lh.n; i++) {
        if (log.lh.block[i] == b->blockno)   // log absorption
            break;
    }
    log.lh.block[i] = b->blockno;
    if (i == log.lh.n) {  // Add new block to log?
        bpin(b);
        log.lh.n++;
    }
    release(&log.lock);
}

// sync(): commit everything so far to the disk. Returns 0.
uint64 sys_sync(void)
{
    flush();
    return 0;
}
