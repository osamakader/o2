// pwd: print the current directory's path.
//
// The file system keeps no names in inodes, so walk up the tree the
// classic way: at each level, find our inode number among the parent
// directory's entries, until "." and ".." are the same inode (the
// root). There is a single disk, so inode numbers identify files.

#include "types.h"
#include "stat.h"
#include "fcntl.h"
#include "fs.h"
#include "user.h"

#define MAXDEPTH 32

// inode number of the file at path, or -1
static int inum(char *path)
{
  struct stat st;

  if(stat(path, &st) < 0)
    return -1;
  return st.ino;
}

// Find the entry for inode ino in directory dir; copy its name to
// name (DIRSIZ+1 bytes). Returns 0 on success.
static int lookup(char *dir, int ino, char *name)
{
  struct dirent de;
  int fd = open(dir, O_RDONLY);

  if(fd < 0)
    return -1;
  while(read(fd, &de, sizeof(de)) == sizeof(de)){
    // skip other inodes, and "." / ".." (names are NUL-padded, but a
    // DIRSIZ-long one has no terminator, so don't strcmp them)
    if(de.inum != ino || (de.name[0] == '.' &&
       (de.name[1] == 0 || (de.name[1] == '.' && de.name[2] == 0))))
      continue;
    memmove(name, de.name, DIRSIZ);
    name[DIRSIZ] = 0;
    close(fd);
    return 0;
  }
  close(fd);
  return -1;
}

int
main(void)
{
  // up: "..", "../..", ...; names[i]: the component at depth i
  char up[3 * MAXDEPTH + 1] = "";
  char names[MAXDEPTH][DIRSIZ + 1];
  char parent[3 * MAXDEPTH + 4];
  int depth = 0;

  for(;;){
    // here = up (or "."), parent = up/..
    char *here = depth == 0 ? "." : up;
    strcpy(parent, depth == 0 ? ".." : up);
    if(depth > 0)
      strcpy(parent + strlen(parent), "/..");

    int self = inum(here), dad = inum(parent);
    if(self < 0 || dad < 0){
      fprintf(2, "pwd: cannot stat %s\n", self < 0 ? here : parent);
      exit(1);
    }
    if(self == dad)
      break;  // at the root
    if(depth == MAXDEPTH){
      fprintf(2, "pwd: too deep\n");
      exit(1);
    }
    if(lookup(parent, self, names[depth]) < 0){
      fprintf(2, "pwd: cannot find %s in %s\n", here, parent);
      exit(1);
    }
    depth++;
    strcpy(up, parent);
  }

  if(depth == 0)
    printf("/");
  for(int i = depth - 1; i >= 0; i--)
    printf("/%s", names[i]);
  printf("\n");
  exit(0);
}
