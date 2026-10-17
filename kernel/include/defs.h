#include "types.h"

// number of elements in fixed-size array
#define NELEM(x) (sizeof(x)/sizeof((x)[0]))

// syscall.c
int argstr(int n, char *buf, int max);
void argint(int n, int *ip);
void argaddr(int n, uint64 *ip);
int fetchstr(uint64 addr, char *buf, int max);
int fetchaddr(uint64 addr, uint64 *ip);
void syscall(void);
// string.c
void* memset(void *dst, int c, uint n);
int memcmp(const void *v1, const void *v2, uint n);
void* memmove(void *dst, const void *src, uint n);
void* memcpy(void *dst, const void *src, uint n);
int strncmp(const char *p, const char *q, uint n);
char* strncpy(char *s, const char *t, int n);
char* safestrcpy(char *s, const char *t, int n);
int strlen(const char *s);

// kalloc.c
void kinit(void);
void *kalloc(void);
void kfree(void *pa);
uint64 kfreepages(void);

// bio.c
struct buf;
void binit(void);
struct buf *bread(uint dev, uint blockno);
void brelse(struct buf *b);
void bwrite(struct buf *b);
void bpin(struct buf *b);
void bunpin(struct buf *b);

// file.c
struct file;
struct file *filealloc(void);
void fileclose(struct file *f);
struct file *filedup(struct file *f);
void fileinit(void);
int fileread(struct file *f, uint64 addr, int n);
int filestat(struct file *f, uint64 addr);
int filewrite(struct file *f, uint64 addr, int n);
int fileseek(struct file *f, int off, int whence);

// pipe.c
struct pipe;
int pipealloc(struct file **f0, struct file **f1);
void pipeclose(struct pipe *pi, int writable);
int piperead(struct pipe *pi, uint64 addr, int n);
int pipewrite(struct pipe *pi, uint64 addr, int n);

// fs.c
struct inode;
struct stat;
struct superblock;
extern struct superblock sb;
void fsinit(int dev);
int dirlink(struct inode *dp, char *name, uint inum);
struct inode *dirlookup(struct inode *dp, char *name, uint *poff);
struct inode *ialloc(uint dev, short type);
struct inode *idup(struct inode *ip);
void iinit(void);
void ilock(struct inode *ip);
void iput(struct inode *ip);
void iunlock(struct inode *ip);
void iunlockput(struct inode *ip);
void iupdate(struct inode *ip);
int namecmp(const char *s, const char *t);
struct inode *namei(char *path);
struct inode *nameiparent(char *path, char *name);
int readi(struct inode *ip, int user_dst, uint64 dst, uint off, uint n);
void stati(struct inode *ip, struct stat *st);
int writei(struct inode *ip, int user_src, uint64 src, uint off, uint n);
void itrunc(struct inode *ip);

// log.c
void initlog(int dev, struct superblock *sb);
void log_write(struct buf *b);
void begin_op(void);
void end_op(void);

// virtio_disk.c
void virtio_disk_init(void);
int virtio_disk_present(void);
struct buf;
void virtio_disk_rw(struct buf *b, int write);

// arch: start the other CPUs; each enters main(0)
void start_cpus(void);

// selftest.c (make SELFTEST=1)
void selftest(void);
void selftest_fs(void);
