// bench test [nproc]: run nproc (default 4) processes of a workload in
// parallel and report how long it took.
//   alloc  allocate and free memory (the page allocator)
//   fs     read a cached file over and over (the buffer cache)
//   fork   fork and wait for children

#include "types.h"
#include "user.h"
#include "fcntl.h"
#include "time.h"

static void
alloc(int id)
{
  for(int i = 0; i < 300; i++){
    char *p = sbrk(64 * 4096);
    if(p == SBRK_ERROR){
      printf("bench: sbrk failed\n");
      exit(1);
    }
    for(int j = 0; j < 64; j++)
      p[j * 4096] = j;
    sbrk(-64 * 4096);
  }
}

static void
fs(int id)
{
  static char buf[4096];
  char name[16] = "bench.0";
  name[6] = '0' + id % 10;
  int fd = open(name, O_CREATE | O_RDWR | O_TRUNC);
  for(int i = 0; i < 16; i++)
    write(fd, buf, sizeof(buf));
  close(fd);
  for(int i = 0; i < 60; i++){
    fd = open(name, O_RDONLY);
    while(read(fd, buf, sizeof(buf)) > 0)
      ;
    close(fd);
  }
  unlink(name);
}

static void
forks(int id)
{
  for(int i = 0; i < 100; i++){
    int pid = fork();
    if(pid == 0)
      exit(0);
    waitpid(pid, 0, 0);
  }
}

static uint64
now_us(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

int
main(int argc, char *argv[])
{
  void (*fn)(int) = 0;
  if(argc >= 2 && strcmp(argv[1], "alloc") == 0)
    fn = alloc;
  else if(argc >= 2 && strcmp(argv[1], "fs") == 0)
    fn = fs;
  else if(argc >= 2 && strcmp(argv[1], "fork") == 0)
    fn = forks;
  if(fn == 0){
    fprintf(2, "usage: bench alloc|fs|fork [nproc]\n");
    exit(1);
  }
  int n = argc >= 3 ? atoi(argv[2]) : 4;

  uint64 t0 = now_us();
  for(int i = 0; i < n; i++){
    if(fork() == 0){
      fn(i);
      exit(0);
    }
  }
  for(int i = 0; i < n; i++)
    wait(0);
  uint64 t = now_us() - t0;
  printf("bench %s %d: %lu.%03lu s\n", argv[1], n, t / 1000000, t / 1000 % 1000);
  exit(0);
}
