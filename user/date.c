// date: print the current date and time (UTC).
// date -s YYYY-MM-DD HH:MM[:SS]: set it (UTC), for boards without an
// RTC, like the Raspberry Pi.

#include "types.h"
#include "user.h"
#include "time.h"

static char *days[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static char *months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

// Set the clock from "YYYY-MM-DD HH:MM[:SS]" (any separators) in the
// arguments. Returns 0, or -1 if they aren't a date and time.
static int
set(int argc, char *argv[])
{
  int v[6] = { 0 }, n = 0;
  for(int i = 0; i < argc; i++){
    for(char *s = argv[i]; *s; ){
      if(*s < '0' || *s > '9'){
        s++;
        continue;
      }
      if(n == 6)
        return -1;
      while(*s >= '0' && *s <= '9')
        v[n] = v[n] * 10 + *s++ - '0';
      n++;
    }
  }
  if(n < 5 || v[0] < 1970 || v[1] < 1 || v[1] > 12 || v[2] < 1 || v[2] > 31 ||
     v[3] > 23 || v[4] > 59 || v[5] > 59)
    return -1;
  struct tm tm = { .tm_year = v[0] - 1900, .tm_mon = v[1] - 1, .tm_mday = v[2],
                   .tm_hour = v[3], .tm_min = v[4], .tm_sec = v[5] };
  struct timespec ts = { timegm(&tm), 0 };
  return clock_settime(CLOCK_REALTIME, &ts);
}

int
main(int argc, char *argv[])
{
  struct tm tm;

  if(argc > 1){
    if(strcmp(argv[1], "-s") != 0 || set(argc - 2, argv + 2) < 0){
      fprintf(2, "usage: date [-s YYYY-MM-DD HH:MM[:SS]]\n");
      exit(1);
    }
  }

  gmtime(time(), &tm);
  printf("%s %s %2d %02d:%02d:%02d UTC %d\n", days[tm.tm_wday], months[tm.tm_mon],
         tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, tm.tm_year + 1900);
  exit(0);
}
