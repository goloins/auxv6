#ifndef _SOCKET_H_
#define _SOCKET_H_

#include "types.h"

// Socket address family
#define AF_INET       2
#define AF_UNIX       1

// Socket types
#define SOCK_STREAM   1     // TCP
#define SOCK_DGRAM    2     // UDP
#define SOCK_RAW      3     // Raw IP payloads (e.g. ICMP)

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

// recvtimeout() returns this value when no data arrives before the timeout.
#define RECV_TIMEOUT_EXPIRED (-2)

// Special address
#define INADDR_LOOPBACK  0x7f000001    // 127.0.0.1
#define INADDR_ANY       0x00000000

// Internet address
struct in_addr {
  uint s_addr;
};

// sockaddr_in structure
struct sockaddr_in {
  uchar  sin_family;
  ushort sin_port;
  uint   sin_addr;
  char   sin_zero[8];
};

// Internet protocol numbers
#define IPPROTO_ICMP   1
#define IPPROTO_UDP    17
#define IPPROTO_TCP    6

struct tcpcb {
  uint state;
  uint iss;           // Initial send sequence number
  uint irs;           // Initial receive sequence number
  uint snd_una;       // Oldest unacknowledged sequence number
  uint snd_nxt;       // Next sequence number to send
  uint rcv_nxt;       // Next sequence number expected to receive
  uint rcv_wnd;       // Receive window size
  
  // Retransmission
  uint rto;           // Retransmission timeout (ticks)
  uint rtt_est;       // Smoothed RTT estimate (ticks)
  uint retransmits;   // Number of retransmissions attempted
  uint last_send;     // Tick count of last send (for RTT and retransmit)
  
  // Unacked data buffer for retransmission (simplified: single segment)
  char *unacked_buf;  // Buffer of unacked data (null if none)
  uint unacked_len;   // Length of unacked data
  uint unacked_seq;   // Sequence number of unacked data
  
  // Teardown
  uint fin_seq;       // Sequence number of our FIN (if sent)
  uint time_wait_start; // Tick when TIME_WAIT started
};

#define TCP_RTO_INIT     100   // Initial RTO: ~1 second at 100Hz
#define TCP_RTO_MIN      20    // Minimum RTO: 200ms
#define TCP_RTO_MAX      6000  // Maximum RTO: 60 seconds
#define TCP_TIME_WAIT_TICKS 12000 // 2*MSL = 2 minutes at 100Hz
#define TCP_MAX_RETRANSMIT 5  // Max retransmits before giving up

#define SOCKET_LISTENQ_MAX 16

// Socket structure (kernel side)
struct socket {
  uint state;           // SOCK_* state
  uint type;            // SOCK_STREAM, SOCK_DGRAM
  uint protocol;        // IPPROTO_* for raw sockets
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

  // Stream listen queue for pending accepted sockets.
  uint backlog;
  uint qhead;
  uint qtail;
  uint qlen;
  struct socket *listenq[SOCKET_LISTENQ_MAX];

  // Ref count for cleanup
  uint ref;
};

#endif
