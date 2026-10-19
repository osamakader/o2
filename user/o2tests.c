// User-mode tests: processes, memory, faults, files, directories and
// pipes. Run from the shell: o2tests

#include "types.h"
#include "user.h"
#include "stat.h"
#include "fcntl.h"
#include "procinfo.h"
#include "syscall.h"
#include "time.h"
#include "mman.h"
#include "lockstat.h"
#include "socket.h"
#include "fb.h"

#define PGSIZE 4096

static int failed;

static void fail(const char *test, const char *why)
{
  printf("o2tests: %s FAILED: %s\n", test, why);
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

// the start of kernel text on each arch
#if defined(__aarch64__)
#define KERNEL_TEXT 0xffffffc040200000UL
#else
#define KERNEL_TEXT 0xffffffc080200000UL
#endif

static void read_kernel(void)
{
  (void)*(volatile char *)KERNEL_TEXT;
}

// Faulting children are killed (status -1); the parent carries on.
static void test_faults(void)
{
  struct { const char *name; void (*fn)(void); } cases[] = {
    { "store to text", store_to_text },
    { "load past sz", load_past_sz },
    { "freed heap", touch_freed_heap },
    { "stack overflow", overflow_stack },
    { "read kernel text", read_kernel },
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
  pause(5);
  if(kill(pid, SIGKILL) != 0)
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

  // The child blocks reading an empty pipe: unlike pause(), which
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
  pause(5);  // let the child block

  int n = getprocs(procs, 64);
  if(n <= 0)
    fail("ps", "getprocs");
  for(int i = 0; i < n; i++){
    if(procs[i].pid == me){
      seen_me = 1;
      if(procs[i].state != PS_RUNNING || procs[i].kthread || strcmp(procs[i].name, "o2tests") != 0)
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

// klog() returns the kernel's boot messages, oldest first, but not
// what user programs wrote to the console.
static void test_klog(void)
{
  static char buf[16384];
  printf("o2tests: user output, not for klog\n");
  int n = klog(buf, sizeof(buf) - 1);
  if(n <= 0){
    fail("klog", "klog");
    return;
  }
  buf[n] = 0;
  char *boot = 0, *disk = 0;
  for(int i = 0; i < n; i++){
    if(!boot && memcmp(buf + i, "starting kernel", 15) == 0)
      boot = buf + i;
    if(!disk && memcmp(buf + i, "virtio_disk:", 12) == 0)
      disk = buf + i;
    if(memcmp(buf + i, "not for klog", 12) == 0)
      fail("klog", "user output in the kernel log");
  }
  if(!boot || !disk || disk < boot)
    fail("klog", "boot messages missing or out of order");
  if(klog(buf, 10) != 10 || klog(buf, -1) != -1)
    fail("klog", "sizes");
  if(klog((char *)0x7000000000UL, 10) != -1)
    fail("klog", "bad buffer");
}


// FP/SIMD register tests. Each runs in one asm block, so the compiler
// can't move values between FP registers behind our back: fill all 32
// FP registers (both halves on arm64) with a pattern from seed, do
// something that may switch processes (a syscall, or spinning long
// enough to be preempted), then count registers that changed.

#if defined(__aarch64__)
#define FP_FILL \
    "fmov d0, x9\n" "add x9, x9, #1\n" "ins v0.d[1], x9\n" "add x9, x9, #1\n" \
    "fmov d1, x9\n" "add x9, x9, #1\n" "ins v1.d[1], x9\n" "add x9, x9, #1\n" \
    "fmov d2, x9\n" "add x9, x9, #1\n" "ins v2.d[1], x9\n" "add x9, x9, #1\n" \
    "fmov d3, x9\n" "add x9, x9, #1\n" "ins v3.d[1], x9\n" "add x9, x9, #1\n" \
    "fmov d4, x9\n" "add x9, x9, #1\n" "ins v4.d[1], x9\n" "add x9, x9, #1\n" \
    "fmov d5, x9\n" "add x9, x9, #1\n" "ins v5.d[1], x9\n" "add x9, x9, #1\n" \
    "fmov d6, x9\n" "add x9, x9, #1\n" "ins v6.d[1], x9\n" "add x9, x9, #1\n" \
    "fmov d7, x9\n" "add x9, x9, #1\n" "ins v7.d[1], x9\n" "add x9, x9, #1\n" \
    "fmov d8, x9\n" "add x9, x9, #1\n" "ins v8.d[1], x9\n" "add x9, x9, #1\n" \
    "fmov d9, x9\n" "add x9, x9, #1\n" "ins v9.d[1], x9\n" "add x9, x9, #1\n" \
    "fmov d10, x9\n" "add x9, x9, #1\n" "ins v10.d[1], x9\n" "add x9, x9, #1\n" \
    "fmov d11, x9\n" "add x9, x9, #1\n" "ins v11.d[1], x9\n" "add x9, x9, #1\n" \
    "fmov d12, x9\n" "add x9, x9, #1\n" "ins v12.d[1], x9\n" "add x9, x9, #1\n" \
    "fmov d13, x9\n" "add x9, x9, #1\n" "ins v13.d[1], x9\n" "add x9, x9, #1\n" \
    "fmov d14, x9\n" "add x9, x9, #1\n" "ins v14.d[1], x9\n" "add x9, x9, #1\n" \
    "fmov d15, x9\n" "add x9, x9, #1\n" "ins v15.d[1], x9\n" "add x9, x9, #1\n" \
    "fmov d16, x9\n" "add x9, x9, #1\n" "ins v16.d[1], x9\n" "add x9, x9, #1\n" \
    "fmov d17, x9\n" "add x9, x9, #1\n" "ins v17.d[1], x9\n" "add x9, x9, #1\n" \
    "fmov d18, x9\n" "add x9, x9, #1\n" "ins v18.d[1], x9\n" "add x9, x9, #1\n" \
    "fmov d19, x9\n" "add x9, x9, #1\n" "ins v19.d[1], x9\n" "add x9, x9, #1\n" \
    "fmov d20, x9\n" "add x9, x9, #1\n" "ins v20.d[1], x9\n" "add x9, x9, #1\n" \
    "fmov d21, x9\n" "add x9, x9, #1\n" "ins v21.d[1], x9\n" "add x9, x9, #1\n" \
    "fmov d22, x9\n" "add x9, x9, #1\n" "ins v22.d[1], x9\n" "add x9, x9, #1\n" \
    "fmov d23, x9\n" "add x9, x9, #1\n" "ins v23.d[1], x9\n" "add x9, x9, #1\n" \
    "fmov d24, x9\n" "add x9, x9, #1\n" "ins v24.d[1], x9\n" "add x9, x9, #1\n" \
    "fmov d25, x9\n" "add x9, x9, #1\n" "ins v25.d[1], x9\n" "add x9, x9, #1\n" \
    "fmov d26, x9\n" "add x9, x9, #1\n" "ins v26.d[1], x9\n" "add x9, x9, #1\n" \
    "fmov d27, x9\n" "add x9, x9, #1\n" "ins v27.d[1], x9\n" "add x9, x9, #1\n" \
    "fmov d28, x9\n" "add x9, x9, #1\n" "ins v28.d[1], x9\n" "add x9, x9, #1\n" \
    "fmov d29, x9\n" "add x9, x9, #1\n" "ins v29.d[1], x9\n" "add x9, x9, #1\n" \
    "fmov d30, x9\n" "add x9, x9, #1\n" "ins v30.d[1], x9\n" "add x9, x9, #1\n" \
    "fmov d31, x9\n" "add x9, x9, #1\n" "ins v31.d[1], x9\n" "add x9, x9, #1\n"
#define FP_CHECK \
    "fmov x10, d0\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" "umov x10, v0.d[1]\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" \
    "fmov x10, d1\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" "umov x10, v1.d[1]\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" \
    "fmov x10, d2\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" "umov x10, v2.d[1]\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" \
    "fmov x10, d3\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" "umov x10, v3.d[1]\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" \
    "fmov x10, d4\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" "umov x10, v4.d[1]\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" \
    "fmov x10, d5\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" "umov x10, v5.d[1]\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" \
    "fmov x10, d6\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" "umov x10, v6.d[1]\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" \
    "fmov x10, d7\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" "umov x10, v7.d[1]\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" \
    "fmov x10, d8\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" "umov x10, v8.d[1]\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" \
    "fmov x10, d9\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" "umov x10, v9.d[1]\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" \
    "fmov x10, d10\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" "umov x10, v10.d[1]\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" \
    "fmov x10, d11\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" "umov x10, v11.d[1]\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" \
    "fmov x10, d12\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" "umov x10, v12.d[1]\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" \
    "fmov x10, d13\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" "umov x10, v13.d[1]\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" \
    "fmov x10, d14\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" "umov x10, v14.d[1]\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" \
    "fmov x10, d15\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" "umov x10, v15.d[1]\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" \
    "fmov x10, d16\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" "umov x10, v16.d[1]\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" \
    "fmov x10, d17\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" "umov x10, v17.d[1]\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" \
    "fmov x10, d18\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" "umov x10, v18.d[1]\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" \
    "fmov x10, d19\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" "umov x10, v19.d[1]\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" \
    "fmov x10, d20\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" "umov x10, v20.d[1]\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" \
    "fmov x10, d21\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" "umov x10, v21.d[1]\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" \
    "fmov x10, d22\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" "umov x10, v22.d[1]\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" \
    "fmov x10, d23\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" "umov x10, v23.d[1]\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" \
    "fmov x10, d24\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" "umov x10, v24.d[1]\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" \
    "fmov x10, d25\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" "umov x10, v25.d[1]\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" \
    "fmov x10, d26\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" "umov x10, v26.d[1]\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" \
    "fmov x10, d27\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" "umov x10, v27.d[1]\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" \
    "fmov x10, d28\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" "umov x10, v28.d[1]\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" \
    "fmov x10, d29\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" "umov x10, v29.d[1]\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" \
    "fmov x10, d30\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" "umov x10, v30.d[1]\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" \
    "fmov x10, d31\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n" "umov x10, v31.d[1]\n" "cmp x10, x9\n" "cinc x11, x11, ne\n" "add x9, x9, #1\n"
#define FP_CLOBBERS "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15", "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23", "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31"

// fill, syscall num(a0), check; returns mismatches, *ret = syscall result
static int fp_regs_syscall(uint64 seed, int num, uint64 a0, uint64 *ret)
{
  register uint64 x0 asm("x0") = a0;
  register uint64 x8 asm("x8") = num;
  uint64 bad;
  asm volatile(
    "mov x9, %[seed]\n"
    FP_FILL
    "svc #0\n"
    "mov x9, %[seed]\n"
    "mov x11, #0\n"
    FP_CHECK
    "mov %[bad], x11\n"
    : [bad] "=r"(bad), "+r"(x0)
    : [seed] "r"(seed), "r"(x8)
    : "x9", "x10", "x11", "memory", "cc", FP_CLOBBERS);
  *ret = x0;
  return bad;
}

// fill, spin for n iterations, check; returns mismatches
static int fp_regs_spin(uint64 seed, uint64 n)
{
  uint64 bad;
  asm volatile(
    "mov x9, %[seed]\n"
    FP_FILL
    "mov x12, %[n]\n"
    "1: subs x12, x12, #1\n"
    "b.ne 1b\n"
    "mov x9, %[seed]\n"
    "mov x11, #0\n"
    FP_CHECK
    "mov %[bad], x11\n"
    : [bad] "=r"(bad)
    : [seed] "r"(seed), [n] "r"(n)
    : "x9", "x10", "x11", "x12", "cc", FP_CLOBBERS);
  return bad;
}

#define FP_NREGS 64

// check only; returns mismatches
static int fp_regs_check(uint64 seed)
{
  uint64 bad;
  asm volatile(
    "mov x9, %[seed]\n"
    "mov x11, #0\n"
    FP_CHECK
    "mov %[bad], x11\n"
    : [bad] "=r"(bad)
    : [seed] "r"(seed)
    : "x9", "x10", "x11", "cc");
  return bad;
}
#else
#define FP_FILL \
    "fmv.d.x f0, t0\n" "addi t0, t0, 1\n" \
    "fmv.d.x f1, t0\n" "addi t0, t0, 1\n" \
    "fmv.d.x f2, t0\n" "addi t0, t0, 1\n" \
    "fmv.d.x f3, t0\n" "addi t0, t0, 1\n" \
    "fmv.d.x f4, t0\n" "addi t0, t0, 1\n" \
    "fmv.d.x f5, t0\n" "addi t0, t0, 1\n" \
    "fmv.d.x f6, t0\n" "addi t0, t0, 1\n" \
    "fmv.d.x f7, t0\n" "addi t0, t0, 1\n" \
    "fmv.d.x f8, t0\n" "addi t0, t0, 1\n" \
    "fmv.d.x f9, t0\n" "addi t0, t0, 1\n" \
    "fmv.d.x f10, t0\n" "addi t0, t0, 1\n" \
    "fmv.d.x f11, t0\n" "addi t0, t0, 1\n" \
    "fmv.d.x f12, t0\n" "addi t0, t0, 1\n" \
    "fmv.d.x f13, t0\n" "addi t0, t0, 1\n" \
    "fmv.d.x f14, t0\n" "addi t0, t0, 1\n" \
    "fmv.d.x f15, t0\n" "addi t0, t0, 1\n" \
    "fmv.d.x f16, t0\n" "addi t0, t0, 1\n" \
    "fmv.d.x f17, t0\n" "addi t0, t0, 1\n" \
    "fmv.d.x f18, t0\n" "addi t0, t0, 1\n" \
    "fmv.d.x f19, t0\n" "addi t0, t0, 1\n" \
    "fmv.d.x f20, t0\n" "addi t0, t0, 1\n" \
    "fmv.d.x f21, t0\n" "addi t0, t0, 1\n" \
    "fmv.d.x f22, t0\n" "addi t0, t0, 1\n" \
    "fmv.d.x f23, t0\n" "addi t0, t0, 1\n" \
    "fmv.d.x f24, t0\n" "addi t0, t0, 1\n" \
    "fmv.d.x f25, t0\n" "addi t0, t0, 1\n" \
    "fmv.d.x f26, t0\n" "addi t0, t0, 1\n" \
    "fmv.d.x f27, t0\n" "addi t0, t0, 1\n" \
    "fmv.d.x f28, t0\n" "addi t0, t0, 1\n" \
    "fmv.d.x f29, t0\n" "addi t0, t0, 1\n" \
    "fmv.d.x f30, t0\n" "addi t0, t0, 1\n" \
    "fmv.d.x f31, t0\n" "addi t0, t0, 1\n"
#define FP_CHECK \
    "fmv.x.d t1, f0\n" "beq t1, t0, 1f\n" "addi t2, t2, 1\n" "1: addi t0, t0, 1\n" \
    "fmv.x.d t1, f1\n" "beq t1, t0, 1f\n" "addi t2, t2, 1\n" "1: addi t0, t0, 1\n" \
    "fmv.x.d t1, f2\n" "beq t1, t0, 1f\n" "addi t2, t2, 1\n" "1: addi t0, t0, 1\n" \
    "fmv.x.d t1, f3\n" "beq t1, t0, 1f\n" "addi t2, t2, 1\n" "1: addi t0, t0, 1\n" \
    "fmv.x.d t1, f4\n" "beq t1, t0, 1f\n" "addi t2, t2, 1\n" "1: addi t0, t0, 1\n" \
    "fmv.x.d t1, f5\n" "beq t1, t0, 1f\n" "addi t2, t2, 1\n" "1: addi t0, t0, 1\n" \
    "fmv.x.d t1, f6\n" "beq t1, t0, 1f\n" "addi t2, t2, 1\n" "1: addi t0, t0, 1\n" \
    "fmv.x.d t1, f7\n" "beq t1, t0, 1f\n" "addi t2, t2, 1\n" "1: addi t0, t0, 1\n" \
    "fmv.x.d t1, f8\n" "beq t1, t0, 1f\n" "addi t2, t2, 1\n" "1: addi t0, t0, 1\n" \
    "fmv.x.d t1, f9\n" "beq t1, t0, 1f\n" "addi t2, t2, 1\n" "1: addi t0, t0, 1\n" \
    "fmv.x.d t1, f10\n" "beq t1, t0, 1f\n" "addi t2, t2, 1\n" "1: addi t0, t0, 1\n" \
    "fmv.x.d t1, f11\n" "beq t1, t0, 1f\n" "addi t2, t2, 1\n" "1: addi t0, t0, 1\n" \
    "fmv.x.d t1, f12\n" "beq t1, t0, 1f\n" "addi t2, t2, 1\n" "1: addi t0, t0, 1\n" \
    "fmv.x.d t1, f13\n" "beq t1, t0, 1f\n" "addi t2, t2, 1\n" "1: addi t0, t0, 1\n" \
    "fmv.x.d t1, f14\n" "beq t1, t0, 1f\n" "addi t2, t2, 1\n" "1: addi t0, t0, 1\n" \
    "fmv.x.d t1, f15\n" "beq t1, t0, 1f\n" "addi t2, t2, 1\n" "1: addi t0, t0, 1\n" \
    "fmv.x.d t1, f16\n" "beq t1, t0, 1f\n" "addi t2, t2, 1\n" "1: addi t0, t0, 1\n" \
    "fmv.x.d t1, f17\n" "beq t1, t0, 1f\n" "addi t2, t2, 1\n" "1: addi t0, t0, 1\n" \
    "fmv.x.d t1, f18\n" "beq t1, t0, 1f\n" "addi t2, t2, 1\n" "1: addi t0, t0, 1\n" \
    "fmv.x.d t1, f19\n" "beq t1, t0, 1f\n" "addi t2, t2, 1\n" "1: addi t0, t0, 1\n" \
    "fmv.x.d t1, f20\n" "beq t1, t0, 1f\n" "addi t2, t2, 1\n" "1: addi t0, t0, 1\n" \
    "fmv.x.d t1, f21\n" "beq t1, t0, 1f\n" "addi t2, t2, 1\n" "1: addi t0, t0, 1\n" \
    "fmv.x.d t1, f22\n" "beq t1, t0, 1f\n" "addi t2, t2, 1\n" "1: addi t0, t0, 1\n" \
    "fmv.x.d t1, f23\n" "beq t1, t0, 1f\n" "addi t2, t2, 1\n" "1: addi t0, t0, 1\n" \
    "fmv.x.d t1, f24\n" "beq t1, t0, 1f\n" "addi t2, t2, 1\n" "1: addi t0, t0, 1\n" \
    "fmv.x.d t1, f25\n" "beq t1, t0, 1f\n" "addi t2, t2, 1\n" "1: addi t0, t0, 1\n" \
    "fmv.x.d t1, f26\n" "beq t1, t0, 1f\n" "addi t2, t2, 1\n" "1: addi t0, t0, 1\n" \
    "fmv.x.d t1, f27\n" "beq t1, t0, 1f\n" "addi t2, t2, 1\n" "1: addi t0, t0, 1\n" \
    "fmv.x.d t1, f28\n" "beq t1, t0, 1f\n" "addi t2, t2, 1\n" "1: addi t0, t0, 1\n" \
    "fmv.x.d t1, f29\n" "beq t1, t0, 1f\n" "addi t2, t2, 1\n" "1: addi t0, t0, 1\n" \
    "fmv.x.d t1, f30\n" "beq t1, t0, 1f\n" "addi t2, t2, 1\n" "1: addi t0, t0, 1\n" \
    "fmv.x.d t1, f31\n" "beq t1, t0, 1f\n" "addi t2, t2, 1\n" "1: addi t0, t0, 1\n"
#define FP_CLOBBERS "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9", "f10", "f11", "f12", "f13", "f14", "f15", "f16", "f17", "f18", "f19", "f20", "f21", "f22", "f23", "f24", "f25", "f26", "f27", "f28", "f29", "f30", "f31"

static int fp_regs_syscall(uint64 seed, int num, uint64 arg, uint64 *ret)
{
  register uint64 a0 asm("a0") = arg;
  register uint64 a7 asm("a7") = num;
  uint64 bad;
  asm volatile(
    "mv t0, %[seed]\n"
    FP_FILL
    "ecall\n"
    "mv t0, %[seed]\n"
    "li t2, 0\n"
    FP_CHECK
    "mv %[bad], t2\n"
    : [bad] "=r"(bad), "+r"(a0)
    : [seed] "r"(seed), "r"(a7)
    : "t0", "t1", "t2", "memory", FP_CLOBBERS);
  *ret = a0;
  return bad;
}

static int fp_regs_spin(uint64 seed, uint64 n)
{
  uint64 bad;
  asm volatile(
    "mv t0, %[seed]\n"
    FP_FILL
    "mv t3, %[n]\n"
    "1: addi t3, t3, -1\n"
    "bnez t3, 1b\n"
    "mv t0, %[seed]\n"
    "li t2, 0\n"
    FP_CHECK
    "mv %[bad], t2\n"
    : [bad] "=r"(bad)
    : [seed] "r"(seed), [n] "r"(n)
    : "t0", "t1", "t2", "t3", FP_CLOBBERS);
  return bad;
}

#define FP_NREGS 32

static int fp_regs_check(uint64 seed)
{
  uint64 bad;
  asm volatile(
    "mv t0, %[seed]\n"
    "li t2, 0\n"
    FP_CHECK
    "mv %[bad], t2\n"
    : [bad] "=r"(bad)
    : [seed] "r"(seed)
    : "t0", "t1", "t2");
  return bad;
}
#endif

// Arithmetic on doubles; volatile inputs keep the compiler from folding
// it at build time.
static void test_fp_math(void)
{
  volatile double two = 2.0, one = 1.0, seven = 7.0, x = 1.0, s = 0.0;
  volatile int n = 1000;

  for(int i = 0; i < 20; i++)
    x = (x + two / x) / two;           // Newton's method for sqrt(2)
  if((long)(x * 1e9) != 1414213562L)
    fail("fp math", "sqrt(2)");

  for(int k = 1; k <= n; k++)
    s = s + one / ((double)k * k);     // Basel series, 1000 terms
  if((long)(s * 1e9) != 1643934566L)
    fail("fp math", "sum 1/k^2");

  if((long)((22.0 * one / seven) * 1e9) != 3142857142L)
    fail("fp math", "22/7");
}

#define FP_SEED 0x5eed000000000000UL

// Several processes keep distinct values in all FP registers across
// system calls and preemption: their FP state must not leak or get
// lost when they switch.
static void test_fp_regs(void)
{
  enum { N = 4, ROUNDS = 5 };
  uint64 ret;

  for(int c = 0; c < N; c++){
    int pid = fork();
    if(pid == 0){
      int bad = 0;
      uint64 seed = FP_SEED + ((uint64)c << 32);
      for(int r = 0; r < ROUNDS; r++){
        bad += fp_regs_syscall(seed + r * 100, SYS_pause, 1, &ret);
        bad += fp_regs_syscall(seed + r * 100, SYS_getpid, 0, &ret);
        bad += fp_regs_spin(seed + r * 100 + 50, 20000000);
      }
      exit(bad ? 1 : 0);
    }
  }
  for(int c = 0; c < N; c++){
    int status = -1;
    wait(&status);
    if(status != 0)
      fail("fp regs", "registers changed across a switch");
  }
}

// fork(): the child starts with the parent's FP registers.
static void test_fp_fork(void)
{
  uint64 pid;
  int bad = fp_regs_syscall(FP_SEED + 7, SYS_fork, 0, &pid);
  if(pid == 0)
    exit(bad ? 1 : 0);
  int status = -1;
  wait(&status);
  if(bad)
    fail("fp fork", "parent's registers changed");
  if(status != 0)
    fail("fp fork", "child didn't inherit the registers");
}

// exec(): the new program doesn't see the old one's FP registers.
static void test_fp_exec(void)
{
  int pid = fork();
  if(pid == 0){
    uint64 ret;
    char *argv[] = { "o2tests", "fpexec", 0 };
    fp_regs_syscall(FP_SEED + 9, SYS_getpid, 0, &ret);  // fill, keep live
    exec("o2tests", argv);
    exec("/o2tests", argv);
    exit(2);
  }
  int status = -1;
  wait(&status);
  if(status != 0)
    fail("fp exec", "new program saw the old FP registers");
}

// o2tests fpexec, run by test_fp_exec: no FP register may still hold
// the old program's value.
static int fp_old_pattern_left(void)
{
  return FP_NREGS - fp_regs_check(FP_SEED + 9);
}

// getppid() is the parent's pid, and init's (1) once the parent has
// exited.
static void test_getppid(void)
{
  int me = getpid(), fds[2];
  if(pipe(fds) < 0){
    fail("getppid", "pipe");
    return;
  }
  int pid = fork();
  if(pid == 0){
    close(fds[0]);
    int ok = getppid() == me;
    if(fork() == 0){
      // orphaned once our parent exits
      for(int i = 0; i < 100 && getppid() != 1; i++)
        pause(1);
      char c = ok && getppid() == 1 ? 'y' : 'n';
      write(fds[1], &c, 1);
      exit(0);
    }
    exit(0);
  }
  close(fds[1]);
  wait(0);
  char c = 0;
  if(read(fds[0], &c, 1) != 1 || c != 'y')
    fail("getppid", "wrong parent");
  close(fds[0]);
  if(getppid() <= 0)
    fail("getppid", "o2tests has no parent");
}

// dup2() replaces the target descriptor, sharing the file offset.
static void test_dup2(void)
{
  int fd = open("dup2.tmp", O_CREATE | O_RDWR);
  int other = open("dup2.other", O_CREATE | O_RDWR);
  if(fd < 0 || other < 0){
    fail("dup2", "create");
    return;
  }
  if(dup2(fd, other) != other || dup2(fd, fd) != fd)
    fail("dup2", "return value");
  if(dup2(fd, -1) != -1 || dup2(fd, 1000) != -1 || dup2(99, 3) != -1)
    fail("dup2", "bad descriptors accepted");
  write(fd, "ab", 2);
  write(other, "cd", 2);      // same file now, appended after "ab"
  close(fd);
  close(other);
  char buf[8] = { 0 };
  fd = open("dup2.tmp", O_RDONLY);
  if(read(fd, buf, sizeof(buf)) != 4 || strcmp(buf, "abcd") != 0)
    fail("dup2", "descriptors don't share the file");
  close(fd);
  struct stat st;
  fd = open("dup2.other", O_RDONLY);
  if(fstat(fd, &st) < 0 || st.size != 0)
    fail("dup2", "wrote to the replaced file");
  close(fd);

  // the shell's redirection idiom: dup2 onto stdout in a child
  int fds[2];
  pipe(fds);
  if(fork() == 0){
    close(fds[0]);
    dup2(fds[1], 1);
    close(fds[1]);
    printf("hi\n");
    exit(0);
  }
  close(fds[1]);
  wait(0);
  memset(buf, 0, sizeof(buf));
  if(read(fds[0], buf, sizeof(buf)) != 3 || strcmp(buf, "hi\n") != 0)
    fail("dup2", "stdout not redirected");
  close(fds[0]);
  unlink("dup2.tmp");
  unlink("dup2.other");
}

// lseek() moves the offset, but not past the end.
static void test_lseek(void)
{
  char buf[16];
  int fd = open("lseek.tmp", O_CREATE | O_RDWR | O_TRUNC);
  if(fd < 0){
    fail("lseek", "create");
    return;
  }
  write(fd, "0123456789", 10);
  if(lseek(fd, 2, SEEK_SET) != 2 || read(fd, buf, 3) != 3 || memcmp(buf, "234", 3) != 0)
    fail("lseek", "SEEK_SET");
  if(lseek(fd, -2, SEEK_CUR) != 3 || read(fd, buf, 1) != 1 || buf[0] != '3')
    fail("lseek", "SEEK_CUR");
  if(lseek(fd, -1, SEEK_END) != 9 || read(fd, buf, 2) != 1 || buf[0] != '9')
    fail("lseek", "SEEK_END");
  if(lseek(fd, -11, SEEK_END) != -1 || lseek(fd, 0, 7) != -1 || lseek(fd, 0, SEEK_CUR) != 10)
    fail("lseek", "bad offsets accepted");
  if(lseek(fd, 0, SEEK_SET) != 0 || write(fd, "ab", 2) != 2)
    fail("lseek", "overwrite");

  if(lseek(fd, 1, SEEK_END) != -1 || lseek(fd, 11, SEEK_SET) != -1)
    fail("lseek", "past the end");
  close(fd);
  unlink("lseek.tmp");

  int fds[2];
  pipe(fds);
  if(lseek(fds[0], 0, SEEK_SET) != -1 || lseek(1, 0, SEEK_SET) != -1)
    fail("lseek", "seek on a pipe or the console");
  close(fds[0]);
  close(fds[1]);
}

// waitpid() waits for one particular child, or with WNOHANG not at all.
static void test_waitpid(void)
{
  int pids[3], status = -1;
  for(int i = 0; i < 3; i++){
    if((pids[i] = fork()) == 0){
      pause(i == 1 ? 1 : 20);   // the middle one exits first
      exit(10 + i);
    }
  }
  if(waitpid(pids[2], &status, WNOHANG) != 0)
    fail("waitpid", "WNOHANG with a running child");
  if(waitpid(pids[2], &status, 0) != pids[2] || status != 12)
    fail("waitpid", "a particular child");
  // the middle one exited long ago, but stays until waited for
  if(waitpid(pids[1], &status, WNOHANG) != pids[1] || status != 11)
    fail("waitpid", "WNOHANG with an exited child");
  if(waitpid(-1, &status, 0) != pids[0] || status != 10)
    fail("waitpid", "any child");
  if(waitpid(-1, 0, WNOHANG) != -1 || waitpid(pids[0], 0, 0) != -1 ||
     waitpid(1, 0, 0) != -1 || waitpid(0, 0, 0) != -1)
    fail("waitpid", "no such child");
}

// Write a script and run it with argv; returns what it printed to
// stdout and stderr (into out, up to n-1 bytes), or -1 if exec failed.
static int run_script(const char *text, char **argv, char *out, int n)
{
  int fd = open("script.tmp", O_CREATE | O_WRONLY | O_TRUNC);
  write(fd, text, strlen(text));
  close(fd);
  int fds[2];
  pipe(fds);
  if(fork() == 0){
    close(fds[0]);
    dup2(fds[1], 1);
    dup2(fds[1], 2);
    close(fds[1]);
    exec("script.tmp", argv);
    exit(7);
  }
  close(fds[1]);
  int got = 0, r;
  while(got < n - 1 && (r = read(fds[0], out + got, n - 1 - got)) > 0)
    got += r;
  out[got] = 0;
  close(fds[0]);
  int status;
  wait(&status);
  unlink("script.tmp");
  return status == 7 ? -1 : got;
}

// exec() of a "#!" script runs its interpreter.
static void test_script(void)
{
  char out[128];
  char *argv[] = { "myscript", "a1", "a2", 0 };

  // echo shows the argv it got
  if(run_script("#!/echo\nignored\n", argv, out, sizeof(out)) < 0 ||
     strcmp(out, "script.tmp a1 a2\n") != 0)
    fail("script", "plain interpreter");
  if(run_script("#! /echo  one arg \n", argv, out, sizeof(out)) < 0 ||
     strcmp(out, "one arg script.tmp a1 a2\n") != 0)
    fail("script", "interpreter with an argument");
  if(run_script("#!/echo", argv, out, sizeof(out)) < 0 ||
     strcmp(out, "script.tmp a1 a2\n") != 0)
    fail("script", "no newline");
  // a shell script, with comments
  char *shargv[] = { "myscript", 0 };
  if(run_script("#!/sh\n# comment\necho one # two\n\necho  three ; echo four\n",
                shargv, out, sizeof(out)) < 0 ||
     strcmp(out, "one\nthree\nfour\n") != 0)
    fail("script", "shell script");
  // the shell's PATH
  mkdir("pathdir");
  link("/echo", "pathdir/hello");
  if(run_script("#!/sh\ncd pathdir\nhello a\nPATH=/:\nhello b\n"
                "PATH=/nonexistent:/pathdir/\nhello c\necho d\n"
                "PATH=/\necho e\n/pathdir/hello f\n",
                shargv, out, sizeof(out)) < 0 ||
     strcmp(out, "exec hello failed\nb\nc\nexec echo failed\ne\nf\n") != 0)
    fail("script", "PATH");
  unlink("pathdir/hello");
  unlink("pathdir");
  // history
  if(run_script("#!/sh\necho a\necho b\n!!\n!1\nhistory\n!9\n!x\n",
                shargv, out, sizeof(out)) < 0 ||
     strcmp(out, "a\nb\necho b\nb\necho a\na\n"
                 "1  echo a\n2  echo b\n3  echo b\n4  echo a\n5  history\n"
                 "sh: no such command in history\nexec !x failed\n") != 0)
    fail("script", "history");
  if(run_script("#!/nonexistent\n", argv, out, sizeof(out)) != -1 ||
     run_script("#!\n", argv, out, sizeof(out)) != -1 ||
     run_script("#!/script.tmp\n", argv, out, sizeof(out)) != -1 ||
     run_script("echo hi\n", argv, out, sizeof(out)) != -1)
    fail("script", "bad scripts ran");
}

// Signals with their default actions: most terminate, SIGCHLD doesn't,
// and they interrupt sleeping system calls.
static void test_signals(void)
{
  int status;

  int pid = fork();
  if(pid == 0)
    spin();
  if(kill(pid, SIGTERM) != 0 || wait(&status) != pid || status != -1)
    fail("signals", "SIGTERM didn't terminate");

  // a sleeping process is woken and terminated
  pid = fork();
  if(pid == 0){
    pause(1000);
    exit(0);
  }
  pause(2);
  if(kill(pid, SIGINT) != 0 || wait(&status) != pid || status != -1)
    fail("signals", "SIGINT didn't end pause()");

  // SIGCHLD is ignored by default
  pid = fork();
  if(pid == 0){
    int t0 = uptime();
    kill(getpid(), SIGCHLD);
    pause(2);
    exit(uptime() - t0 >= 2 ? 3 : 4);
  }
  if(wait(&status) != pid || status != 3)
    fail("signals", "SIGCHLD wasn't ignored");

  if(kill(pid, SIGTERM) != -1 || kill(getpid(), 0) != 0 ||
     kill(getpid(), NSIG) != -1 || kill(getpid(), -1) != -1)
    fail("signals", "bad pids or signals accepted");
}

static volatile int got_sig, sig_count;

static void on_sig(int sig)
{
  got_sig = sig;
  sig_count++;
}

// a handler that leaves junk in all FP registers
static void on_sig_clobber(int sig)
{
  fp_regs_spin(0xbad0000000000000UL, 1);
  sig_count++;
}

// sigreturn() with a frame of junk: the process may crash, but only
// itself
static void bad_sigreturn(void)
{
  static char buf[4096] __attribute__((aligned(16)));
  memset(buf, 0xff, sizeof(buf));
#if defined(__aarch64__)
  asm volatile("mov sp, %0; mov x8, %1; svc #0" : : "r"(buf), "r"((uint64)SYS_sigreturn));
#else
  asm volatile("mv sp, %0; li a7, %1; ecall" : : "r"(buf), "i"(SYS_sigreturn));
#endif
  exit(0);   // not reached, the stack is gone
}

// Signal handlers.
static void test_handlers(void)
{
  int pid, status = -1;

  // to self: delivered on the way back from kill()
  got_sig = 0;
  if(signal(SIGUSR1, on_sig) != SIG_DFL)
    fail("handlers", "previous handler");
  if(kill(getpid(), SIGUSR1) != 0 || got_sig != SIGUSR1)
    fail("handlers", "handler not called");
  if(signal(SIGUSR1, SIG_IGN) != on_sig)
    fail("handlers", "previous handler");
  kill(getpid(), SIGUSR1);   // ignored
  if(signal(SIGUSR1, SIG_DFL) != SIG_IGN)
    fail("handlers", "previous handler");
  if(signal(SIGKILL, on_sig) != SIG_ERR || signal(0, on_sig) != SIG_ERR ||
     signal(NSIG, on_sig) != SIG_ERR)
    fail("handlers", "bad signals accepted");

  // the handler is inherited, and interrupts pause(), which returns -1
  int fds[2];
  char c;
  pipe(fds);
  signal(SIGUSR1, on_sig);
  pid = fork();
  if(pid == 0){
    got_sig = 0;
    write(fds[1], "x", 1);
    int r = pause(1000);
    exit(r == -1 && got_sig == SIGUSR1 ? 0 : 1);
  }
  signal(SIGUSR1, SIG_DFL);
  read(fds[0], &c, 1);
  pause(2);
  kill(pid, SIGUSR1);
  if(wait(&status) != pid || status != 0)
    fail("handlers", "pause() not interrupted");

  // registers and FP state survive handlers that change them, for
  // signals arriving at any point. A signal sent while the same one is
  // still pending is merged with it, so keep sending until the child
  // has seen enough.
  pid = fork();
  if(pid == 0){
    int bad = 0;
    sig_count = 0;
    signal(SIGUSR2, on_sig_clobber);
    write(fds[1], "x", 1);
    while(sig_count < 5)
      bad += fp_regs_spin(FP_SEED + 3, 100000);
    exit(bad ? 1 : 0);
  }
  read(fds[0], &c, 1);
  int r = 0;
  for(int i = 0; i < 1000 && (r = waitpid(pid, &status, WNOHANG)) == 0; i++){
    kill(pid, SIGUSR2);
    pause(1);
  }
  if(r == 0){
    kill(pid, SIGKILL);
    wait(0);
    fail("handlers", "signals lost");
  } else if(r != pid || status != 0)
    fail("handlers", "registers changed");
  close(fds[0]);
  close(fds[1]);

  // exec: handlers go back to the default, ignored signals stay ignored
  char *argv[] = { "o2tests", "sigself", 0 };
  for(int ign = 0; ign < 2; ign++){
    pid = fork();
    if(pid == 0){
      signal(SIGUSR1, ign ? SIG_IGN : on_sig);
      exec("/o2tests", argv);
      exit(2);
    }
    if(wait(&status) != pid || status != (ign ? 0 : -1))
      fail("handlers", ign ? "ignored signal not kept across exec" :
                             "handler kept across exec");
  }

  // SIGCHLD when a child exits
  got_sig = 0;
  signal(SIGCHLD, on_sig);
  pid = fork();
  if(pid == 0)
    exit(0);
  pause(1000);   // interrupted by SIGCHLD
  if(wait(&status) != pid || got_sig != SIGCHLD)
    fail("handlers", "no SIGCHLD");
  signal(SIGCHLD, SIG_DFL);

  pid = fork();
  if(pid == 0)
    bad_sigreturn();
  if(wait(&status) != pid || status != -1)
    fail("handlers", "bad sigreturn frame");
}

static void exit_with_sig(int sig)
{
  exit(100 + sig);
}

// Run fn in a child with SIGSEGV, SIGBUS, SIGILL and SIGTRAP handled
// by handler (or ignored); returns its exit status.
static int fault_status(void (*fn)(void), sighandler_t handler)
{
  int pid = fork();
  if(pid == 0){
    signal(SIGSEGV, handler);
    signal(SIGBUS, handler);
    signal(SIGILL, handler);
    signal(SIGTRAP, handler);
    fn();
    exit(0);
  }
  int status = 0;
  wait(&status);
  return status;
}

static void store_to_null(void)
{
  *(volatile char *)0 = 1;   // page 0 is read-only text
}

static void illegal_instruction(void)
{
#if defined(__aarch64__)
  asm volatile(".inst 0x00000000");   // udf #0
#else
  asm volatile(".word 0");            // all zeros: illegal
#endif
}

static void breakpoint(void)
{
#if defined(__aarch64__)
  asm volatile("brk #0");
#else
  asm volatile("ebreak");
#endif
}

// Faults raise signals a handler can catch.
static void test_sigsegv(void)
{
  if(fault_status(store_to_null, exit_with_sig) != 100 + SIGSEGV ||
     fault_status(read_kernel, exit_with_sig) != 100 + SIGSEGV)
    fail("sigsegv", "SIGSEGV not caught");
  if(fault_status(illegal_instruction, exit_with_sig) != 100 + SIGILL)
    fail("sigsegv", "SIGILL not caught");
  if(fault_status(breakpoint, exit_with_sig) != 100 + SIGTRAP)
    fail("sigsegv", "SIGTRAP not caught");
  // ignoring a fault doesn't work: the process still dies
  if(fault_status(store_to_null, SIG_IGN) != -1)
    fail("sigsegv", "ignored SIGSEGV");
}

// printf's formats, widths and flags.
static void test_printf(void)
{
  char buf[200];
  int fds[2];
  pipe(fds);
  fprintf(fds[1], "[%d|%5d|%-5d|%05d|%05d|%x|%lx|%ld|%lu|%u]",
          -12, 42, 42, 42, -42, 0xbeef, 0x123456789aUL, -5000000000L,
          18446744073709551615UL, 4000000000U);
  fprintf(fds[1], "[%s|%6s|%-6s|%c|%3c|%%|%02d:%02d]", "ab", "ab", "ab", 'x', 'y', 7, 30);
  close(fds[1]);
  int n = read(fds[0], buf, sizeof(buf) - 1);
  close(fds[0]);
  buf[n < 0 ? 0 : n] = 0;
  char *want = "[-12|   42|42   |00042|-0042|BEEF|123456789A|-5000000000|18446744073709551615|4000000000]"
               "[ab|    ab|ab    |x|  y|%|07:30]";
  if(strcmp(buf, want) != 0){
    printf("o2tests: got %s\n", buf);
    fail("printf", "wrong output");
  }
}

// clock_gettime() and gmtime().
static void test_time(void)
{
  struct timespec r0, m0, r1, m1;
  if(clock_gettime(CLOCK_REALTIME, &r0) < 0 || clock_gettime(CLOCK_MONOTONIC, &m0) < 0 ||
     clock_gettime(7, &r0) != -1 || clock_gettime(CLOCK_REALTIME, (void *)0x7000000000UL) != -1)
    fail("time", "clock_gettime");
  int rtc = r0.tv_sec >= 1577836800;   // the Raspberry Pi 4 has none: 1970
  if(!rtc)
    printf("o2tests: no real-time clock, checking time since boot only\n");
  if(r0.tv_nsec >= 1000000000 || m0.tv_nsec >= 1000000000)
    fail("time", "nanoseconds");
  pause(10);   // 100 ms
  clock_gettime(CLOCK_REALTIME, &r1);
  clock_gettime(CLOCK_MONOTONIC, &m1);
  uint64 dr = (r1.tv_sec - r0.tv_sec) * 1000000000 + r1.tv_nsec - r0.tv_nsec;
  uint64 dm = (m1.tv_sec - m0.tv_sec) * 1000000000 + m1.tv_nsec - m0.tv_nsec;
  if(dm < 90000000 || dm > 2000000000 || dr < 90000000 || dr > 2000000000)
    fail("time", "clocks don't advance with pause()");

  struct { uint64 t; int y, mon, d, h, min, s, wday, yday; } cases[] = {
    { 0,          70, 0, 1, 0, 0, 0, 4, 0 },
    { 951782400,  100, 1, 29, 0, 0, 0, 2, 59 },     // 2000-02-29, a leap day
    { 1792314711, 126, 9, 18, 9, 11, 51, 0, 290 },  // 2026-10-18
    { 4107542399, 200, 1, 28, 23, 59, 59, 0, 58 },  // 2100-02-28, not leap
  };
  for(int i = 0; i < sizeof(cases) / sizeof(cases[0]); i++){
    struct tm tm;
    gmtime(cases[i].t, &tm);
    if(tm.tm_year != cases[i].y || tm.tm_mon != cases[i].mon || tm.tm_mday != cases[i].d ||
       tm.tm_hour != cases[i].h || tm.tm_min != cases[i].min || tm.tm_sec != cases[i].s ||
       tm.tm_wday != cases[i].wday || tm.tm_yday != cases[i].yday){
      printf("o2tests: gmtime(%lu) = %d-%d-%d %d:%d:%d wday %d yday %d\n", cases[i].t,
             tm.tm_year, tm.tm_mon, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
             tm.tm_wday, tm.tm_yday);
      fail("time", "gmtime");
    }
    if(timegm(&tm) != cases[i].t)
      fail("time", "timegm");
  }

  // setting the clock: a day ahead, and back
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  uint64 t0 = ts.tv_sec;
  ts.tv_sec += 86400;
  if(clock_settime(CLOCK_REALTIME, &ts) < 0 || time() < t0 + 86400 || time() > t0 + 86402)
    fail("time", "clock_settime");
  ts.tv_sec = t0;
  clock_settime(CLOCK_REALTIME, &ts);
  ts.tv_nsec = 1000000000;
  if(clock_settime(CLOCK_MONOTONIC, &ts) != -1 || clock_settime(CLOCK_REALTIME, &ts) != -1 ||
     time() > t0 + 2)
    fail("time", "bad clock_settime worked");
}

// Wait until the wall clock's seconds change.
static void next_second(void)
{
  uint64 t0 = time();
  while(time() == t0)
    pause(1);
}

// Files record when they were created and changed.
static void test_timestamps(void)
{
  struct stat st, st2;
  uint64 t0 = time();
  int fd = open("ts.tmp", O_CREATE | O_RDWR | O_TRUNC);
  if(fd < 0 || fstat(fd, &st) < 0){
    fail("timestamps", "create");
    return;
  }
  if(st.atime < t0 || st.mtime < t0 || st.ctime < t0 || st.mtime > time())
    fail("timestamps", "new file");

  next_second();
  write(fd, "x", 1);
  fstat(fd, &st2);
  if(st2.mtime <= st.mtime || st2.ctime != st2.mtime || st2.atime != st.atime)
    fail("timestamps", "write");

  next_second();
  char c;
  lseek(fd, 0, SEEK_SET);
  read(fd, &c, 1);
  link("ts.tmp", "ts.link");
  fstat(fd, &st);
  if(st.mtime != st2.mtime || st.ctime <= st2.ctime)
    fail("timestamps", "link changes ctime only");
  close(fd);

  next_second();
  fd = open("ts.tmp", O_RDWR | O_TRUNC);
  fstat(fd, &st2);
  if(st2.mtime <= st.mtime || st2.size != 0)
    fail("timestamps", "truncate");
  close(fd);

  // a directory changes when entries are added or removed
  struct stat d0, d1;
  stat(".", &d0);
  next_second();
  unlink("ts.link");
  stat(".", &d1);
  if(d1.mtime <= d0.mtime)
    fail("timestamps", "directory");
  unlink("ts.tmp");
}

// Fill n pages at p with a pattern from seed; check it, returning the
// number of pages that differ.
static void fill_pages(char *p, int n, int seed)
{
  for(int i = 0; i < n; i++)
    for(int j = 0; j < 4096; j += 512)
      p[i * 4096 + j] = seed + i + j / 512;
}

static int check_pages(char *p, int n, int seed)
{
  int bad = 0;
  for(int i = 0; i < n; i++)
    for(int j = 0; j < 4096; j += 512)
      if(p[i * 4096 + j] != (char)(seed + i + j / 512)){
        bad++;
        break;
      }
  return bad;
}

// fork() shares memory copy-on-write: each process sees only its own
// writes, also those the kernel makes (read()), and a process using
// more than half of RAM can still fork.
static void test_cow(void)
{
  enum { N = 64 };
  char *mem = sbrk(N * 4096);
  int fds[2], back[2], status;
  char c;

  fill_pages(mem, N, 1);
  pipe(fds);
  pipe(back);
  int pid = fork();
  if(pid == 0){
    int bad = check_pages(mem, N, 1);
    fill_pages(mem, N, 2);              // the child's own copy
    write(back[1], "x", 1);
    read(fds[0], &c, 1);                // wait for the parent's writes
    bad += check_pages(mem, N, 2);
    exit(bad);
  }
  read(back[0], &c, 1);
  close(back[0]);
  close(back[1]);
  if(check_pages(mem, N, 1) != 0)
    fail("cow", "the child's writes showed in the parent");
  fill_pages(mem, N / 2, 3);
  write(fds[1], "y", 1);
  waitpid(pid, &status, 0);
  if(status != 0)
    fail("cow", "the parent's writes showed in the child");

  // the kernel writing into a shared page (read() from a pipe)
  pid = fork();
  if(pid == 0){
    read(fds[0], mem + 4096 * 10, 1);
    exit(mem[4096 * 10] == 'z' ? 0 : 1);
  }
  write(fds[1], "z", 1);
  waitpid(pid, &status, 0);
  if(status != 0 || mem[4096 * 10] == 'z')
    fail("cow", "read() into a shared page");
  close(fds[0]);
  close(fds[1]);

  // processes writing to the same shared pages at once
  for(int i = 0; i < 4; i++){
    if(fork() == 0){
      int bad = 0;
      for(int round = 0; round < 20; round++){
        fill_pages(mem, N, 10 * i + round);
        bad += check_pages(mem, N, 10 * i + round);
      }
      exit(bad);
    }
  }
  for(int i = 0; i < 4; i++){
    wait(&status);
    if(status != 0)
      fail("cow", "concurrent writers");
  }
  if(check_pages(mem + N / 2 * 4096, N / 2, 1 + N / 2) != 0)
    fail("cow", "the parent's memory changed");
  sbrk(-N * 4096);

  // 72 of the 128 MB of RAM: forking needs copy-on-write
  int big = 72 * 1024 * 1024;
  char *p = sbrk(big);
  if(p == SBRK_ERROR){
    fail("cow", "sbrk");
    return;
  }
  for(int i = 0; i < big; i += 4096)
    p[i] = 1;
  pid = fork();
  if(pid < 0){
    fail("cow", "fork of a big process");
  } else if(pid == 0){
    p[0] = 2;   // one page of its own
    exit(p[4096] == 1 && p[0] == 2 ? 0 : 1);
  } else {
    waitpid(pid, &status, 0);
    if(status != 0 || p[0] != 1)
      fail("cow", "big process");
  }
  sbrk(-big);
}

// Run fn(arg) in a child; returns its exit status, 100 + sig if it
// faulted (SIGSEGV or SIGBUS).
static int child_status(void (*fn)(char *), char *arg)
{
  int pid = fork();
  if(pid == 0){
    signal(SIGSEGV, exit_with_sig);
    signal(SIGBUS, exit_with_sig);
    fn(arg);
    exit(0);
  }
  int status = -1;
  waitpid(pid, &status, 0);
  return status;
}

static void touch_read(char *p)
{
  (void)*(volatile char *)p;
}

static void touch_write(char *p)
{
  *(volatile char *)p = 1;
}

// Anonymous memory mappings.
static void test_mmap(void)
{
  int status;
  char *heap = sbrk(0);

  // private: lazily allocated, zeroed
  char *p = mmap(0, 3 * 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if(p == MAP_FAILED || (uint64)p % 4096 != 0 || p < heap){
    fail("mmap", "mmap");
    return;
  }
  if(p[0] != 0 || p[3 * 4096 - 1] != 0)
    fail("mmap", "not zeroed");
  memset(p, 'a', 3 * 4096);

  // the kernel reads and writes it too
  int fds[2];
  pipe(fds);
  write(fds[1], p + 4096 - 2, 4);
  char *q = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if(q == MAP_FAILED || q == p || read(fds[0], q + 10, 4) != 4 || memcmp(q + 10, "aaaa", 4) != 0)
    fail("mmap", "read/write through a mapping");
  close(fds[0]);
  close(fds[1]);

  // a hole in the middle
  if(munmap(p + 4096, 4096) != 0)
    fail("mmap", "munmap");
  if(p[0] != 'a' || p[2 * 4096] != 'a')
    fail("mmap", "the rest of a split mapping");
  if(child_status(touch_read, p + 4096) != 100 + SIGSEGV)
    fail("mmap", "an unmapped page is still there");
  munmap(p, 3 * 4096);
  munmap(q, 4096);
  if(child_status(touch_read, p) != 100 + SIGSEGV)
    fail("mmap", "munmap");

  // read-only
  p = mmap(0, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if(p == MAP_FAILED || p[100] != 0 || child_status(touch_write, p) != 100 + SIGSEGV)
    fail("mmap", "PROT_READ");
  munmap(p, 4096);
  p = mmap(0, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if(p == MAP_FAILED || child_status(touch_read, p) != 100 + SIGSEGV)
    fail("mmap", "PROT_NONE");
  munmap(p, 4096);

  // shared with a child, or private to each
  char *sh = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  char *pr = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if(sh == MAP_FAILED || pr == MAP_FAILED){
    fail("mmap", "mmap");
    return;
  }
  strcpy(sh, "parent");
  strcpy(pr, "parent");
  int pid = fork();
  if(pid == 0){
    int ok = strcmp(sh, "parent") == 0 && strcmp(pr, "parent") == 0;
    strcpy(sh, "child");
    strcpy(pr, "child");
    exit(ok ? 0 : 1);
  }
  waitpid(pid, &status, 0);
  if(status != 0 || strcmp(sh, "child") != 0 || strcmp(pr, "parent") != 0)
    fail("mmap", "MAP_SHARED / MAP_PRIVATE after fork");
  munmap(sh, 4096);
  munmap(pr, 4096);

  // big and lazy: only touched pages take memory
  p = mmap(0, 512 * 1024 * 1024, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if(p == MAP_FAILED){
    fail("mmap", "a big private mapping");
  } else {
    p[0] = p[256 * 1024 * 1024] = 1;
    munmap(p, 512 * 1024 * 1024);
  }

  // children's mappings go away when they exit or exec: 200 of 1 MB
  // each would not fit if they leaked
  for(int i = 0; i < 200; i++){
    pid = fork();
    if(pid == 0){
      p = mmap(0, 1024 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
      if(p == MAP_FAILED)
        exit(1);
      if(i % 2){
        char *argv[] = { "o2tests", "nop", 0 };
        exec("/o2tests", argv);
        exit(1);
      }
      exit(0);
    }
    waitpid(pid, &status, 0);
    if(status != 0){
      fail("mmap", "mappings leak");
      break;
    }
  }

  if(mmap(0, 0, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) != MAP_FAILED ||
     mmap(0, 4096, PROT_READ, MAP_SHARED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) != MAP_FAILED ||
     mmap(0, 4096, 8, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) != MAP_FAILED ||
     munmap((char *)4097, 4096) != -1)
    fail("mmap", "bad arguments accepted");
  if(sbrk(0) != heap)
    fail("mmap", "the heap moved");
}

// Make a file of n bytes, byte i being 'a' + i % 26.
static void make_file(char *name, int n)
{
  char buf[512];
  int fd = open(name, O_CREATE | O_WRONLY | O_TRUNC);
  for(int i = 0; i < n; i += sizeof(buf)){
    for(int j = 0; j < sizeof(buf); j++)
      buf[j] = 'a' + (i + j) % 26;
    write(fd, buf, n - i < sizeof(buf) ? n - i : sizeof(buf));
  }
  close(fd);
}

// Bytes [from, to) of the file; check they are 'a' + i % 26, or c if
// c isn't 0. Returns the number that differ.
static int check_file(char *name, int from, int to, char c)
{
  char ch;
  int bad = 0, fd = open(name, O_RDONLY);
  lseek(fd, from, SEEK_SET);
  for(int i = from; i < to; i++){
    if(read(fd, &ch, 1) != 1 || ch != (c ? c : 'a' + i % 26))
      bad++;
  }
  close(fd);
  return bad;
}

// File mappings.
static void test_mmap_file(void)
{
  enum { SIZE = 6000 };
  int status, bad = 0;
  struct stat st;

  make_file("mm.tmp", SIZE);
  int fd = open("mm.tmp", O_RDWR);
  int rofd = open("mm.tmp", O_RDONLY);

  // private: a copy; zeros past the end of the file
  char *p = mmap(0, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE, rofd, 0);
  if(p == MAP_FAILED){
    fail("mmap file", "MAP_PRIVATE");
    return;
  }
  for(int i = 0; i < 8192; i++)
    bad += p[i] != (i < SIZE ? 'a' + i % 26 : 0);
  if(bad)
    fail("mmap file", "contents");
  memset(p, 'x', 8192);
  munmap(p, 8192);
  if(check_file("mm.tmp", 0, SIZE, 0) != 0)
    fail("mmap file", "MAP_PRIVATE wrote to the file");

  // from an offset
  p = mmap(0, 4096, PROT_READ, MAP_PRIVATE, rofd, 4096);
  if(p == MAP_FAILED || p[0] != 'a' + 4096 % 26)
    fail("mmap file", "offset");
  munmap(p, 4096);

  // shared: written back by munmap, within the file
  p = mmap(0, 8192, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if(p == MAP_FAILED){
    fail("mmap file", "MAP_SHARED");
    return;
  }
  memset(p + 100, 'y', 50);
  memset(p + SIZE - 10, 'z', 200);   // past the end: not written
  // write() from the mapping, to the same file
  if(write(fd, p + 100, 10) != 10)
    fail("mmap file", "write() from a mapping of the file");
  munmap(p, 8192);
  if(check_file("mm.tmp", 100, 150, 'y') != 0 || check_file("mm.tmp", 150, SIZE - 10, 0) != 0 ||
     check_file("mm.tmp", SIZE - 10, SIZE, 'z') != 0)
    fail("mmap file", "MAP_SHARED not written back");
  if(stat("mm.tmp", &st) < 0 || st.size != SIZE)
    fail("mmap file", "the file grew");

  // a child shares the mapping; exit writes it back
  p = mmap(0, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  int pid = fork();
  if(pid == 0){
    p[0] = 'C';
    exit(0);
  }
  waitpid(pid, &status, 0);
  if(p[0] != 'C')
    fail("mmap file", "the child's write");
  pid = fork();
  if(pid == 0){
    p[1] = 'D';
    exit(0);   // without munmap
  }
  waitpid(pid, &status, 0);
  if(check_file("mm.tmp", 0, 2, 0) == 0)
    fail("mmap file", "exit didn't write back");
  munmap(p, SIZE);

  // what may not be mapped
  int fds[2];
  pipe(fds);
  int dfd = open(".", O_RDONLY);
  if(mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, rofd, 0) != MAP_FAILED ||
     mmap(0, 4096, PROT_READ, MAP_PRIVATE, fds[0], 0) != MAP_FAILED ||
     mmap(0, 4096, PROT_READ, MAP_PRIVATE, dfd, 0) != MAP_FAILED ||
     mmap(0, 4096, PROT_READ, MAP_PRIVATE, 99, 0) != MAP_FAILED ||
     mmap(0, 4096, PROT_READ, MAP_PRIVATE, fd, 100) != MAP_FAILED)
    fail("mmap file", "bad mappings allowed");
  close(fds[0]);
  close(fds[1]);
  close(dfd);

  // the mapping holds the file: close and unlink don't affect it
  p = mmap(0, 4096, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  close(rofd);
  unlink("mm.tmp");
  if(p == MAP_FAILED || p[50] != 'a' + 50 % 26)
    fail("mmap file", "mapping of a closed, unlinked file");
  munmap(p, 4096);
}

// Files past what direct and single-indirect blocks hold (268 KB).
static void test_bigfile(void)
{
  enum { CHUNK = 8192, SIZE = 2 * 1024 * 1024 };
  static char buf[CHUNK];
  struct stat st;

  for(int round = 0; round < 2; round++){
    int fd = open("big.tmp", O_CREATE | O_RDWR | O_TRUNC);
    if(fd < 0){
      fail("bigfile", "create");
      return;
    }
    for(int off = 0; off < SIZE; off += CHUNK){
      for(int i = 0; i < CHUNK; i += 512)
        *(int *)(buf + i) = off + i + round;
      if(write(fd, buf, CHUNK) != CHUNK){
        fail("bigfile", "write");
        close(fd);
        unlink("big.tmp");
        return;
      }
    }
    if(fstat(fd, &st) < 0 || st.size != SIZE)
      fail("bigfile", "size");
    // read back, from the end and from the start
    int bad = 0;
    for(int k = 0; k < 2; k++){
      int off = k ? 0 : SIZE - CHUNK;
      lseek(fd, off, SEEK_SET);
      for(; off < SIZE; off += CHUNK){
        if(read(fd, buf, CHUNK) != CHUNK){
          bad++;
          break;
        }
        for(int i = 0; i < CHUNK; i += 512)
          bad += *(int *)(buf + i) != off + i + round;
      }
    }
    if(bad)
      fail("bigfile", "contents");
    close(fd);
    // the second round truncates the first round's file (O_TRUNC),
    // freeing its double-indirect blocks, and writes it anew
  }
  unlink("big.tmp");
  if(sync() != 0)
    fail("bigfile", "sync");
}

// Read a whole (small) file into buf; returns its length, or -1.
static int read_file(char *path, char *buf, int n)
{
  int fd = open(path, O_RDONLY);
  if(fd < 0)
    return -1;
  int got = read(fd, buf, n - 1);
  close(fd);
  buf[got < 0 ? 0 : got] = 0;
  return got;
}

// Symbolic links.
static void test_symlink(void)
{
  char buf[64];
  struct stat st;

  mkdir("sl.d");
  make_file("sl.d/f", 10);

  // relative to the link's directory; read and written through
  if(symlink("f", "sl.d/lf") != 0 || read_file("sl.d/lf", buf, sizeof(buf)) != 10 ||
     memcmp(buf, "abcdefghij", 10) != 0)
    fail("symlink", "relative link");
  int fd = open("sl.d/lf", O_WRONLY);
  write(fd, "XY", 2);
  close(fd);
  if(read_file("sl.d/f", buf, sizeof(buf)) != 10 || memcmp(buf, "XYcd", 4) != 0)
    fail("symlink", "write through a link");

  // stat follows, lstat and readlink don't
  if(stat("sl.d/lf", &st) < 0 || st.type != T_FILE || st.size != 10)
    fail("symlink", "stat");
  if(lstat("sl.d/lf", &st) < 0 || st.type != T_SYMLINK || st.size != 1)
    fail("symlink", "lstat");
  int n = readlink("sl.d/lf", buf, sizeof(buf));
  if(n != 1 || buf[0] != 'f' || readlink("sl.d/f", buf, sizeof(buf)) != -1)
    fail("symlink", "readlink");

  // absolute, and to a directory in the middle of a path
  if(symlink("/sl.d", "sl.dir") != 0 || read_file("sl.dir/f", buf, sizeof(buf)) != 10 ||
     symlink("sl.dir/lf", "sl.chain") != 0 || read_file("sl.chain", buf, sizeof(buf)) != 10)
    fail("symlink", "absolute link, link to a directory");
  if(chdir("sl.dir") != 0 || read_file("f", buf, sizeof(buf)) != 10 || chdir("..") != 0 ||
     stat("sl.d", &st) < 0)
    fail("symlink", "chdir through a link");

  // dangling, loops, too many
  symlink("nothing", "sl.dang");
  symlink("sl.loop2", "sl.loop1");
  symlink("sl.loop1", "sl.loop2");
  if(open("sl.dang", O_RDONLY) >= 0 || open("sl.loop1", O_RDONLY) >= 0 ||
     lstat("sl.dang", &st) < 0 || st.type != T_SYMLINK)
    fail("symlink", "dangling link or loop");
  char a[16], b[16];
  strcpy(a, "sl.0");
  symlink("sl.d/f", a);
  for(int i = 1; i <= 10; i++){
    strcpy(b, a);
    a[3] = '0' + i % 10;
    a[4] = i == 10 ? '+' : 0;
    a[5] = 0;
    symlink(b, a);
    if((open(a, O_RDONLY) >= 0) != (i < 10))
      fail("symlink", i < 10 ? "a chain of links" : "more than 10 links");
  }

  // O_NOFOLLOW, and link() and unlink() of the link itself
  if(open("sl.d/lf", O_RDONLY | O_NOFOLLOW) >= 0 || (fd = open("sl.d/f", O_RDONLY | O_NOFOLLOW)) < 0)
    fail("symlink", "O_NOFOLLOW");
  close(fd);
  if(link("sl.d/lf", "sl.hard") != 0 || lstat("sl.hard", &st) < 0 || st.type != T_SYMLINK ||
     st.nlink != 2)
    fail("symlink", "hard link to a link");
  if(symlink("x", "sl.d/f") != -1 || symlink("", "sl.empty") != -1)
    fail("symlink", "bad symlinks made");
  unlink("sl.hard");
  unlink("sl.d/lf");
  if(stat("sl.d/f", &st) < 0)
    fail("symlink", "unlink removed the target");

  for(int i = 0; i <= 10; i++){
    strcpy(a, "sl.0");
    a[3] = '0' + i % 10;
    if(i == 10){
      a[4] = '+';
      a[5] = 0;
    }
    unlink(a);
  }
  unlink("sl.dang");
  unlink("sl.loop1");
  unlink("sl.loop2");
  unlink("sl.chain");
  unlink("sl.dir");
  unlink("sl.d/f");
  unlink("sl.d");
}

// lockstat() counts acquires per lock name.
static void test_lockstat(void)
{
  static struct lockstat ls[64], ls2[64];
  int n = lockstat(ls, 64);
  int proc = -1;
  for(int i = 0; i < n; i++)
    if(strcmp(ls[i].name, "proc") == 0)
      proc = i;
  if(n <= 0 || proc < 0 || ls[proc].acquire == 0){
    fail("lockstat", "no proc locks");
    return;
  }
  getpid();
  int pid = fork();
  if(pid == 0)
    exit(0);
  wait(0);
  if(lockstat(ls2, 64) < n || ls2[proc].acquire <= ls[proc].acquire ||
     lockstat(ls2, 1) != 1 || lockstat((void *)0x7000000000UL, 64) != -1)
    fail("lockstat", "counts");
}

// Datagram sockets, on qemu's user-mode network (skipped without one).
static void test_net(void)
{
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if(fd < 0){
    printf("o2tests: no network, skipping\n");
    return;
  }
  struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(5555) };
  int fd2 = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if(bind(fd, &a, sizeof(a)) != 0 || bind(fd2, &a, sizeof(a)) != -1 || bind(fd, &a, sizeof(a)) != -1)
    fail("net", "bind");
  a.sin_port = 0;
  if(bind(fd2, &a, sizeof(a)) != 0)
    fail("net", "bind to any port");

  // nothing arrives: with MSG_DONTWAIT, and with a timeout
  char buf[64];
  struct timeval tv = { 0, 200000 };
  uint64 t0 = uptime();
  if(recvfrom(fd, buf, sizeof(buf), MSG_DONTWAIT, 0, 0) != -1 ||
     setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0 ||
     recvfrom(fd, buf, sizeof(buf), 0, 0, 0) != -1 || uptime() - t0 < 20 || uptime() - t0 > 100)
    fail("net", "receive timeout");
  close(fd);
  close(fd2);

  // ping the gateway
  fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
  struct sockaddr_in gw = { .sin_family = AF_INET };
  inet_aton("10.0.2.2", &gw.sin_addr);
  tv.tv_sec = 2;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  char msg[16] = { 8 };   // echo request
  msg[7] = 42;
  struct sockaddr_in from;
  int len = sizeof(from);
  if(sendto(fd, msg, sizeof(msg), 0, &gw, sizeof(gw)) != sizeof(msg) ||
     recvfrom(fd, buf, sizeof(buf), 0, &from, &len) != sizeof(msg) || buf[0] != 0 ||
     buf[7] != 42 || from.sin_addr.s_addr != gw.sin_addr.s_addr)
    fail("net", "ping 10.0.2.2");
  close(fd);

  if(socket(AF_INET, 1, 0) != -1 || socket(1, SOCK_DGRAM, 0) != -1 ||
     socket(AF_INET, SOCK_DGRAM, 6) != -1 || bind(0, &a, sizeof(a)) != -1 ||
     sendto(1, msg, 1, 0, &gw, sizeof(gw)) != -1)
    fail("net", "bad arguments accepted");
}

// The framebuffer device (skipped without a GPU).
static void test_fb(void)
{
  struct fb_info info;
  int fd = open("/fb", O_RDWR);
  if(fd < 0 || ioctl(fd, FB_INFO, (uint64)&info) < 0){
    printf("o2tests: no framebuffer, skipping\n");
    if(fd >= 0)
      close(fd);
    return;
  }
  if(info.width == 0 || info.height == 0 || info.stride < info.width * 4 ||
     info.size < info.stride * info.height)
    fail("fb", "FB_INFO");
  uint32 *fb = mmap(0, info.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if(fb == MAP_FAILED){
    fail("fb", "mmap");
    close(fd);
    return;
  }
  fb[0] = 0x123456;
  struct fb_rect r = { 0, 0, 1, 1 }, bad = { info.width, 0, 1, 1 };
  if(ioctl(fd, FB_FLUSH, (uint64)&r) != 0 || ioctl(fd, FB_FLUSH, 0) != 0 ||
     ioctl(fd, FB_FLUSH, (uint64)&bad) != -1 || ioctl(fd, 99, 0) != -1)
    fail("fb", "FB_FLUSH");

  // a child draws in the same framebuffer
  int pid = fork();
  if(pid == 0){
    fb[1] = 0xabcdef;
    exit(0);
  }
  waitpid(pid, 0, 0);
  if(fb[1] != 0xabcdef || fb[0] != 0x123456)
    fail("fb", "shared with a child");

  // a second mapping sees the same pixels
  uint32 *fb2 = mmap(0, 4096, PROT_READ, MAP_SHARED, fd, 0);
  if(fb2 == MAP_FAILED || fb2[1] != 0xabcdef)
    fail("fb", "second mapping");
  if(mmap(0, 4096, PROT_READ, MAP_PRIVATE, fd, 0) != MAP_FAILED ||
     mmap(0, info.size + 4096, PROT_READ, MAP_SHARED, fd, 0) != MAP_FAILED)
    fail("fb", "bad mappings allowed");
  munmap(fb2, 4096);
  munmap(fb, info.size);
  close(fd);
}

// O_NONBLOCK: reads that would wait return -1 instead.
static void test_nonblock(void)
{
  int fds[2];
  char c;
  pipe(fds);
  if(fcntl(fds[0], F_GETFL, 0) != O_RDONLY || fcntl(fds[0], F_SETFL, O_NONBLOCK) != 0 ||
     fcntl(fds[0], F_GETFL, 0) != (O_RDONLY | O_NONBLOCK))
    fail("nonblock", "fcntl");
  if(read(fds[0], &c, 1) != -1)
    fail("nonblock", "empty pipe");
  write(fds[1], "x", 1);
  if(read(fds[0], &c, 1) != 1 || c != 'x')
    fail("nonblock", "pipe with data");
  close(fds[1]);
  if(read(fds[0], &c, 1) != 0)
    fail("nonblock", "pipe at its end");
  close(fds[0]);

  // writes to a full pipe: -1, or what fit; and small ones are atomic
  static char buf[3000];
  pipe(fds);
  fcntl(fds[1], F_SETFL, O_NONBLOCK);
  int n = 0, r;
  while((r = write(fds[1], buf, 100)) == 100)
    n += 100;
  if(r != -1 || n < 512)
    fail("nonblock", "write to a full pipe");
  close(fds[0]);
  close(fds[1]);
  pipe(fds);
  fcntl(fds[1], F_SETFL, O_NONBLOCK);
  if((r = write(fds[1], buf, sizeof(buf))) <= 0 || r >= sizeof(buf))
    fail("nonblock", "long write");
  close(fds[0]);
  close(fds[1]);
  if(fcntl(99, F_GETFL, 0) != -1 || fcntl(1, 99, 0) != -1)
    fail("nonblock", "bad fcntl accepted");
}

// memfd(): memory that processes map separately, and share.
static void test_memfd(void)
{
  int fd = memfd(3 * 4096 + 1);
  if(fd < 0)
    fail("memfd", "memfd");
  char c;
  if(read(fd, &c, 1) != -1 || write(fd, "x", 1) != -1)
    fail("memfd", "read or write worked");
  if(mmap(0, 4096, PROT_READ, MAP_PRIVATE, fd, 0) != MAP_FAILED)
    fail("memfd", "MAP_PRIVATE worked");
  if(mmap(0, 5 * 4096, PROT_READ, MAP_SHARED, fd, 0) != MAP_FAILED)
    fail("memfd", "mapped past its end");
  int *a = mmap(0, 4 * 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if(a == MAP_FAILED)
    fail("memfd", "mmap");
  if(a[0] != 0 || a[3 * 1024 + 5] != 0)
    fail("memfd", "not zeroed");
  a[1024] = 42;
  // a child maps it again, at its own address, and writes
  int pid = fork();
  if(pid == 0){
    munmap(a, 4 * 4096);
    int *b = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 4096);
    if(b == MAP_FAILED || b[0] != 42)
      exit(1);
    b[1] = 43;
    exit(0);
  }
  int st;
  wait(&st);
  if(st != 0 || a[1025] != 43)
    fail("memfd", "not shared");
  // the memory stays while mapped, closed or not; and is freed: 100
  // times 4MB is more than there is
  close(fd);
  a[0] = 1;
  munmap(a, 4 * 4096);
  for(int i = 0; i < 100; i++){
    if((fd = memfd(4 << 20)) < 0)
      fail("memfd", "leaks");
    char *p = mmap(0, 4 << 20, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(p == MAP_FAILED)
      fail("memfd", "leaks (mmap)");
    p[(4 << 20) - 1] = 1;
    close(fd);
    munmap(p, 4 << 20);
  }
  // pages come when mapped: 16MB, of which a page is used, 100 times
  for(int i = 0; i < 100; i++){
    if((fd = memfd(16 << 20)) < 0)
      fail("memfd", "16MB");
    char *p = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (16 << 20) - 4096);
    if(p == MAP_FAILED)
      fail("memfd", "16MB (mmap)");
    p[1] = 1;
    munmap(p, 4096);
    close(fd);
  }
  if(memfd(0) != -1 || memfd(64 << 20) != -1)
    fail("memfd", "bad sizes worked");
}

static void o2tests(void)
{
  struct { const char *name; void (*fn)(void); } tests[] = {
    { "forkwait", test_forkwait },
    { "forkloop", test_forkloop },
    { "cow", test_cow },
    { "mmap", test_mmap },
    { "mmap file", test_mmap_file },
    { "faults", test_faults },
    { "badptr", test_badptr },
    { "kill", test_kill },
    { "files", test_files },
    { "dirs", test_dirs },
    { "pipe", test_pipe },
    { "ps", test_ps },
    { "klog", test_klog },
    { "printf", test_printf },
    { "time", test_time },
    { "timestamps", test_timestamps },
    { "bigfile", test_bigfile },
    { "symlink", test_symlink },
    { "lockstat", test_lockstat },
    { "net", test_net },
    { "fb", test_fb },
    { "nonblock", test_nonblock },
    { "memfd", test_memfd },
    { "getppid", test_getppid },
    { "dup2", test_dup2 },
    { "lseek", test_lseek },
    { "waitpid", test_waitpid },
    { "script", test_script },
    { "signals", test_signals },
    { "handlers", test_handlers },
    { "sigsegv", test_sigsegv },
    { "fp math", test_fp_math },
    { "fp regs", test_fp_regs },
    { "fp fork", test_fp_fork },
    { "fp exec", test_fp_exec },
  };
  for(int i = 0; i < sizeof(tests) / sizeof(tests[0]); i++){
    int before = failed;
    tests[i].fn();
    printf("o2tests: %s %s\n", tests[i].name, failed == before ? "ok" : "FAILED");
  }
  printf(failed ? "o2tests: SOME TESTS FAILED\n" : "o2tests: ALL TESTS PASSED\n");
}

int main(int argc, char *argv[])
{
  if(argc == 2 && strcmp(argv[1], "fpexec") == 0)
    exit(fp_old_pattern_left() ? 1 : 0);
  if(argc == 2 && strcmp(argv[1], "nop") == 0)
    exit(0);
  if(argc == 2 && strcmp(argv[1], "sigself") == 0){
    // run by test_handlers: send ourselves SIGUSR1
    kill(getpid(), SIGUSR1);
    exit(0);
  }
  o2tests();
  exit(failed);
}
