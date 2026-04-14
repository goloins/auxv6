#ifndef _POLL_H_
#define _POLL_H_

#include "sys/types.h"

#ifndef _NFDS_T
#define _NFDS_T
typedef unsigned int nfds_t;
#endif

struct pollfd {
  int fd;
  short events;
  short revents;
};

#define POLLIN   0x0001
#define POLLPRI  0x0002
#define POLLOUT  0x0004
#define POLLERR  0x0008
#define POLLHUP  0x0010
#define POLLNVAL 0x0020

int poll(struct pollfd *fds, nfds_t nfds, int timeout);

#endif
