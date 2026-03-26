#ifndef _NET_H_
#define _NET_H_

#include "types.h"

// Network buffer (mbuf-style)
#define MBUF_SIZE 2048
#define IFNAMSIZ 16

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

// Buffer allocation.
struct mbuf* mbuf_alloc(void);
void mbuf_free(struct mbuf *m);

#endif
