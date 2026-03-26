#ifndef _SOCKET_H_
#define _SOCKET_H_

#include "types.h"

// Socket address family
#define AF_INET       2
#define AF_UNIX       1

// Socket types
#define SOCK_STREAM   1     // TCP
#define SOCK_DGRAM    2     // UDP

// Socket states
#define SOCK_CLOSED   0
#define SOCK_BOUND    1
#define SOCK_LISTEN   2
#define SOCK_CONNECT  3
#define SOCK_ESTAB    4     // Established

// Special address
#define INADDR_LOOPBACK  0x7f000001    // 127.0.0.1
#define INADDR_ANY       0x00000000

// sockaddr_in structure
struct sockaddr_in {
  uchar  sin_family;
  ushort sin_port;
  uint   sin_addr;
  char   sin_zero[8];
};

// Internet protocol numbers
#define IPPROTO_UDP    17
#define IPPROTO_TCP    6

// Socket structure (kernel side)
struct socket {
  uint state;           // SOCK_* state
  uint type;            // SOCK_STREAM, SOCK_DGRAM
  uint family;          // AF_INET, AF_UNIX

  // Addressing
  struct sockaddr_in local_addr;
  struct sockaddr_in remote_addr;

  // Data buffers
  char *send_buf;
  char *recv_buf;
  uint send_len;
  uint recv_len;
  uint send_cap;
  uint recv_cap;

  // Ref count for cleanup
  uint ref;
};

#endif
