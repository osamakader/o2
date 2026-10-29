#ifndef POLL_H
#define POLL_H

// poll(), shared with user space.

#define POLLIN   0x01   // there is something to read (or its end)
#define POLLOUT  0x04   // a write wouldn't wait
#define POLLHUP  0x10   // the other end is closed (always reported)
#define POLLNVAL 0x20   // fd isn't open (always reported)

struct pollfd {
    int fd;
    short events;    // POLLIN | POLLOUT: what to wait for
    short revents;   // what is so
};

#endif
