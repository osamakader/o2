//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
// From xv6.
//

#include "types.h"
#include "param.h"
#include "defs.h"
#include "console.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "vm.h"
#include "stat.h"
#include "fs.h"
#include "file.h"
#include "fcntl.h"
#include "memlayout.h"
#include "exec.h"
#include "timer.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int argfd(int n, int *pfd, struct file **pf)
{
    int fd;
    struct file *f;

    argint(n, &fd);
    if (fd < 0 || fd >= NOFILE || (f = myproc()->ofile[fd]) == 0)
        return -1;
    if (pfd)
        *pfd = fd;
    if (pf)
        *pf = f;
    return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
int fdalloc(struct file *f)
{
    struct proc *p = myproc();

    for (int fd = 0; fd < NOFILE; fd++) {
        if (p->ofile[fd] == 0) {
            p->ofile[fd] = f;
            return fd;
        }
    }
    return -1;
}

uint64 sys_dup(void)
{
    struct file *f;
    int fd;

    if (argfd(0, 0, &f) < 0)
        return -1;
    if ((fd = fdalloc(f)) < 0)
        return -1;
    filedup(f);
    return fd;
}

// dup2(old, new): make new refer to old's file, closing whatever new
// had open first.
uint64 sys_dup2(void)
{
    struct proc *p = myproc();
    struct file *f, *prev;
    int oldfd, newfd;

    argint(1, &newfd);
    if (argfd(0, &oldfd, &f) < 0 || newfd < 0 || newfd >= NOFILE)
        return -1;
    if (newfd == oldfd)
        return newfd;
    filedup(f);
    prev = p->ofile[newfd];
    p->ofile[newfd] = f;
    if (prev)
        fileclose(prev);
    return newfd;
}

uint64 sys_read(void)
{
    struct file *f;
    int n;
    uint64 p;

    argaddr(1, &p);
    argint(2, &n);
    if (argfd(0, 0, &f) < 0)
        return -1;
    return fileread(f, p, n);
}

uint64 sys_write(void)
{
    struct file *f;
    int n;
    uint64 p;

    argaddr(1, &p);
    argint(2, &n);
    if (argfd(0, 0, &f) < 0)
        return -1;

    return filewrite(f, p, n);
}

uint64 sys_lseek(void)
{
    struct file *f;
    int off, whence;

    argint(1, &off);
    argint(2, &whence);
    if (argfd(0, 0, &f) < 0)
        return -1;
    return fileseek(f, off, whence);
}

// ioctl(fd, req, arg): a device-specific request (CONSOLE_RAW).
uint64 sys_ioctl(void)
{
    struct file *f;
    int req;
    uint64 arg;

    argint(1, &req);
    argaddr(2, &arg);
    if (argfd(0, 0, &f) < 0 || f->type != FD_DEVICE)
        return -1;
    if (f->major < 0 || f->major >= NDEV || !devsw[f->major].ioctl)
        return -1;
    return devsw[f->major].ioctl(req, arg);
}

uint64 sys_close(void)
{
    int fd;
    struct file *f;

    if (argfd(0, &fd, &f) < 0)
        return -1;
    myproc()->ofile[fd] = 0;
    fileclose(f);
    return 0;
}

uint64 sys_fstat(void)
{
    struct file *f;
    uint64 st; // user pointer to struct stat

    argaddr(1, &st);
    if (argfd(0, 0, &f) < 0)
        return -1;
    return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64 sys_link(void)
{
    char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
    struct inode *dp, *ip;

    if (argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
        return -1;

    begin_op();
    if ((ip = namei_nofollow(old)) == 0) {   // a link to a link, as on Linux
        end_op();
        return -1;
    }

    ilock(ip);
    if (ip->type == T_DIR) {
        iunlockput(ip);
        end_op();
        return -1;
    }

    ip->nlink++;
    ip->ctime = time_sec();
    iupdate(ip);
    iunlock(ip);

    if ((dp = nameiparent(new, name)) == 0)
        goto bad;
    ilock(dp);
    if (dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0) {
        iunlockput(dp);
        goto bad;
    }
    iunlockput(dp);
    iput(ip);

    end_op();

    return 0;

bad:
    ilock(ip);
    ip->nlink--;
    iupdate(ip);
    iunlockput(ip);
    end_op();
    return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int isdirempty(struct inode *dp)
{
    struct dirent de;

    for (int off = 2 * sizeof(de); off < dp->size; off += sizeof(de)) {
        if (readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
            panic("isdirempty: readi");
        if (de.inum != 0)
            return 0;
    }
    return 1;
}

uint64 sys_unlink(void)
{
    struct inode *ip, *dp;
    struct dirent de;
    char name[DIRSIZ], path[MAXPATH];
    uint off;

    if (argstr(0, path, MAXPATH) < 0)
        return -1;

    begin_op();
    if ((dp = nameiparent(path, name)) == 0) {
        end_op();
        return -1;
    }

    ilock(dp);

    // Cannot unlink "." or "..".
    if (namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
        goto bad;

    if ((ip = dirlookup(dp, name, &off)) == 0)
        goto bad;
    ilock(ip);

    if (ip->nlink < 1)
        panic("unlink: nlink < 1");
    if (ip->type == T_DIR && !isdirempty(ip)) {
        iunlockput(ip);
        goto bad;
    }

    memset(&de, 0, sizeof(de));
    if (writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
        panic("unlink: writei");
    if (ip->type == T_DIR) {
        dp->nlink--;
        iupdate(dp);
    }
    iunlockput(dp);

    ip->nlink--;
    ip->ctime = time_sec();
    iupdate(ip);
    iunlockput(ip);

    end_op();

    return 0;

bad:
    iunlockput(dp);
    end_op();
    return -1;
}

static struct inode *create(char *path, short type, short major, short minor)
{
    struct inode *ip, *dp;
    char name[DIRSIZ];

    if ((dp = nameiparent(path, name)) == 0)
        return 0;

    ilock(dp);

    if ((ip = dirlookup(dp, name, 0)) != 0) {
        iunlockput(dp);
        ilock(ip);
        if (type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
            return ip;
        iunlockput(ip);
        return 0;
    }

    if ((ip = ialloc(dp->dev, type)) == 0) {
        iunlockput(dp);
        return 0;
    }

    ilock(ip);
    ip->major = major;
    ip->minor = minor;
    ip->nlink = 1;
    iupdate(ip);

    if (type == T_DIR) {  // Create . and .. entries.
        // No ip->nlink++ for ".": avoid cyclic ref count.
        if (dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
            goto fail;
    }

    if (dirlink(dp, name, ip->inum) < 0)
        goto fail;

    if (type == T_DIR) {
        // now that success is guaranteed:
        dp->nlink++;  // for ".."
        iupdate(dp);
    }

    iunlockput(dp);

    return ip;

fail:
    // something went wrong. de-allocate ip.
    ip->nlink = 0;
    iupdate(ip);
    iunlockput(ip);
    iunlockput(dp);
    return 0;
}

// symlink(target, path): make path a symbolic link to target, which
// need not exist.
uint64 sys_symlink(void)
{
    char target[MAXPATH], path[MAXPATH];
    struct inode *ip;
    int n;

    if (argstr(0, target, MAXPATH) < 0 || argstr(1, path, MAXPATH) < 0 ||
        (n = strlen(target)) == 0)
        return -1;
    begin_op();
    if ((ip = create(path, T_SYMLINK, 0, 0)) == 0) {
        end_op();
        return -1;
    }
    int r = writei(ip, 0, (uint64)target, 0, n) == n ? 0 : -1;
    iunlockput(ip);
    end_op();
    return r;
}

// readlink(path, buf, n): the target of symbolic link path, up to n
// bytes, not null-terminated. Returns its length, or -1.
uint64 sys_readlink(void)
{
    char path[MAXPATH];
    uint64 buf;
    int n;
    struct inode *ip;

    argaddr(1, &buf);
    argint(2, &n);
    if (argstr(0, path, MAXPATH) < 0 || n < 0)
        return -1;
    begin_op();
    if ((ip = namei_nofollow(path)) == 0) {
        end_op();
        return -1;
    }
    ilock(ip);
    int r = -1;
    if (ip->type == T_SYMLINK)
        r = readi(ip, 1, buf, 0, n < ip->size ? n : ip->size);
    iunlockput(ip);
    end_op();
    return r;
}

// lstat(path, st): like stat(), but of a symbolic link itself.
uint64 sys_lstat(void)
{
    char path[MAXPATH];
    uint64 addr;
    struct inode *ip;
    struct stat st;

    argaddr(1, &addr);
    if (argstr(0, path, MAXPATH) < 0)
        return -1;
    begin_op();
    if ((ip = namei_nofollow(path)) == 0) {
        end_op();
        return -1;
    }
    ilock(ip);
    stati(ip, &st);
    iunlockput(ip);
    end_op();
    return copyout(myproc()->pagetable, addr, (char *)&st, sizeof(st));
}

uint64 sys_open(void)
{
    char path[MAXPATH];
    int fd, omode;
    struct file *f;
    struct inode *ip;

    argint(1, &omode);
    if (argstr(0, path, MAXPATH) < 0)
        return -1;

    begin_op();

    if ((omode & O_CREATE) && !(omode & O_NOFOLLOW) && (ip = namei(path)) != 0) {
        // exists, maybe through a symbolic link: open that
        ilock(ip);
        if (ip->type != T_FILE && ip->type != T_DEVICE) {
            iunlockput(ip);
            end_op();
            return -1;
        }
    } else if (omode & O_CREATE) {
        ip = create(path, T_FILE, 0, 0);
        if (ip == 0) {
            end_op();
            return -1;
        }
    } else {
        ip = (omode & O_NOFOLLOW) ? namei_nofollow(path) : namei(path);
        if (ip == 0) {
            end_op();
            return -1;
        }
        ilock(ip);
        if ((ip->type == T_DIR && (omode & (O_WRONLY | O_RDWR))) || ip->type == T_SYMLINK) {
            iunlockput(ip);
            end_op();
            return -1;
        }
    }

    if (ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)) {
        iunlockput(ip);
        end_op();
        return -1;
    }

    if ((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0) {
        if (f)
            fileclose(f);
        iunlockput(ip);
        end_op();
        return -1;
    }

    if (ip->type == T_DEVICE) {
        f->type = FD_DEVICE;
        f->major = ip->major;
    } else {
        f->type = FD_INODE;
        f->off = 0;
    }
    f->ip = ip;
    f->readable = !(omode & O_WRONLY);
    f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

    if ((omode & O_TRUNC) && ip->type == T_FILE)
        itrunc(ip);

    iunlock(ip);
    end_op();

    return fd;
}

uint64 sys_mkdir(void)
{
    char path[MAXPATH];
    struct inode *ip;

    begin_op();
    if (argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0) {
        end_op();
        return -1;
    }
    iunlockput(ip);
    end_op();
    return 0;
}

uint64 sys_mknod(void)
{
    struct inode *ip;
    char path[MAXPATH];
    int major, minor;

    begin_op();
    argint(1, &major);
    argint(2, &minor);
    if (argstr(0, path, MAXPATH) < 0 ||
        (ip = create(path, T_DEVICE, major, minor)) == 0) {
        end_op();
        return -1;
    }
    iunlockput(ip);
    end_op();
    return 0;
}

uint64 sys_chdir(void)
{
    char path[MAXPATH];
    struct inode *ip;
    struct proc *p = myproc();

    begin_op();
    if (argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0) {
        end_op();
        return -1;
    }
    ilock(ip);
    if (ip->type != T_DIR) {
        iunlockput(ip);
        end_op();
        return -1;
    }
    iunlock(ip);
    iput(p->cwd);
    end_op();
    p->cwd = ip;
    return 0;
}

uint64 sys_pipe(void)
{
    uint64 fdarray; // user pointer to array of two integers
    struct file *rf, *wf;
    int fd0, fd1;
    struct proc *p = myproc();

    argaddr(0, &fdarray);
    if (pipealloc(&rf, &wf) < 0)
        return -1;
    fd0 = -1;
    if ((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0) {
        if (fd0 >= 0)
            p->ofile[fd0] = 0;
        fileclose(rf);
        fileclose(wf);
        return -1;
    }
    if (copyout(p->pagetable, fdarray, (char *)&fd0, sizeof(fd0)) < 0 ||
        copyout(p->pagetable, fdarray + sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0) {
        p->ofile[fd0] = 0;
        p->ofile[fd1] = 0;
        fileclose(rf);
        fileclose(wf);
        return -1;
    }
    return 0;
}

uint64 sys_exec(void)
{
    char path[MAXPATH], *argv[MAXARG];
    int i;
    uint64 uargv, uarg;

    argaddr(1, &uargv);
    if (argstr(0, path, MAXPATH) < 0)
        return -1;
    memset(argv, 0, sizeof(argv));
    for (i = 0;; i++) {
        if (i >= NELEM(argv))
            goto bad;
        if (fetchaddr(uargv + sizeof(uint64) * i, (uint64 *)&uarg) < 0)
            goto bad;
        if (uarg == 0) {
            argv[i] = 0;
            break;
        }
        argv[i] = kalloc();
        if (argv[i] == 0)
            goto bad;
        if (fetchstr(uarg, argv[i], PGSIZE) < 0)
            goto bad;
    }

    int ret = kexec(path, argv);

    for (i = 0; i < NELEM(argv) && argv[i] != 0; i++)
        kfree(argv[i]);

    return ret;

bad:
    for (i = 0; i < NELEM(argv) && argv[i] != 0; i++)
        kfree(argv[i]);
    return -1;
}
