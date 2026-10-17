// ps: list processes and kernel threads.

#include "types.h"
#include "user.h"
#include "procinfo.h"

#define MAXPROCS 64

static struct procinfo procs[MAXPROCS];

static const char *statename(int state)
{
  switch(state){
  case PS_USED:     return "new";
  case PS_SLEEPING: return "sleep";
  case PS_RUNNABLE: return "runble";
  case PS_RUNNING:  return "run";
  case PS_ZOMBIE:   return "zombie";
  default:          return "?";
  }
}

// print s left-aligned in a field of width w
static void field(const char *s, int w)
{
  int n = strlen(s);
  printf("%s", s);
  while(n++ < w)
    printf(" ");
}

static void num(int v, int w)
{
  char buf[16];
  int i = sizeof(buf) - 1;

  buf[i] = 0;
  do {
    buf[--i] = '0' + v % 10;
    v /= 10;
  } while(v > 0 && i > 0);
  field(buf + i, w);
}

int
main(void)
{
  int n = getprocs(procs, MAXPROCS);
  if(n < 0){
    fprintf(2, "ps: getprocs failed\n");
    exit(1);
  }

  printf("PID  PPID STATE  SIZE   NAME\n");
  for(int i = 0; i < n; i++){
    struct procinfo *p = &procs[i];
    num(p->pid, 5);
    num(p->ppid, 5);
    field(statename(p->state), 7);
    num((int)p->sz, 7);
    if(p->kthread)
      printf("[%s]\n", p->name);
    else
      printf("%s\n", p->name);
  }
  exit(0);
}
