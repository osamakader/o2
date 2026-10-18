// lockstat cmd [args]: run cmd, then show the spin locks it (and
// anything else running meanwhile) used most, by contention.

#include "types.h"
#include "user.h"
#include "lockstat.h"

#define N 64

static struct lockstat before[N], after[N];

int
main(int argc, char *argv[])
{
  if(argc < 2){
    fprintf(2, "usage: lockstat cmd [args]\n");
    exit(1);
  }
  int nb = lockstat(before, N);
  int pid = fork();
  if(pid == 0){
    exec(argv[1], argv + 1);
    char path[64] = "/";
    if(strlen(argv[1]) < sizeof(path) - 1){
      strcpy(path + 1, argv[1]);
      exec(path, argv + 1);
    }
    fprintf(2, "lockstat: exec %s failed\n", argv[1]);
    exit(1);
  }
  waitpid(pid, 0, 0);
  int na = lockstat(after, N);

  // the differences; slots only ever get added
  for(int i = 0; i < na; i++){
    if(i < nb){
      after[i].acquire -= before[i].acquire;
      after[i].contended -= before[i].contended;
      after[i].spin -= before[i].spin;
    }
  }
  printf("%-16s %10s %10s %12s\n", "lock", "acquires", "contended", "spins");
  // most contended first; then by acquires
  for(int shown = 0; shown < 12; shown++){
    int best = -1;
    for(int i = 0; i < na; i++){
      if(after[i].acquire == 0)
        continue;
      if(best < 0 || after[i].contended > after[best].contended ||
         (after[i].contended == after[best].contended && after[i].acquire > after[best].acquire))
        best = i;
    }
    if(best < 0)
      break;
    printf("%-16s %10lu %10lu %12lu\n", after[best].name, after[best].acquire,
           after[best].contended, after[best].spin);
    after[best].acquire = 0;
  }
  exit(0);
}
