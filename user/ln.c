#include "types.h"
#include "stat.h"
#include "user.h"

// ln [-s] old new: a hard link, or with -s a symbolic link.
int
main(int argc, char *argv[])
{
  int sym = argc == 4 && strcmp(argv[1], "-s") == 0;

  if(argc != 3 && !sym){
    fprintf(2, "Usage: ln [-s] old new\n");
    exit(1);
  }
  char *old = argv[argc - 2], *new = argv[argc - 1];
  if((sym ? symlink(old, new) : link(old, new)) < 0){
    fprintf(2, "%s %s %s: failed\n", sym ? "symlink" : "link", old, new);
    exit(1);
  }
  exit(0);
}
