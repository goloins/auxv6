#ifndef _NET_H_
#define _NET_H_

#include "types.h"

// Network buffer (mbuf-style)
#define MBUF_SIZE 2048
#define IFNAMSIZ 16
#define MAXNETIF 16
#define ETH_ADDR_LEN 6
#define NET_ROUTE_TABLE_MAX 128
#define NET_ARP_CACHE_MAX 128

// Protocol IDs
#define NET_PROTO_IP   0x0800
#define NET_IP_ICMP    1
#define NET_IP_UDP     17
#define NET_IP_TCP     6

// Interface flags (subset, BSD-style naming)
#define IFF_UP        0x1
#define IFF_BROADCAST 0x2
#define IFF_LOOPBACK  0x8
#define IFF_RUNNING   0x40

// Link state (BSD-style, normalized for driver parity reporting)
#define LINK_STATE_UNKNOWN 0
#define LINK_STATE_DOWN    1
#define LINK_STATE_UP      2

// Route flags.
#define RTF_UP        0x1
#define RTF_GATEWAY   0x2

static inline ushort
net_htons(ushort x)
{
  return (ushort)(((x & 0x00ff) << 8) | ((x >> 8) & 0x00ff));
}

static inline ushort
net_ntohs(ushort x)
{
  return net_htons(x);
}

static inline uint
net_htonl(uint x)
{
  return ((x & 0x000000ffU) << 24) |
         ((x & 0x0000ff00U) << 8) |
         ((x & 0x00ff0000U) >> 8) |
         ((x & 0xff000000U) >> 24);
}

static inline uint
net_ntohl(uint x)
{
  return net_htonl(x);
}

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
  uchar tos;
  ushort len;
  ushort id;
  ushort off;
  uchar ttl;
  uchar proto;
  ushort sum;
  uint src;
  uint dst;
} __attribute__((packed));

// Minimal UDP header for loopback stack development.
struct udp_hdr {
  ushort src_port;
  ushort dst_port;
  ushort len;
  ushort csum;
} __attribute__((packed));

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
} __attribute__((packed));

// Minimal ICMP echo header.
struct icmp_hdr {
  uchar type;
  uchar code;
  ushort csum;
  ushort ident;
  ushort seq;
} __attribute__((packed));

#define ICMP_ECHO_REPLY   0
#define ICMP_UNREACH      3   // Destination unreachable
#define ICMP_ECHO         8
#define ICMP_TIMXCEED     11  // Time exceeded (TTL expired)
#define ICMP_TIMXCEED_INTRANS 0  // TTL exceeded in transit
#define ICMP_UNREACH_PORT 3   // Port unreachable (code for ICMP_UNREACH)

// Network interface operations.
struct ifnet_ops {
  int (*if_output)(struct ifnet *ifp, struct mbuf *m);
  void (*if_poll)(struct ifnet *ifp);
};

// Network interface descriptor.
struct ifnet {
  uint if_index;
  char if_xname[IFNAMSIZ];
  uint if_mtu;
  uint if_flags;
    uint if_link_state;
    uint if_addr;
    uint if_netmask;
    uchar if_hwaddr[ETH_ADDR_LEN];
    void *if_softc;
  struct ifnet_ops *if_ops;
  void (*if_input)(struct ifnet *ifp, struct mbuf *m);
  struct ifnet *if_next;
  /* Per-interface traffic counters */
  uint if_ipackets;   /* inbound packets delivered */
  uint if_opackets;   /* outbound packets sent */
  uint if_ibytes;     /* inbound bytes (after ethernet strip) */
  uint if_obytes;     /* outbound bytes (after ethernet header prepend) */
  uint if_ierrors;    /* inbound errors: dropped/malformed */
  uint if_oerrors;    /* outbound errors: driver send failures */
};

struct route {
  uint rt_dst;
  uint rt_mask;
  uint rt_gateway;
  uint rt_src;
  uint rt_flags;
  struct ifnet *rt_ifp;
};

struct netif_info {
  uint if_index;
  char if_name[IFNAMSIZ];
  uint if_link_state;
    uint if_addr;
    uint if_netmask;
    uchar if_hwaddr[ETH_ADDR_LEN];
  uint if_mtu;
  uint if_flags;
};

struct route_info {
  uint rt_dst;
  uint rt_mask;
  uint rt_gateway;
  uint rt_src;
  uint rt_flags;
  uint if_index;
};

struct arp_info {
  uint ai_ip;
  uchar ai_mac[ETH_ADDR_LEN];
  uint ai_flags;
  uint ai_expires;
  uint if_index;
};

// Core network device layer.
void netdev_init(void);
void netdev_poll(void);
int if_register(struct ifnet *ifp);
struct ifnet* if_get(char *name);
struct ifnet* if_byindex(uint ifindex);
struct ifnet* if_first(void);
struct ifnet* if_next(struct ifnet *ifp);
int if_dump(struct netif_info *out, int max);
int if_output(struct ifnet *ifp, struct mbuf *m);
void if_input(struct ifnet *ifp, struct mbuf *m);
int if_set_addr(struct ifnet *ifp, uint addr, uint mask);
int if_set_addr_byindex(uint ifindex, uint addr, uint mask);

// Routing table.
void route_init(void);
int route_add(uint dst, uint mask, uint gateway, uint src, struct ifnet *ifp, uint flags);
int route_delete(uint dst, uint mask, struct ifnet *ifp);
struct ifnet* route_lookup(uint dst, uint *src, uint *gateway);
int route_dump(struct route_info *out, int max);

// Built-in interfaces.
void loopback_attach(void);
void arp_init(void);
int arp_resolve(struct ifnet *ifp, uint ip, uchar *mac, struct mbuf *pending);
int arp_dump(struct arp_info *out, int max);
void arp_input(struct ifnet *ifp, struct mbuf *m);
int ether_output(struct ifnet *ifp, struct mbuf *m, const uchar *dst, ushort type);
int ether_output_ip(struct ifnet *ifp, struct mbuf *m, uint next_hop);
void ether_input(struct ifnet *ifp, struct mbuf *m);

// IP layer.
int ip_output(struct ifnet *ifp, uchar proto, uint src, uint dst,
              char *payload, uint len);
int ip_output_ttl(struct ifnet *ifp, uchar proto, uint src, uint dst,
                  char *payload, uint len, uchar ttl);
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
