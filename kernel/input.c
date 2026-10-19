// The input device: events from keyboard and pointer drivers queue
// here, for whoever reads the "input" device (the desktop).

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "defs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "fs.h"
#include "file.h"
#include "input.h"

#define NEVENT 256   // a power of two

static struct {
    struct spinlock lock;
    struct input_event ev[NEVENT];
    uint r, w;       // read and write counts; full when w - r == NEVENT
} q = { .lock = { .name = "input" } };

// A driver saw an event (interrupt context). Dropped if nobody read the
// last NEVENT.
void input_report(int type, int code, int value)
{
    acquire(&q.lock);
    if (q.w - q.r < NEVENT) {
        q.ev[q.w % NEVENT] = (struct input_event){ type, code, value };
        q.w++;
        wakeup(&q.r);
    }
    release(&q.lock);
}

// read(): whole events into dst, up to n bytes; waits for at least one
// unless nonblock (-1 then if there is none).
static int inputread(int user_dst, uint64 dst, int n, int nonblock)
{
    int got = 0;

    acquire(&q.lock);
    while (q.r == q.w) {
        if (nonblock || interrupted(myproc())) {
            release(&q.lock);
            return -1;
        }
        sleep(&q.r, &q.lock);
    }
    while (q.r != q.w && n - got >= sizeof(struct input_event)) {
        struct input_event e = q.ev[q.r % NEVENT];
        // copyout may sleep (lazy pages): not with the spinlock held
        release(&q.lock);
        if (either_copyout(user_dst, dst + got, &e, sizeof(e)) < 0)
            return got ? got : -1;
        acquire(&q.lock);
        q.r++;
        got += sizeof(e);
    }
    release(&q.lock);
    return got;
}

void inputinit(void)
{
    devsw[INPUT].read = inputread;
}
