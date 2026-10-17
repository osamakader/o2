// User-mode tests: processes, memory, faults, files, directories and
// pipes. Run from the shell: o2tests

#include "types.h"
#include "user.h"
#include "stat.h"
#include "fcntl.h"
#include "procinfo.h"
#include "syscall.h"

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
#define KERNEL_TEXT 0xffffffc040080000UL
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

// lseek() moves the offset; writing past the end leaves zeros in the
// gap.
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

  // a gap of more than a block, ending mid-block
  int gap = 3000;
  if(lseek(fd, gap, SEEK_END) != 10 + gap || write(fd, "end", 3) != 3)
    fail("lseek", "write past the end");
  struct stat st;
  if(fstat(fd, &st) < 0 || st.size != 10 + gap + 3)
    fail("lseek", "size after the gap");
  lseek(fd, 0, SEEK_SET);
  int bad = 0;
  char c;
  for(int i = 0; i < 10 + gap + 3; i++){
    if(read(fd, &c, 1) != 1){
      bad = 1;
      break;
    }
    if(i < 10)
      bad |= c != "ab23456789"[i];
    else if(i < 10 + gap)
      bad |= c != 0;
    else
      bad |= c != "end"[i - 10 - gap];
  }
  if(bad)
    fail("lseek", "file contents");
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

// Write a script and run it with argv; returns what it printed (into
// out, up to n-1 bytes), or -1 if exec failed.
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
  if(run_script("#!/nonexistent\n", argv, out, sizeof(out)) != -1 ||
     run_script("#!\n", argv, out, sizeof(out)) != -1 ||
     run_script("#!/script.tmp\n", argv, out, sizeof(out)) != -1 ||
     run_script("echo hi\n", argv, out, sizeof(out)) != -1)
    fail("script", "bad scripts ran");
}

static void o2tests(void)
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
    { "klog", test_klog },
    { "getppid", test_getppid },
    { "dup2", test_dup2 },
    { "lseek", test_lseek },
    { "waitpid", test_waitpid },
    { "script", test_script },
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
  o2tests();
  exit(failed);
}
