#ifndef FCNTL_H
#define FCNTL_H

#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_CREATE  0x200
#define O_TRUNC   0x400

// lseek() whence
#define SEEK_SET  0
#define SEEK_CUR  1
#define SEEK_END  2

#endif
