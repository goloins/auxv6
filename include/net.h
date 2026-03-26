#ifndef _NET_H_
#define _NET_H_

#include "types.h"

// Forward declarations
struct socket;

// Network buffer (mbuf-style)
#define MBUF_SIZE 2048
#define NET_NBUF 128

struct mbuf {
  char data[MBUF_SIZE];
  uint len;
  struct mbuf *next;
};

// Device operations
struct netdev_ops {
  int (*tx)(struct socket *sock, char *data, uint len);
  int (*rx)(struct socket *sock, char *buf, uint len);
};

// Network device
struct netdev {
  char name[16];
  struct netdev_ops *ops;
  uint flags;
};

// Device registration
void netdev_register(struct netdev *dev);
struct netdev* netdev_get(char *name);

// Buffer allocation
struct mbuf* mbuf_alloc(void);
void mbuf_free(struct mbuf *m);

#endif
