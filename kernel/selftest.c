// Boot-time self tests, run as kernel threads alongside everything else.

#include "types.h"
#include "proc.h"
#include "console.h"
#include "timer.h"
#include "cpu.h"
#include "spinlock.h"
#include "sleeplock.h"

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

static void spawn(void (*fn)(void *), void *arg) {
    if (kthread_create(fn, arg) == 0)
        panic("selftest: kthread_create");
}

// Create the self-test threads. Called once from main() on CPU 0.
void selftest(void) {
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
