#ifndef USER_H
#define USER_H

#include "types.h"
#include "signal.h"

#define SBRK_ERROR ((char *)-1)

struct stat;
struct procinfo;

// system calls
int fork(void);
int exit(int) __attribute__((noreturn));
int wait(int *);
int read(int, void *, int);
int write(int, const void *, int);
int kill(int, int);
int exec(const char *, char **);
int getpid(void);
char *sys_sbrk(int, int);
sighandler_t sys_signal(int, sighandler_t, void (*)(void));
void sigreturn_tramp(void);
int pause(int);
int sync(void);
int uptime(void);
int pipe(int *);
int close(int);
int open(const char *, int);
int mknod(const char *, short, short);
int unlink(const char *);
int fstat(int fd, struct stat *);
int link(const char *, const char *);
int mkdir(const char *);
int chdir(const char *);
int dup(int);
int getprocs(struct procinfo *, int);
int klog(char *, int);
int getppid(void);
int dup2(int, int);
int lseek(int, int, int);
int waitpid(int, int *, int);

// ulib.c
char *strcpy(char *, const char *);
int strcmp(const char *, const char *);
uint strlen(const char *);
void *memset(void *, int, uint);
void *memmove(void *, const void *, int);
int memcmp(const void *, const void *, uint);
void *memcpy(void *, const void *, uint);
char *gets(char *, int max);
char *sbrk(int);
char *sbrklazy(int);
sighandler_t signal(int, sighandler_t);
int stat(const char *, struct stat *);
char *strchr(const char *, char c);
int atoi(const char *);

// umalloc.c
void *malloc(uint);
void free(void *);

// printf.c
void fprintf(int, const char *, ...) __attribute__((format(printf, 2, 3)));
void printf(const char *, ...) __attribute__((format(printf, 1, 2)));

#endif
