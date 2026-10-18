//
// Support functions for system calls that involve file descriptors.
// From xv6.
//

#include "types.h"
#include "param.h"
#include "defs.h"
#include "console.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "vm.h"
#include "fs.h"
#include "stat.h"
#include "file.h"
#include "fcntl.h"

struct devsw devsw[NDEV];
static struct {
    struct spinlock lock;
    struct file file[NFILE];
} ftable;

void fileinit(void)
{
    initlock(&ftable.lock, "ftable");
}

// Allocate a file structure.
struct file *filealloc(void)
{
    acquire(&ftable.lock);
    for (struct file *f = ftable.file; f < ftable.file + NFILE; f++) {
        if (f->ref == 0) {
            f->ref = 1;
            release(&ftable.lock);
            return f;
        }
    }
    release(&ftable.lock);
    return 0;
}

// Increment ref count for file f.
struct file *filedup(struct file *f)
{
    acquire(&ftable.lock);
    if (f->ref < 1)
        panic("filedup");
    f->ref++;
    release(&ftable.lock);
    return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
void fileclose(struct file *f)
{
    struct file ff;

    acquire(&ftable.lock);
    if (f->ref < 1)
        panic("fileclose");
    if (--f->ref > 0) {
        release(&ftable.lock);
        return;
    }
    ff = *f;
    f->ref = 0;
    f->type = FD_NONE;
    release(&ftable.lock);

    if (ff.type == FD_PIPE) {
        pipeclose(ff.pipe, ff.writable);
    } else if (ff.type == FD_INODE || ff.type == FD_DEVICE) {
        begin_op();
        iput(ff.ip);
        end_op();
    }
}

// Get metadata about file f.
// addr is a user virtual address, pointing to a struct stat.
int filestat(struct file *f, uint64 addr)
{
    struct proc *p = myproc();
    struct stat st;

    if (f->type == FD_INODE || f->type == FD_DEVICE) {
        ilock(f->ip);
        stati(f->ip, &st);
        iunlock(f->ip);
        if (copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
            return -1;
        return 0;
    }
    return -1;
}

// Read from file f.
// addr is a user virtual address.
int fileread(struct file *f, uint64 addr, int n)
{
    int r = 0;

    if (f->readable == 0)
        return -1;

    if (f->type == FD_PIPE) {
        r = piperead(f->pipe, addr, n);
    } else if (f->type == FD_DEVICE) {
        if (f->major < 0 || f->major >= NDEV || !devsw[f->major].read)
            return -1;
        r = devsw[f->major].read(1, addr, n);
    } else if (f->type == FD_INODE) {
        ilock(f->ip);
        if ((r = readi(f->ip, 1, addr, f->off, n)) > 0)
            f->off += r;
        iunlock(f->ip);
    } else {
        panic("fileread");
    }

    return r;
}

// Set f's offset relative to the start (SEEK_SET), the current offset
// (SEEK_CUR) or the end (SEEK_END), and return it. Only files and
// directories have offsets. Unlike POSIX, the offset can't go past the
// end of the file: as in xv6, writing there fails (usertests'
// truncate2 checks that), so there are no holes.
int fileseek(struct file *f, int off, int whence)
{
    if (f->type != FD_INODE)
        return -1;
    ilock(f->ip);
    long base = whence == SEEK_SET ? 0 :
                whence == SEEK_CUR ? f->off :
                whence == SEEK_END ? f->ip->size : -1;
    long newoff = base + off;
    if (base < 0 || newoff < 0 || newoff > f->ip->size) {
        iunlock(f->ip);
        return -1;
    }
    f->off = newoff;
    iunlock(f->ip);
    return newoff;
}

// Write to file f.
// addr is a user virtual address.
int filewrite(struct file *f, uint64 addr, int n)
{
    int r, ret = 0;

    if (f->writable == 0)
        return -1;

    if (f->type == FD_PIPE) {
        ret = pipewrite(f->pipe, addr, n);
    } else if (f->type == FD_DEVICE) {
        if (f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
            return -1;
        ret = devsw[f->major].write(1, addr, n);
    } else if (f->type == FD_INODE) {
        // write a few blocks at a time to avoid exceeding
        // the maximum log transaction size, including
        // i-node, two indirect blocks, allocation blocks,
        // and 2 blocks of slop for non-aligned writes.
        int max = ((MAXOPBLOCKS-1-2-2) / 2) * BSIZE;
        int i = 0;
        while (i < n) {
            int n1 = n - i;
            if (n1 > max)
                n1 = max;

            begin_op();
            ilock(f->ip);
            if ((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0)
                f->off += r;
            iunlock(f->ip);
            end_op();

            if (r != n1) {
                // error from writei
                break;
            }
            i += r;
        }
        ret = (i == n ? n : -1);
    } else {
        panic("filewrite");
    }

    return ret;
}
