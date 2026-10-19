// evtest [n]: print the next n (default 10) input events.

#include "types.h"
#include "user.h"
#include "fcntl.h"
#include "input.h"

int
main(int argc, char *argv[])
{
  int n = argc > 1 ? atoi(argv[1]) : 10;
  int fd = open("/input", O_RDONLY);
  if(fd < 0){
    fprintf(2, "evtest: no input device\n");
    exit(1);
  }
  struct input_event e;
  for(int i = 0; i < n && read(fd, &e, sizeof(e)) == sizeof(e); i++)
    printf("event type %d code %d value %d\n", e.type, e.code, e.value);
  exit(0);
}
