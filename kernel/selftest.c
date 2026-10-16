// Boot-time self tests, run as kernel threads alongside everything else.

#include "types.h"
#include "proc.h"
#include "console.h"
#include "timer.h"
#include "cpu.h"

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
}
