#ifndef _NET_H_
#define _NET_H_

#include "types.h"

// Network buffer (mbuf-style)
#define MBUF_SIZE 2048
#define IFNAMSIZ 16

// Protocol IDs
#define NET_PROTO_IP   0x0800
#define NET_IP_ICMP    1
#define NET_IP_UDP     17
#define NET_IP_TCP     6

// Interface flags (subset, BSD-style naming)
#define IFF_UP        0x1
#define IFF_LOOPBACK  0x8

struct mbuf {
  char data[MBUF_SIZE];
  uint len;
  struct ifnet *rcvif;
  struct mbuf *next;
};

struct ifnet;
struct sockaddr_in;

// Minimal IPv4 header for loopback stack development.
struct ip_hdr {
  uchar vhl;
  uchar proto;
  ushort len;
  uint src;
  uint dst;
};

// Minimal UDP header for loopback stack development.
struct udp_hdr {
  ushort src_port;
  ushort dst_port;
  ushort len;
  ushort csum;
};

// Minimal TCP header skeleton for future transport implementation.
struct tcp_hdr {
  ushort src_port;
  ushort dst_port;
  uint seq;
  uint ack;
  uchar off;
  uchar flags;
  ushort win;
  ushort csum;
  ushort urg;
};

// Minimal ICMP echo header.
struct icmp_hdr {
  uchar type;
  uchar code;
  ushort csum;
  ushort ident;
  ushort seq;
};

#define ICMP_ECHO_REPLY 0
#define ICMP_ECHO       8

// Network interface operations.
struct ifnet_ops {
  int (*if_output)(struct ifnet *ifp, struct mbuf *m);
};

// Network interface descriptor.
struct ifnet {
  char if_xname[IFNAMSIZ];
  uint if_mtu;
  uint if_flags;
  struct ifnet_ops *if_ops;
  void (*if_input)(struct ifnet *ifp, struct mbuf *m);
  struct ifnet *if_next;
};

// Core network device layer.
void netdev_init(void);
int if_register(struct ifnet *ifp);
struct ifnet* if_get(char *name);
int if_output(struct ifnet *ifp, struct mbuf *m);
void if_input(struct ifnet *ifp, struct mbuf *m);

// Built-in interfaces.
void loopback_attach(void);

// IP layer.
int ip_output(struct ifnet *ifp, uchar proto, uint src, uint dst,
              char *payload, uint len);
void ip_input(struct ifnet *ifp, struct mbuf *m);

// UDP layer.
int udp_output(struct ifnet *ifp, struct sockaddr_in *src,
               struct sockaddr_in *dst, char *payload, uint len);
void udp_input(struct ifnet *ifp, struct ip_hdr *ip, char *payload, uint len);

// ICMP layer.
void icmp_input(struct ifnet *ifp, struct ip_hdr *ip, char *payload, uint len);

// TCP layer (skeleton).
int tcp_output(struct ifnet *ifp, struct sockaddr_in *src,
               struct sockaddr_in *dst, char *payload, uint len);
void tcp_input(struct ifnet *ifp, struct ip_hdr *ip, char *payload, uint len);

// Buffer allocation.
struct mbuf* mbuf_alloc(void);
void mbuf_free(struct mbuf *m);

#endif
