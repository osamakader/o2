// tz: show the time zone; tz +HH:MM [name]: set it (an offset from
// UTC, and a name like CEST); tz UTC: back to UTC. It lives in
// /timezone.

#include "types.h"
#include "user.h"
#include "fcntl.h"

static int
valid(char *s)
{
  if(*s != '+' && *s != '-')
    return 0;
  s++;
  int h = 0, m = 0, n = 0;
  for(; *s >= '0' && *s <= '9'; s++, n++)
    h = h * 10 + *s - '0';
  if(n == 0 || n > 2 || h > 14)
    return 0;
  if(*s == 0)
    return 1;
  if(*s++ != ':')
    return 0;
  for(n = 0; *s >= '0' && *s <= '9'; s++, n++)
    m = m * 10 + *s - '0';
  return n == 2 && m < 60 && *s == 0;
}

int
main(int argc, char *argv[])
{
  char name[12];
  if(argc == 1){
    long off = tzoffset(name);
    printf("%s (UTC%c%02d:%02d)\n", name, off < 0 ? '-' : '+', (int)(off < 0 ? -off : off) / 3600,
           (int)(off < 0 ? -off : off) % 3600 / 60);
    exit(0);
  }
  if(argc == 2 && (strcmp(argv[1], "UTC") == 0 || strcmp(argv[1], "0") == 0)){
    unlink("/timezone");
    exit(0);
  }
  if(argc > 3 || !valid(argv[1]) || (argc == 3 && strlen(argv[2]) > 11)){
    fprintf(2, "usage: tz [+HH:MM [name] | UTC]\n");
    exit(1);
  }
  int fd = open("/timezone", O_WRONLY | O_CREATE | O_TRUNC);
  if(fd < 0){
    fprintf(2, "tz: cannot write /timezone\n");
    exit(1);
  }
  write(fd, argv[1], strlen(argv[1]));
  if(argc == 3){
    write(fd, " ", 1);
    write(fd, argv[2], strlen(argv[2]));
  }
  write(fd, "\n", 1);
  close(fd);
  exit(0);
}
