#ifndef FCNTL_H
#define FCNTL_H

#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_CREATE  0x200
#define O_TRUNC   0x400
#define O_NOFOLLOW 0x800   // fail if the path names a symbolic link
#define O_NONBLOCK 0x1000  // reads return -1 at once when there is nothing

// fcntl() commands
#define F_GETFL 3
#define F_SETFL 4          // only O_NONBLOCK can change

// ioctl() requests
#define CONSOLE_RAW 1   // console input raw (arg 1) or by lines (arg 0)

// lseek() whence
#define SEEK_SET  0
#define SEEK_CUR  1
#define SEEK_END  2

#endif
