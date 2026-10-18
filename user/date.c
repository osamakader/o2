// date: print the current date and time (UTC).

#include "types.h"
#include "user.h"
#include "time.h"

static char *days[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static char *months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

int
main(int argc, char *argv[])
{
  struct tm tm;

  gmtime(time(), &tm);
  printf("%s %s %2d %02d:%02d:%02d UTC %d\n", days[tm.tm_wday], months[tm.tm_mon],
         tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, tm.tm_year + 1900);
  exit(0);
}
