#ifndef SOCKET_H
#define SOCKET_H

#include "types.h"

// Sockets (socket.c), shared with user space: IPv4 datagrams only,
// UDP or ICMP echo ("ping sockets", as on Linux). Linux's values.

#define AF_INET      2
#define SOCK_DGRAM   2
#define IPPROTO_ICMP 1
#define IPPROTO_UDP  17

#define SOL_SOCKET   1
#define SO_RCVTIMEO  20   // struct timeval: give up recvfrom() after it

#define MSG_DONTWAIT 0x40

struct in_addr {
    uint32 s_addr;        // network byte order
};

struct sockaddr_in {
    uint16 sin_family;    // AF_INET
    uint16 sin_port;      // network byte order
    struct in_addr sin_addr;
    char sin_zero[8];
};

struct timeval {
    uint64 tv_sec;
    uint64 tv_usec;
};

// byte order: both arches are little-endian
static inline uint16 htons(uint16 x) { return (x << 8) | (x >> 8); }
static inline uint16 ntohs(uint16 x) { return htons(x); }
static inline uint32 htonl(uint32 x) {
    return (x << 24) | ((x & 0xff00) << 8) | ((x >> 8) & 0xff00) | (x >> 24);
}
static inline uint32 ntohl(uint32 x) { return htonl(x); }

// a.b.c.d, in host byte order
#define IPADDR(a, b, c, d) (((uint32)(a) << 24) | ((b) << 16) | ((c) << 8) | (d))

#endif
