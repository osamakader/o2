// The first user program. Until there is a file system (and exec),
// it also carries the user-mode tests, then echoes console input.

#include "types.h"
#include "user.h"

#define PGSIZE 4096

static int failed;

static void fail(const char *test, const char *why)
{
  printf("usertests: %s FAILED: %s\n", test, why);
  failed = 1;
}

// Run fn in a child and return its exit status (-1 if it was killed).
static int run_child(void (*fn)(void))
{
  int pid = fork();
  if(pid < 0)
    return -2;
  if(pid == 0){
    fn();
    exit(0);
  }
  int status = -3;
  if(wait(&status) != pid)
    return -4;
  return status;
}

// Many children at once, each with its own exit status.
static void test_forkwait(void)
{
  enum { N = 20 };
  int seen[N] = { 0 };

  for(int i = 0; i < N; i++){
    int pid = fork();
    if(pid < 0){
      fail("forkwait", "fork");
      return;
    }
    if(pid == 0)
      exit(100 + i);
  }
  for(int i = 0; i < N; i++){
    int status;
    if(wait(&status) < 0){
      fail("forkwait", "wait");
      return;
    }
    if(status < 100 || status >= 100 + N || seen[status - 100]++){
      fail("forkwait", "bad status");
      return;
    }
  }
  if(wait(0) != -1)
    fail("forkwait", "extra child");
}

// Process slots and memory are reclaimed: far more forks than NPROC.
static void test_forkloop(void)
{
  for(int i = 0; i < 200; i++){
    int pid = fork();
    if(pid < 0){
      fail("forkloop", "fork ran out");
      return;
    }
    if(pid == 0)
      exit(0);
    if(wait(0) != pid){
      fail("forkloop", "wait");
      return;
    }
  }
}

static void store_to_text(void)
{
  *(volatile int *)run_child = 0;   // text is read-only
}

static void load_past_sz(void)
{
  volatile char *p = sbrk(0) + 10 * PGSIZE;
  (void)*p;
}

static void touch_freed_heap(void)
{
  char *p = sbrk(PGSIZE);
  p[0] = 1;
  sbrk(-PGSIZE);
  p[0] = 2;
}

static int recurse(int n)
{
  volatile char buf[256];
  buf[0] = n;
  if(n < 1000000)
    return recurse(n + 1) + buf[0];
  return 0;
}

static void overflow_stack(void)
{
  recurse(0);
}

// Faulting children are killed (status -1); the parent carries on.
static void test_faults(void)
{
  struct { const char *name; void (*fn)(void); } cases[] = {
    { "store to text", store_to_text },
    { "load past sz", load_past_sz },
    { "freed heap", touch_freed_heap },
    { "stack overflow", overflow_stack },
  };
  for(int i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    if(run_child(cases[i].fn) != -1)
      fail("faults", cases[i].name);
}

// Bad pointers make syscalls fail; they don't kill the caller.
static void test_badptr(void)
{
  if(write(1, (void *)0x7000000000UL, 10) != 0)
    fail("badptr", "write from unmapped memory");
  if(write(1, (void *)0, 0) != 0)
    fail("badptr", "zero-length write");
  if(wait((int *)run_child) != -1)   // read-only status address
    ;   // no children: -1 either way; must not kill us
}

static void spin(void)
{
  for(;;)
    ;
}

// A child spinning in user mode is preempted and killed by the timer
// interrupt path.
static void test_kill(void)
{
  int pid = fork();
  if(pid == 0)
    spin();
  sleep(5);
  if(kill(pid) != 0)
    fail("kill", "kill");
  int status;
  if(wait(&status) != pid || status != -1)
    fail("kill", "status");
}

static void usertests(void)
{
  struct { const char *name; void (*fn)(void); } tests[] = {
    { "forkwait", test_forkwait },
    { "forkloop", test_forkloop },
    { "faults", test_faults },
    { "badptr", test_badptr },
    { "kill", test_kill },
  };
  for(int i = 0; i < sizeof(tests) / sizeof(tests[0]); i++){
    int before = failed;
    tests[i].fn();
    printf("usertests: %s %s\n", tests[i].name, failed == before ? "ok" : "FAILED");
  }
  printf(failed ? "usertests: SOME TESTS FAILED\n" : "usertests: ALL TESTS PASSED\n");
}

int main(void)
{
  printf("init: starting, pid %d\n", getpid());

  usertests();

  // no shell yet: echo console lines back until end of file, then
  // just reap orphans
  char line[64];
  for(;;){
    int n = read(0, line, sizeof(line) - 1);
    if(n <= 0)
      break;
    if(line[n-1] == '\n')
      n--;
    line[n] = 0;
    printf("init: got \"%s\"\n", line);
  }
  printf("init: console eof\n");
  for(;;){
    if(wait(0) < 0)
      sleep(100);
  }
}
