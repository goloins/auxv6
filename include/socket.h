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

// TCP states (BSD-inspired subset)
#define TCPS_CLOSED       0
#define TCPS_LISTEN       1
#define TCPS_SYN_SENT     2
#define TCPS_SYN_RECEIVED 3
#define TCPS_ESTABLISHED  4
#define TCPS_FIN_WAIT_1   5
#define TCPS_FIN_WAIT_2   6
#define TCPS_CLOSE_WAIT   7
#define TCPS_LAST_ACK     8
#define TCPS_TIME_WAIT    9

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

struct tcpcb {
  uint state;
  uint iss;
  uint snd_nxt;
  uint irs;
  uint rcv_nxt;
};

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

  // Transport control state.
  struct tcpcb tcp;

  // Ref count for cleanup
  uint ref;
};

#endif
