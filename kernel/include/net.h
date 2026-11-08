#ifndef NET_H
#define NET_H

#include "types.h"

// The network stack (net.c): Ethernet, ARP, IPv4, ICMP, UDP and TCP
// (tcp.c) over virtio_net.c. Addresses are in host byte order here.

// qemu's user-mode network: we are 10.0.2.15, the host 10.0.2.2
#define NET_IP      IPADDR(10, 0, 2, 15)
#define NET_GATEWAY IPADDR(10, 0, 2, 2)
#define NET_MASK    IPADDR(255, 255, 255, 0)

#define UDP_MAXDATA 1472   // payload in one 1500-byte IP packet

extern uchar net_mac[6];

// virtio_net.c
void virtio_net_init(void);
int virtio_net_present(void);
int virtio_net_send(const void *frame, int len);

// net.c
void net_rx(char *frame, int len);
int net_send_udp(uint32 dst, uint16 sport, uint16 dport, const char *data, int len);
int net_send_icmp(uint32 dst, char *msg, int len);
int net_send_tcp(uint32 dst, char *seg, int len);
int net_arp_wait(uint32 dst);

// tcp.c
struct tcb;
void tcp_init(void);
void tcp_rx(uint32 src, char *seg, int len);
void tcp_tick(void);
struct tcb *tcp_connect(uint16 lport, uint32 rip, uint16 rport);
int tcp_read(struct tcb *t, uint64 dst, int n, int nonblock);
int tcp_write(struct tcb *t, uint64 src, int n);
int tcp_poll(struct tcb *t);
void tcp_close(struct tcb *t);

// socket.c: deliver a packet's payload to the socket bound to port
// (UDP) or using echo id port (ICMP)
void sock_deliver(int proto, uint16 port, uint32 src, uint16 sport, const char *data, int len);
void sock_tick(void);

#endif
