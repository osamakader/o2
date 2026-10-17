// User-mode tests: processes, memory, faults, files, directories and
// pipes. Run from the shell: usertests

#include "types.h"
#include "user.h"
#include "stat.h"
#include "fcntl.h"
#include "procinfo.h"

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

// Create, write, read back, unlink.
static void test_files(void)
{
  char buf[100];
  int fd = open("tf", O_CREATE | O_RDWR);
  if(fd < 0){
    fail("files", "create");
    return;
  }
  for(int i = 0; i < 50; i++)
    if(write(fd, "0123456789", 10) != 10)
      fail("files", "write");
  close(fd);

  struct stat st;
  if(stat("tf", &st) < 0 || st.type != T_FILE || st.size != 500)
    fail("files", "stat");

  fd = open("tf", O_RDONLY);
  int total = 0, n;
  while((n = read(fd, buf, sizeof(buf))) > 0){
    for(int i = 0; i < n; i++)
      if(buf[i] != '0' + (total + i) % 10)
        fail("files", "content");
    total += n;
  }
  close(fd);
  if(total != 500)
    fail("files", "read size");
  if(unlink("tf") < 0 || open("tf", O_RDONLY) >= 0)
    fail("files", "unlink");
}

// mkdir, chdir, relative paths, link, and non-empty directories.
static void test_dirs(void)
{
  if(mkdir("td") < 0 || chdir("td") < 0){
    fail("dirs", "mkdir/chdir");
    return;
  }
  int fd = open("f", O_CREATE | O_WRONLY);
  if(fd < 0 || write(fd, "x", 1) != 1)
    fail("dirs", "create in dir");
  close(fd);
  if(chdir("..") < 0 || open("td/f", O_RDONLY) < 0)
    fail("dirs", "relative open");
  if(link("td/f", "td/g") < 0 || unlink("td/f") < 0 || open("td/g", O_RDONLY) < 0)
    fail("dirs", "link");
  if(unlink("td") == 0)
    fail("dirs", "unlinked a non-empty directory");
  if(unlink("td/g") < 0 || unlink("td") < 0)
    fail("dirs", "cleanup");
  // the open descriptors from the checks above
  for(int i = 3; i < 16; i++)
    close(i);
}

// A child writes through a pipe; the parent reads it all and sees EOF.
static void test_pipe(void)
{
  int fds[2];
  if(pipe(fds) < 0){
    fail("pipe", "pipe");
    return;
  }
  int pid = fork();
  if(pid == 0){
    close(fds[0]);
    for(int i = 0; i < 100; i++)
      write(fds[1], "abcdefghij", 10);
    exit(0);
  }
  close(fds[1]);
  char buf[64];
  int total = 0, n;
  while((n = read(fds[0], buf, sizeof(buf))) > 0){
    for(int i = 0; i < n; i++)
      if(buf[i] != 'a' + (total + i) % 10)
        fail("pipe", "content");
    total += n;
  }
  close(fds[0]);
  wait(0);
  if(total != 1000)
    fail("pipe", "length");
}

// getprocs() reports this process running and a sleeping child.
static void test_ps(void)
{
  static struct procinfo procs[64];
  int me = getpid(), seen_me = 0, seen_child = 0;

  // The child blocks reading an empty pipe: unlike sleep(), which
  // wakes on every tick to recheck, nothing wakes it until we close
  // the write end.
  int fds[2];
  if(pipe(fds) < 0){
    fail("ps", "pipe");
    return;
  }
  int pid = fork();
  if(pid == 0){
    char c;
    close(fds[1]);
    read(fds[0], &c, 1);
    exit(0);
  }
  close(fds[0]);
  sleep(5);  // let the child block

  int n = getprocs(procs, 64);
  if(n <= 0)
    fail("ps", "getprocs");
  for(int i = 0; i < n; i++){
    if(procs[i].pid == me){
      seen_me = 1;
      if(procs[i].state != PS_RUNNING || procs[i].kthread || strcmp(procs[i].name, "usertests") != 0)
        fail("ps", "self");
    }
    if(procs[i].pid == pid){
      seen_child = 1;
      if(procs[i].state != PS_SLEEPING || procs[i].ppid != me)
        fail("ps", "child");
    }
  }
  if(!seen_me || !seen_child)
    fail("ps", "missing");
  if(getprocs((struct procinfo *)0x7000000000UL, 64) != -1)
    fail("ps", "bad buffer");

  close(fds[1]);  // EOF for the child
  wait(0);
}

static void usertests(void)
{
  struct { const char *name; void (*fn)(void); } tests[] = {
    { "forkwait", test_forkwait },
    { "forkloop", test_forkloop },
    { "faults", test_faults },
    { "badptr", test_badptr },
    { "kill", test_kill },
    { "files", test_files },
    { "dirs", test_dirs },
    { "pipe", test_pipe },
    { "ps", test_ps },
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
  usertests();
  exit(failed);
}
