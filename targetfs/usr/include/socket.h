#ifndef _SOCKET_H_
#define _SOCKET_H_

#include "types.h"

struct file;

// Socket address family
#define AF_INET       2
#define AF_UNIX       1

#ifndef UNIX_PATH_MAX
#define UNIX_PATH_MAX 108
#endif

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

// sockaddr_in — POSIX layout: sin_family is 2 bytes so field offsets
// match the standard (sin_port at offset 2, sin_addr at offset 4).
struct sockaddr_in {
  ushort sin_family;    // Address family (AF_INET)
  ushort sin_port;      // Port number (host byte order internally)
  struct in_addr sin_addr; // IPv4 address (host byte order internally)
  char   sin_zero[8];   // Padding to make struct 16 bytes
};

// Internet protocol numbers
#define IPPROTO_IP     0
#define IPPROTO_ICMP   1
#define IPPROTO_UDP    17
#define IPPROTO_TCP    6

// setsockopt / getsockopt levels
#define SOL_SOCKET     1

// SOL_SOCKET options
#define SO_REUSEADDR   2    // Allow reuse of local addresses
#define SO_KEEPALIVE   9    // Enable keep-alive on TCP connections
#define SO_ERROR       4    // Get and clear pending socket error
#define SO_BROADCAST   6    // Allow broadcast sends

// IP-level socket options (level = IPPROTO_IP)
#define IP_TTL         2    // Time-to-live on outgoing packets
#define IP_HDRINCL     3    // Application provides full IP header (not implemented)

// shutdown() how values
#define SHUT_RD   0   // Shut down reading
#define SHUT_WR   1   // Shut down writing
#define SHUT_RDWR 2   // Shut down both

// MSG flags (for future send/recv extensions)
#define MSG_DONTWAIT  0x40   // Non-blocking I/O
#define MSG_PEEK      0x02   // Peek at data without consuming
#define MSG_WAITALL   0x100  // Wait for full request
#define MSG_NOSIGNAL  0x4000 // Don't raise SIGPIPE on broken stream

struct tcpcb {
  uint state;
  uint iss;           // Initial send sequence number
  uint irs;           // Initial receive sequence number
  uint snd_una;       // Oldest unacknowledged sequence number
  uint snd_nxt;       // Next sequence number to send
  uint snd_wnd;       // Sender's view of peer's receive window
  uint rcv_nxt;       // Next sequence number expected to receive
  uint rcv_wnd;       // Our receive window size (advertised to peer)

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

  // Set when socket_close() initiates FIN teardown; cleared when TCP
  // reaches CLOSED and releases the extra teardown ref.
  uint close_pending; // 1 = extra socket_deref needed on TCPS_CLOSED
};

#define TCP_RTO_INIT     100   // Initial RTO: ~1 second at 100Hz
#define TCP_RTO_MIN      20    // Minimum RTO: 200ms
#define TCP_RTO_MAX      6000  // Maximum RTO: 60 seconds
#define TCP_TIME_WAIT_TICKS 12000 // 2*MSL = 2 minutes at 100Hz
#define TCP_MAX_RETRANSMIT 5  // Max retransmits before giving up

#define SOCKET_LISTENQ_MAX 16
#define SOCKET_RIGHTS_QMAX 16

// Socket structure (kernel side)
struct socket {
  uint state;           // SOCK_* state
  uint type;            // SOCK_STREAM, SOCK_DGRAM
  uint protocol;        // IPPROTO_* for raw sockets
  uint family;          // AF_INET, AF_UNIX

  // Addressing
  struct sockaddr_in local_addr;
  struct sockaddr_in remote_addr;
  char unix_path[UNIX_PATH_MAX];

  // Data buffers
  char *send_buf;
  char *recv_buf;
  uint send_len;
  uint recv_len;
  uint send_cap;
  uint recv_cap;

  // Transport control state.
  struct tcpcb tcp;

  // Local socketpair peer linkage (AF_UNIX stream pairs).
  struct socket *peer;

  // Stream listen queue for pending accepted sockets.
  uint backlog;
  uint qhead;
  uint qtail;
  uint qlen;
  struct socket *listenq[SOCKET_LISTENQ_MAX];

  // Queued file references for SCM_RIGHTS transfer.
  uint rights_head;
  uint rights_tail;
  uint rights_len;
  struct file *rights_q[SOCKET_RIGHTS_QMAX];

  // Ref count for cleanup
  uint ref;

  // Per-socket IP options
  uchar ttl;            // Outgoing TTL (default 64; set via IP_TTL setsockopt)

  // Socket-level options
  uchar reuseaddr;      // SO_REUSEADDR: allow rebinding a used port
  uchar shut_rd;        // SHUT_RD: read side shut down
  uchar shut_wr;        // SHUT_WR: write side shut down
};

/*
 * Flat snapshot record produced by socket_get_table().
 * Safe to read outside socket_lock once the snapshot is complete.
 * All addresses and ports are in host byte order (same convention as
 * the rest of this codebase).
 */
struct socket_info_k {
  uint   family;       /* AF_INET */
  uint   type;         /* SOCK_STREAM, SOCK_DGRAM, SOCK_RAW */
  uint   state;        /* SOCK_* socket-level state */
  uint   tcp_state;    /* TCPS_* for SOCK_STREAM; 0 otherwise */
  uint   local_ip;     /* local address (host order) */
  ushort local_port;   /* local port (host order) */
  uint   remote_ip;    /* remote address (host order; 0 if unconnected) */
  ushort remote_port;  /* remote port (host order; 0 if unconnected) */
  uint   recv_len;     /* bytes pending in recv buffer */
  uint   send_len;     /* bytes pending in send buffer */
  int    pid;          /* owning process PID; -1 if not tracked */
};


#endif
