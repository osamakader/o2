#include "types.h"
#include "stat.h"
#include "user.h"

// kill [-sig] pid...: send signal sig (default SIGTERM) to each pid.
int
main(int argc, char **argv)
{
  int i = 1, sig = SIGTERM, status = 0;

  if(argc > 1 && argv[1][0] == '-'){
    sig = atoi(argv[1] + 1);
    i++;
  }
  if(i >= argc){
    fprintf(2, "usage: kill [-sig] pid...\n");
    exit(1);
  }
  for(; i < argc; i++){
    if(kill(atoi(argv[i]), sig) < 0){
      fprintf(2, "kill: %s failed\n", argv[i]);
      status = 1;
    }
  }
  exit(status);
}
