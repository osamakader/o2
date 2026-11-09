// fetch [-i] [-o file] url: get url (http:// only) and print what came,
// or write it to file. -i prints the response's header first. The
// status and size go to stderr.

#include "types.h"
#include "user.h"
#include "fcntl.h"
#include "libnet.h"

int
main(int argc, char *argv[])
{
  int head = 0, out = 1, i;
  char *file = 0;
  for(i = 1; i < argc && argv[i][0] == '-'; i++){
    if(strcmp(argv[i], "-i") == 0)
      head = 1;
    else if(strcmp(argv[i], "-o") == 0 && i + 1 < argc)
      file = argv[++i];
    else
      break;
  }
  if(i != argc - 1){
    fprintf(2, "usage: fetch [-i] [-o file] url\n");
    exit(1);
  }

  static struct http h;
  int r = http_get(argv[i], &h);
  if(r < 0){
    fprintf(2, "fetch: %s: %s\n", argv[i], http_error(r));
    exit(1);
  }
  if(file && (out = open(file, O_WRONLY | O_CREATE | O_TRUNC)) < 0){
    fprintf(2, "fetch: cannot write %s\n", file);
    exit(1);
  }
  if(head){
    write(out, h.header, strlen(h.header));
    write(out, "\n\n", 2);
  }
  static char buf[4096];
  int n, total = 0;
  while((n = http_read(&h, buf, sizeof(buf))) > 0){
    if(write(out, buf, n) != n){
      fprintf(2, "fetch: write failed\n");
      exit(1);
    }
    total += n;
  }
  http_close(&h);
  if(file)
    close(out);
  fprintf(2, "fetch: %s: %d, %d bytes\n", h.url, h.status, total);
  exit(n < 0 || h.status / 100 != 2);
}
