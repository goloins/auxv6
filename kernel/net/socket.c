// Socket management and syscall handlers

#include "../../include/types.h"
#include "../../include/defs.h"
#include "../../include/param.h"
#include "../../include/memlayout.h"
#include "../../include/mmu.h"
#include "../../include/proc.h"
#include "../../include/x86.h"
#include "../../include/spinlock.h"
#include "../../include/sleeplock.h"
#include "../../include/fs.h"
#include "../../include/file.h"
#include "../../include/socket.h"
#include "../../include/sys/socket.h"
#include "../../include/net.h"

struct socket sockets[NSOCKET];
struct spinlock socket_lock;
static ushort next_ephemeral = 40000;
uint tcp_iss = 1000;   // Shared ISN counter; also used by tcp.c via extern
static ushort alloc_ephemeral_port_locked(void);

#define SOCKET_MSG_IOV_MAX 16
#define SOCKET_MSG_BYTES_MAX 65536

// Allocate a file descriptor in the current process.
// Mirrors sysfile.c's internal fdalloc helper.
static int
socket_fdalloc(struct file *f)
{
  int fd;
  struct proc *curproc = myproc();
  int limit;

  if(curproc == 0 || curproc->fdtable == 0)
    return -1;

  limit = proc_fd_limit(curproc);

  for(fd = 0; fd < limit; fd++){
    while(fd >= curproc->fdtable->capacity){
      if(fdtable_grow(curproc->fdtable) < 0)
        return -1;
    }
    if(fd >= curproc->fdtable->nfds)
      curproc->fdtable->nfds = fd + 1;
    if(curproc->fdtable->entries[fd] == 0){
      curproc->fdtable->entries[fd] = f;
      return fd;
    }
  }

  return -1;
}

static void
socket_reset_locked(struct socket *s)
{
  int i;

  s->state = SOCK_CLOSED;
  s->type = 0;
  s->protocol = 0;
  s->family = 0;
  memset(&s->local_addr, 0, sizeof(s->local_addr));
  memset(&s->remote_addr, 0, sizeof(s->remote_addr));
  s->send_buf = 0;
  s->recv_buf = 0;
  s->send_len = 0;
  s->recv_len = 0;
  s->send_cap = 0;
  s->recv_cap = 0;
  s->tcp.state = TCPS_CLOSED;
  s->tcp.iss = 0;
  s->tcp.snd_nxt = 0;
  s->tcp.snd_una = 0;
  s->tcp.snd_wnd = 0;
  s->tcp.irs = 0;
  s->tcp.rcv_nxt = 0;
  s->tcp.rcv_wnd = 0;
  s->tcp.close_pending = 0;
  s->tcp.fin_seq = 0;
  s->tcp.time_wait_start = 0;
  s->tcp.unacked_buf = 0;
  s->tcp.unacked_len = 0;
  s->tcp.unacked_seq = 0;
  s->peer = 0;
  s->backlog = 0;
  s->qhead = 0;
  s->qtail = 0;
  s->qlen = 0;
  for(i = 0; i < SOCKET_LISTENQ_MAX; i++)
    s->listenq[i] = 0;
  s->rights_head = 0;
  s->rights_tail = 0;
  s->rights_len = 0;
  for(i = 0; i < SOCKET_RIGHTS_QMAX; i++)
    s->rights_q[i] = 0;
  s->ttl = 64;
  s->reuseaddr = 0;
  s->shut_rd = 0;
  s->shut_wr = 0;
}

static void
socket_free_locked(struct socket *s)
{
  uint i;
  struct socket *child;
  struct file *rf;

  for(i = 0; i < s->qlen; i++) {
    child = s->listenq[(s->qhead + i) % SOCKET_LISTENQ_MAX];
    if(child && child->ref > 0) {
      child->ref = 0;
      if(child->send_buf) {
        kfree(child->send_buf);
        child->send_buf = 0;
      }
      if(child->recv_buf) {
        kfree(child->recv_buf);
        child->recv_buf = 0;
      }
      socket_reset_locked(child);
    }
  }

  if(s->send_buf) {
    kfree(s->send_buf);
    s->send_buf = 0;
  }
  if(s->recv_buf) {
    kfree(s->recv_buf);
    s->recv_buf = 0;
  }

  while(s->rights_len > 0) {
    rf = s->rights_q[s->rights_head];
    s->rights_q[s->rights_head] = 0;
    s->rights_head = (s->rights_head + 1) % SOCKET_RIGHTS_QMAX;
    s->rights_len--;
    if(rf)
      fileclose(rf);
  }

  if(s->peer)
    s->peer->peer = 0;

  socket_reset_locked(s);
}

static struct socket*
socket_alloc_locked(void)
{
  int i;

  for(i = 0; i < NSOCKET; i++) {
    if(sockets[i].ref == 0) {
      sockets[i].ref = 1;
      socket_reset_locked(&sockets[i]);
      return &sockets[i];
    }
  }
  return 0;
}

static int
socket_buffers_init(struct socket *s)
{
  s->send_cap = 4096;
  s->recv_cap = 4096;
  s->send_buf = kalloc();
  s->recv_buf = kalloc();

  if(!s->send_buf || !s->recv_buf) {
    if(s->send_buf) {
      kfree(s->send_buf);
      s->send_buf = 0;
    }
    if(s->recv_buf) {
      kfree(s->recv_buf);
      s->recv_buf = 0;
    }
    return -1;
  }
  return 0;
}

static int
socket_listenq_enqueue_locked(struct socket *listener, struct socket *child)
{
  uint maxq;

  maxq = listener->backlog;
  if(maxq == 0 || maxq > SOCKET_LISTENQ_MAX)
    maxq = SOCKET_LISTENQ_MAX;
  if(listener->qlen >= maxq)
    return -1;

  listener->listenq[listener->qtail] = child;
  listener->qtail = (listener->qtail + 1) % SOCKET_LISTENQ_MAX;
  listener->qlen++;
  return 0;
}

static struct socket*
socket_listenq_dequeue_locked(struct socket *listener)
{
  struct socket *s;

  if(listener->qlen == 0)
    return 0;

  s = listener->listenq[listener->qhead];
  listener->listenq[listener->qhead] = 0;
  listener->qhead = (listener->qhead + 1) % SOCKET_LISTENQ_MAX;
  listener->qlen--;
  return s;
}

void
socket_init(void)
{
  int i;

  initlock(&socket_lock, "socket");
  lockdep_set_rank(&socket_lock, LOCK_RANK_DEFAULT, "socket");
  
  // Initialize socket table
  for(i = 0; i < NSOCKET; i++) {
    socket_reset_locked(&sockets[i]);
    sockets[i].ref = 0;
  }
}

static int
socket_stream_prepare_client_locked(struct socket *s)
{
  ushort p;

  if(s->local_addr.sin_port == 0) {
    p = alloc_ephemeral_port_locked();
    if(p == 0)
      return -1;
    s->local_addr.sin_family = AF_INET;
    s->local_addr.sin_port = p;
  }

  s->tcp.state = TCPS_SYN_SENT;
  s->tcp.iss = tcp_iss++;
  s->tcp.snd_nxt = s->tcp.iss + 1;
  return 0;
}

static void
socket_stream_mark_established_locked(struct socket *client,
                                      struct socket *accepted,
                                      struct sockaddr_in *remote)
{
  memmove(&client->remote_addr, remote, sizeof(*remote));
  client->state = SOCK_ESTAB;
  client->tcp.irs = accepted->tcp.iss;
  client->tcp.rcv_nxt = client->tcp.irs + 1;
  client->tcp.state = TCPS_ESTABLISHED;

  accepted->state = SOCK_ESTAB;
  accepted->tcp.state = TCPS_ESTABLISHED;
}

static int
socket_stream_eof_locked(struct socket *s)
{
  if(s == 0 || s->type != SOCK_STREAM)
    return 0;
  if(s->recv_len != 0)
    return 0;
  if(s->family == AF_UNIX)
    return (s->peer == 0 || s->peer->state == SOCK_CLOSED);
  if(s->state == SOCK_CLOSED)
    return 1;
  return s->tcp.state == TCPS_CLOSE_WAIT || s->tcp.state == TCPS_CLOSED;
}

static int
socket_recv_copy_locked(struct socket *s, char *buf, int len, struct sockaddr_in *peer)
{
  int n;

  n = len;
  if((uint)n > s->recv_len)
    n = (int)s->recv_len;

  if(n > 0)
    memmove(buf, s->recv_buf, (uint)n);
  if((uint)n < s->recv_len)
    memmove(s->recv_buf, s->recv_buf + n, s->recv_len - (uint)n);
  s->recv_len -= (uint)n;

  if(peer)
    memmove(peer, &s->remote_addr, sizeof(*peer));

  return n;
}

static struct file*
fd_get_file_ref(int fd)
{
  struct proc *p;
  struct file *f;

  p = myproc();
  if(p == 0 || p->fdtable == 0)
    return 0;
  if(fd < 0 || fd >= p->fdtable->nfds)
    return 0;

  f = p->fdtable->entries[fd];
  if(f == 0)
    return 0;

  filedup(f);
  return f;
}

static void
fd_close_local(int fd)
{
  struct proc *p;
  struct file *f;

  p = myproc();
  if(p == 0 || p->fdtable == 0)
    return;
  if(fd < 0 || fd >= p->fdtable->nfds)
    return;

  f = p->fdtable->entries[fd];
  if(f == 0)
    return;

  p->fdtable->entries[fd] = 0;
  p->fdtable->fdflags[fd] = 0;
  fileclose(f);
}

static int
socket_rights_enqueue_locked(struct socket *s, struct file *rf)
{
  if(s == 0 || rf == 0)
    return -1;
  if(s->rights_len >= SOCKET_RIGHTS_QMAX)
    return -1;

  s->rights_q[s->rights_tail] = rf;
  s->rights_tail = (s->rights_tail + 1) % SOCKET_RIGHTS_QMAX;
  s->rights_len++;
  return 0;
}

static struct file*
socket_rights_dequeue_locked(struct socket *s)
{
  struct file *rf;

  if(s == 0 || s->rights_len == 0)
    return 0;

  rf = s->rights_q[s->rights_head];
  s->rights_q[s->rights_head] = 0;
  s->rights_head = (s->rights_head + 1) % SOCKET_RIGHTS_QMAX;
  s->rights_len--;
  return rf;
}

static void
socket_rights_drop_locked(struct socket *s, int n)
{
  struct file *rf;

  if(s == 0 || n <= 0)
    return;

  while(n > 0 && s->rights_len > 0) {
    rf = socket_rights_dequeue_locked(s);
    if(rf)
      fileclose(rf);
    n--;
  }
}

static int
socket_local_stream_send_locked(struct socket *s, const char *buf, int len,
                                struct file **rfs, int rcount)
{
  struct socket *peer;
  uint free_space;
  int n;
  int i;

  if(s == 0 || s->family != AF_UNIX || s->type != SOCK_STREAM)
    return -1;
  if(s->shut_wr)
    return -1;

  peer = s->peer;
  if(peer == 0 || peer->state == SOCK_CLOSED || peer->shut_rd)
    return -1;

  if(rcount < 0 || rcount > SOCKET_RIGHTS_QMAX)
    return -1;
  if(peer->rights_len + (uint)rcount > SOCKET_RIGHTS_QMAX)
    return -1;

  free_space = peer->recv_cap - peer->recv_len;
  if(free_space == 0)
    return -1;

  n = len;
  if((uint)n > free_space)
    n = (int)free_space;
  if(n > 0)
    memmove(peer->recv_buf + peer->recv_len, buf, (uint)n);
  peer->recv_len += (uint)n;

  for(i = 0; i < rcount; i++) {
    if(rfs[i] && socket_rights_enqueue_locked(peer, rfs[i]) < 0) {
      if((uint)n <= peer->recv_len)
        peer->recv_len -= (uint)n;
      while(i > 0) {
        i--;
        socket_rights_drop_locked(peer, 1);
      }
      return -1;
    }
  }

  wakeup(peer);
  return n;
}

static void
socket_stream_window_update(struct socket *s, int nread)
{
  if(s == 0 || nread <= 0)
    return;
  if(s->type != SOCK_STREAM)
    return;
  tcp_send_ack(s);
}

int
socket_deliver(struct sockaddr_in *src, struct sockaddr_in *dst, char *data, uint len)
{
  int i;
  struct socket *rs;
  uint copylen = 0;

  if(src == 0 || dst == 0 || data == 0)
    return -1;

  rs = 0;
  acquire(&socket_lock);

  // Prefer exact established stream match first so accepted sockets receive data.
  for(i = 0; i < NSOCKET; i++) {
    if(sockets[i].ref == 0)
      continue;
    if(sockets[i].family != AF_INET)
      continue;
    if(sockets[i].type != SOCK_STREAM)
      continue;
    if(sockets[i].state != SOCK_ESTAB)
      continue;
    if(sockets[i].local_addr.sin_port != dst->sin_port)
      continue;
    if(sockets[i].remote_addr.sin_port != src->sin_port)
      continue;
    if(sockets[i].remote_addr.sin_addr.s_addr != src->sin_addr.s_addr)
      continue;
    if(sockets[i].local_addr.sin_addr.s_addr != INADDR_ANY &&
       sockets[i].local_addr.sin_addr.s_addr != dst->sin_addr.s_addr)
      continue;
    rs = &sockets[i];
    break;
  }

  // Datagram fallback keyed by local bind.
  if(rs == 0) {
    for(i = 0; i < NSOCKET; i++) {
      if(sockets[i].ref == 0)
        continue;
      if(sockets[i].family != AF_INET)
        continue;
      if(sockets[i].type != SOCK_DGRAM)
        continue;
      if(sockets[i].local_addr.sin_port != dst->sin_port)
        continue;
      if(sockets[i].state != SOCK_BOUND && sockets[i].state != SOCK_CONNECT &&
         sockets[i].state != SOCK_ESTAB)
        continue;
      if(sockets[i].local_addr.sin_addr.s_addr != INADDR_ANY &&
         sockets[i].local_addr.sin_addr.s_addr != dst->sin_addr.s_addr)
        continue;
      rs = &sockets[i];
      break;
    }
  }

  if(rs && rs->recv_buf && rs->recv_cap > rs->recv_len) {
    copylen = len;
    if(copylen > rs->recv_cap - rs->recv_len)
      copylen = rs->recv_cap - rs->recv_len;
    memmove(rs->recv_buf + rs->recv_len, data, copylen);
    rs->recv_len += copylen;
    memmove(&rs->remote_addr, src, sizeof(*src));
    wakeup(rs);
  }
  release(&socket_lock);

  return rs ? (int)copylen : -1;
}

int
socket_deliver_raw(uchar proto, struct sockaddr_in *src, struct sockaddr_in *dst,
                   char *data, uint len)
{
  int i;
  struct socket *rs;
  uint copylen;

  if(src == 0 || dst == 0 || data == 0)
    return -1;

  rs = 0;
  copylen = 0;
  acquire(&socket_lock);

  // Prefer connected raw sockets so packets are delivered to the intended peer.
  for(i = 0; i < NSOCKET; i++) {
    if(sockets[i].ref == 0)
      continue;
    if(sockets[i].family != AF_INET || sockets[i].type != SOCK_RAW)
      continue;
    if(sockets[i].protocol != 0 && sockets[i].protocol != proto)
      continue;
    if(sockets[i].state != SOCK_CONNECT)
      continue;
    if(sockets[i].remote_addr.sin_addr.s_addr != 0 &&
       sockets[i].remote_addr.sin_addr.s_addr != src->sin_addr.s_addr)
      continue;
    if(sockets[i].local_addr.sin_addr.s_addr != INADDR_ANY &&
       sockets[i].local_addr.sin_addr.s_addr != dst->sin_addr.s_addr)
      continue;
    rs = &sockets[i];
    break;
  }

  if(rs == 0) {
  for(i = 0; i < NSOCKET; i++) {
    if(sockets[i].ref == 0)
      continue;
    if(sockets[i].family != AF_INET || sockets[i].type != SOCK_RAW)
      continue;
    if(sockets[i].protocol != 0 && sockets[i].protocol != proto)
      continue;
    if(sockets[i].state != SOCK_BOUND && sockets[i].state != SOCK_CLOSED)
      continue;
    if(sockets[i].state == SOCK_BOUND &&
       sockets[i].local_addr.sin_addr.s_addr != INADDR_ANY &&
       sockets[i].local_addr.sin_addr.s_addr != dst->sin_addr.s_addr)
      continue;
    rs = &sockets[i];
    break;
  }
  }

  if(rs && rs->recv_buf && rs->recv_cap > rs->recv_len) {
    copylen = len;
    if(copylen > rs->recv_cap - rs->recv_len)
      copylen = rs->recv_cap - rs->recv_len;
    memmove(rs->recv_buf + rs->recv_len, data, copylen);
    rs->recv_len += copylen;
    memmove(&rs->remote_addr, src, sizeof(*src));
    wakeup(rs);
  }
  release(&socket_lock);

  return rs ? (int)copylen : -1;
}

static ushort
alloc_ephemeral_port_locked(void)
{
  int i, tries;
  ushort p;
  int inuse;

  for(tries = 0; tries < 20000; tries++) {
    p = next_ephemeral++;
    if(next_ephemeral < 40000)
      next_ephemeral = 40000;

    inuse = 0;
    for(i = 0; i < NSOCKET; i++) {
      if(sockets[i].ref > 0 && sockets[i].local_addr.sin_port == p) {
        inuse = 1;
        break;
      }
    }
    if(!inuse)
      return p;
  }
  return 0;
}

// Exported wrapper so tcp.c can use the same allocator.
ushort
socket_alloc_port_locked(void)
{
  return alloc_ephemeral_port_locked();
}

int
socket_stream_connect(struct socket *s, struct sockaddr_in *remote)
{
  int i;
  struct socket *listener;
  struct socket *accepted;

  if(s == 0 || remote == 0)
    return -1;

  accepted = socket_alloc();
  if(accepted == 0)
    return -1;
  if(socket_buffers_init(accepted) < 0) {
    socket_deref(accepted);
    return -1;
  }

  acquire(&socket_lock);

  if(s->family != AF_INET || s->type != SOCK_STREAM) {
    release(&socket_lock);
    socket_deref(accepted);
    return -1;
  }

  if(s->state == SOCK_LISTEN || s->tcp.state != TCPS_CLOSED) {
    release(&socket_lock);
    socket_deref(accepted);
    return -1;
  }

  if(socket_stream_prepare_client_locked(s) < 0) {
    release(&socket_lock);
    socket_deref(accepted);
    return -1;
  }

  listener = 0;
  for(i = 0; i < NSOCKET; i++) {
    if(&sockets[i] == s || sockets[i].ref == 0)
      continue;
    if(sockets[i].family != AF_INET || sockets[i].type != SOCK_STREAM)
      continue;
    if(sockets[i].local_addr.sin_port != remote->sin_port)
      continue;
    if(sockets[i].state != SOCK_LISTEN)
      continue;
    if(sockets[i].local_addr.sin_addr.s_addr != INADDR_ANY &&
       sockets[i].local_addr.sin_addr.s_addr != remote->sin_addr.s_addr)
      continue;
    listener = &sockets[i];
    break;
  }

  if(listener == 0) {
    release(&socket_lock);
    socket_deref(accepted);
    return -1;
  }

  if(listener->qlen >= listener->backlog) {
    release(&socket_lock);
    socket_deref(accepted);
    return -1;
  }

  accepted->family = AF_INET;
  accepted->type = SOCK_STREAM;
  accepted->local_addr = listener->local_addr;
  accepted->remote_addr.sin_family = AF_INET;
  accepted->remote_addr.sin_addr.s_addr = s->local_addr.sin_addr.s_addr;
  accepted->remote_addr.sin_port = s->local_addr.sin_port;

  accepted->tcp.state = TCPS_SYN_RECEIVED;
  accepted->tcp.irs = s->tcp.iss;
  accepted->tcp.rcv_nxt = accepted->tcp.irs + 1;
  accepted->tcp.iss = tcp_iss++;
  accepted->tcp.snd_nxt = accepted->tcp.iss + 1;

  socket_stream_mark_established_locked(s, accepted, remote);

  if(socket_listenq_enqueue_locked(listener, accepted) < 0) {
    release(&socket_lock);
    socket_deref(accepted);
    return -1;
  }

  wakeup(listener);
  wakeup(s);

  release(&socket_lock);
  return 0;
}

// Allocate a socket
struct socket*
socket_alloc(void)
{
  struct socket *s;
  
  acquire(&socket_lock);
  s = socket_alloc_locked();
  release(&socket_lock);
  return s;
}

// Reference a socket
struct socket*
socket_ref(struct socket *s)
{
  if(!s) return 0;
  acquire(&socket_lock);
  if(s->ref > 0)
    s->ref++;
  else
    s = 0;
  release(&socket_lock);
  return s;
}

// Dereference a socket
void
socket_deref(struct socket *s)
{
  if(!s) return;
  
  acquire(&socket_lock);
  
  if(s->ref > 0)
    s->ref--;
  if(s->ref == 0)
    socket_free_locked(s);

  release(&socket_lock);
}

int
ksock_open_udp(struct socket **out)
{
  struct socket *s;

  if(out == 0)
    return -1;

  s = socket_alloc();
  if(s == 0)
    return -1;

  s->family = AF_INET;
  s->type = SOCK_DGRAM;
  s->protocol = IPPROTO_UDP;
  s->state = SOCK_CLOSED;

  if(socket_buffers_init(s) < 0) {
    socket_deref(s);
    return -1;
  }

  *out = s;
  return 0;
}

int
ksock_sendto(struct socket *s, struct sockaddr_in *dst, char *buf, uint len)
{
  struct sockaddr_in src;
  struct ifnet *ifp;
  uint route_src;

  if(s == 0 || dst == 0 || buf == 0)
    return -1;
  if(dst->sin_family != AF_INET || dst->sin_addr.s_addr == 0 || dst->sin_port == 0)
    return -1;
  if(len > MBUF_SIZE - sizeof(struct udp_hdr))
    return -1;

  acquire(&socket_lock);
  if(s->type != SOCK_DGRAM || s->family != AF_INET) {
    release(&socket_lock);
    return -1;
  }
  memmove(&src, &s->local_addr, sizeof(src));
  release(&socket_lock);

  route_src = 0;
  ifp = route_lookup(dst->sin_addr.s_addr, &route_src, 0);
  if(ifp == 0)
    return -1;

  if(src.sin_addr.s_addr == 0)
    src.sin_addr.s_addr = route_src;

  if(src.sin_port == 0) {
    ushort p;

    acquire(&socket_lock);
    if(s->local_addr.sin_port == 0) {
      p = alloc_ephemeral_port_locked();
      if(p == 0) {
        release(&socket_lock);
        return -1;
      }
      s->local_addr.sin_family = AF_INET;
      s->local_addr.sin_port = p;
      s->local_addr.sin_addr.s_addr = src.sin_addr.s_addr;
      if(s->state == SOCK_CLOSED)
        s->state = SOCK_BOUND;
    }
    src.sin_port = s->local_addr.sin_port;
    release(&socket_lock);
  }

  if(udp_output(ifp, &src, dst, buf, len) < 0)
    return -1;

  return (int)len;
}

int
ksock_recvfrom_timeout(struct socket *s, char *buf, uint len, int timeout_ticks,
                       struct sockaddr_in *src)
{
  int n;
  uint start;
  uint now;
  struct sockaddr_in peer;

  if(s == 0 || buf == 0)
    return -1;
  if(timeout_ticks < 0)
    return -1;
  if(timeout_ticks == 0)
    return 0;

  acquire(&tickslock);
  start = ticks;
  release(&tickslock);

  acquire(&socket_lock);
  while(s->recv_len == 0) {
    if(socket_stream_eof_locked(s)) {
      release(&socket_lock);
      return 0;
    }
    release(&socket_lock);

    acquire(&tickslock);
    now = ticks;
    if(now - start >= (uint)timeout_ticks) {
      release(&tickslock);
      return RECV_TIMEOUT_EXPIRED;
    }
    sleep(&ticks, &tickslock);
    release(&tickslock);

    acquire(&socket_lock);
  }

  n = socket_recv_copy_locked(s, buf, (int)len, &peer);

  release(&socket_lock);

  if(src)
    memmove(src, &peer, sizeof(peer));

  socket_stream_window_update(s, n);

  return n;
}

int
socket_fileread(struct file *f, char *dst, int n)
{
  struct socket *s;
  int r;

  if(f == 0 || f->type != FD_SOCKET || dst == 0)
    return -1;
  if(n < 0)
    return -1;
  if(n == 0)
    return 0;

  s = f->socket;
  if(s == 0)
    return -1;

  acquire(&socket_lock);
  while(s->recv_len == 0) {
    if(socket_stream_eof_locked(s)) {
      release(&socket_lock);
      return 0;
    }
    sleep(s, &socket_lock);
  }

  r = socket_recv_copy_locked(s, dst, n, 0);
  release(&socket_lock);

  socket_stream_window_update(s, r);
  return r;
}

int
socket_filewrite(struct file *f, char *src_buf, int n)
{
  struct socket *s;
  struct sockaddr_in src;
  struct sockaddr_in dst;
  struct ifnet *ifp;
  uint route_src;
  int type;
  uint proto;
  uint tcp_state;

  if(f == 0 || f->type != FD_SOCKET || src_buf == 0)
    return -1;
  if(n < 0)
    return -1;
  if(n == 0)
    return 0;

  s = f->socket;
  if(s == 0)
    return -1;

  if(s->family == AF_UNIX && s->type == SOCK_STREAM) {
    int sent;

    acquire(&socket_lock);
    sent = socket_local_stream_send_locked(s, src_buf, n, 0, 0);
    release(&socket_lock);
    return sent;
  }

  acquire(&socket_lock);
  if(s->shut_wr) {
    release(&socket_lock);
    return -1;
  }
  memmove(&src, &s->local_addr, sizeof(src));
  memmove(&dst, &s->remote_addr, sizeof(dst));
  type = (int)s->type;
  proto = s->protocol;
  tcp_state = s->tcp.state;
  release(&socket_lock);

  route_src = 0;
  ifp = route_lookup(dst.sin_addr.s_addr, &route_src, 0);
  if(ifp == 0)
    return -1;
  if(src.sin_addr.s_addr == 0)
    src.sin_addr.s_addr = route_src;

  if(type == SOCK_DGRAM) {
    if((uint)n > MBUF_SIZE - sizeof(struct udp_hdr))
      return -1;
    if(src.sin_port == 0 || dst.sin_port == 0)
      return -1;
    if(udp_output(ifp, &src, &dst, src_buf, (uint)n) < 0)
      return -1;
    return n;
  }

  if(type == SOCK_STREAM) {
    const char *sp;
    int remaining;
    int total_sent;
    int r;

    if(src.sin_port == 0 || dst.sin_port == 0)
      return -1;
    if(tcp_state != TCPS_ESTABLISHED)
      return -1;

    sp = src_buf;
    remaining = n;
    total_sent = 0;
    while(remaining > 0) {
      r = tcp_output(ifp, &src, &dst, (char*)sp, (uint)remaining);
      if(r <= 0)
        break;
      total_sent += r;
      sp += r;
      remaining -= r;
    }
    return (total_sent > 0) ? total_sent : -1;
  }

  if(type == SOCK_RAW) {
    uchar snd_ttl;

    if((uint)n > MBUF_SIZE - sizeof(struct ip_hdr))
      return -1;
    if(dst.sin_addr.s_addr == 0)
      return -1;
    if(src.sin_addr.s_addr == 0)
      src.sin_addr.s_addr = route_src;

    acquire(&socket_lock);
    snd_ttl = s->ttl ? s->ttl : 64;
    release(&socket_lock);

    if(ip_output_ttl(ifp, (uchar)proto, src.sin_addr.s_addr, dst.sin_addr.s_addr,
                     src_buf, (uint)n, snd_ttl) < 0)
      return -1;
    return n;
  }

  return -1;
}

// ============ SYSCALL HANDLERS ============

// socket(family, type, protocol) syscall
int
sys_socket(void)
{
  int family, type, protocol;
  struct socket *s;
  int fd;
  struct file *f;
  
  if(argint(0, &family) < 0 || argint(1, &type) < 0 || argint(2, &protocol) < 0)
    return -1;
  
  // Validate arguments
  if(family != AF_INET) {
    cprintf("socket: unsupported family %d\n", family);
    return -1;
  }
  
  if(type != SOCK_STREAM && type != SOCK_DGRAM && type != SOCK_RAW) {
    cprintf("socket: invalid type %d\n", type);
    return -1;
  }

  if(type == SOCK_RAW && protocol < 0)
    return -1;
  
  // Allocate socket
  s = socket_alloc();
  if(!s) {
    cprintf("socket: out of sockets\n");
    return -1;
  }
  
  s->family = family;
  s->type = type;
  s->protocol = (uint)protocol;
  s->state = SOCK_CLOSED;
  s->tcp.state = TCPS_CLOSED;

  if(socket_buffers_init(s) < 0) {
    socket_deref(s);
    return -1;
  }

  // Allocate a struct file wrapping this socket
  f = filealloc();
  if(!f) {
    socket_deref(s);
    return -1;
  }
  f->type = FD_SOCKET;
  f->socket = s;
  f->readable = 1;
  f->writable = 1;
  
  // Allocate a file descriptor for this process
  if((fd = socket_fdalloc(f)) < 0) {
    fileclose(f);
    return -1;
  }
  
  NETDBG("socket: created fd=%d family=%d type=%d\n", fd, family, type);
  
  return fd;
}

int
sys_socketpair(void)
{
  int domain;
  int type;
  int protocol;
  int sv_raw;
  int sv[2];
  int fd0;
  int fd1;
  struct socket *a;
  struct socket *b;
  struct file *fa;
  struct file *fb;
  struct proc *p;
  pde_t *pgdir;

  if(argint(0, &domain) < 0 || argint(1, &type) < 0 ||
     argint(2, &protocol) < 0 || argint(3, &sv_raw) < 0)
    return -1;

  if(domain != AF_UNIX)
    return -1;
  if(type != SOCK_STREAM)
    return -1;
  if(protocol != 0)
    return -1;

  a = socket_alloc();
  if(a == 0)
    return -1;
  b = socket_alloc();
  if(b == 0) {
    socket_deref(a);
    return -1;
  }

  if(socket_buffers_init(a) < 0 || socket_buffers_init(b) < 0) {
    socket_deref(a);
    socket_deref(b);
    return -1;
  }

  acquire(&socket_lock);
  a->family = AF_UNIX;
  a->type = SOCK_STREAM;
  a->protocol = 0;
  a->state = SOCK_ESTAB;
  a->tcp.state = TCPS_ESTABLISHED;
  a->peer = b;

  b->family = AF_UNIX;
  b->type = SOCK_STREAM;
  b->protocol = 0;
  b->state = SOCK_ESTAB;
  b->tcp.state = TCPS_ESTABLISHED;
  b->peer = a;
  release(&socket_lock);

  fa = filealloc();
  if(fa == 0) {
    socket_deref(a);
    socket_deref(b);
    return -1;
  }
  fb = filealloc();
  if(fb == 0) {
    fileclose(fa);
    socket_deref(a);
    socket_deref(b);
    return -1;
  }

  fa->type = FD_SOCKET;
  fa->socket = a;
  fa->readable = 1;
  fa->writable = 1;
  fb->type = FD_SOCKET;
  fb->socket = b;
  fb->readable = 1;
  fb->writable = 1;

  fd0 = socket_fdalloc(fa);
  if(fd0 < 0) {
    fileclose(fa);
    fileclose(fb);
    return -1;
  }
  fd1 = socket_fdalloc(fb);
  if(fd1 < 0) {
    fd_close_local(fd0);
    fileclose(fb);
    return -1;
  }

  sv[0] = fd0;
  sv[1] = fd1;

  p = myproc();
  pgdir = p ? proc_pgdir(p) : 0;
  if(pgdir == 0 || copyout(pgdir, (uint)sv_raw, sv, sizeof(sv)) < 0) {
    fd_close_local(fd0);
    fd_close_local(fd1);
    return -1;
  }

  return 0;
}

int
sys_sendmsg(void)
{
  int sockfd;
  int msg_raw;
  int flags;
  struct socket *s;
  struct msghdr msg;
  struct iovec *iov;
  char *kbuf;
  int i;
  int total;
  int copied;
  struct proc *p;
  pde_t *pgdir;
  struct file *right_files[SOCKET_RIGHTS_QMAX];
  int right_count;
  int sent;

  if(argint(0, &sockfd) < 0 || argint(1, &msg_raw) < 0 || argint(2, &flags) < 0)
    return -1;
  if(flags & ~(MSG_NOSIGNAL | MSG_DONTWAIT))
    return -1;

  p = myproc();
  pgdir = p ? proc_pgdir(p) : 0;
  if(pgdir == 0)
    return -1;

  if(copyin(pgdir, &msg, (uint)msg_raw, sizeof(msg)) < 0)
    return -1;
  if(msg.msg_iovlen <= 0 || msg.msg_iovlen > SOCKET_MSG_IOV_MAX)
    return -1;

  iov = (struct iovec*)kmalloc((uint)msg.msg_iovlen * sizeof(*iov));
  if(iov == 0)
    return -1;
  if(copyin(pgdir, iov, (uint)msg.msg_iov,
            (uint)msg.msg_iovlen * sizeof(*iov)) < 0) {
    kmalloc_free(iov);
    return -1;
  }

  total = 0;
  for(i = 0; i < msg.msg_iovlen; i++) {
    if(iov[i].iov_len > (size_t)(SOCKET_MSG_BYTES_MAX - total)) {
      kmalloc_free(iov);
      return -1;
    }
    total += (int)iov[i].iov_len;
  }

  if(total > 0) {
    kbuf = (char*)kmalloc((uint)total);
    if(kbuf == 0) {
      kmalloc_free(iov);
      return -1;
    }
  } else {
    kbuf = 0;
  }

  copied = 0;
  for(i = 0; i < msg.msg_iovlen; i++) {
    if(iov[i].iov_len == 0)
      continue;
    if(copyin(pgdir, kbuf + copied, (uint)iov[i].iov_base, (uint)iov[i].iov_len) < 0) {
      if(kbuf)
        kmalloc_free(kbuf);
      kmalloc_free(iov);
      return -1;
    }
    copied += (int)iov[i].iov_len;
  }

  right_count = 0;
  memset(right_files, 0, sizeof(right_files));
  if(msg.msg_control && msg.msg_controllen >= CMSG_LEN(sizeof(int))) {
    struct cmsghdr cmsg;
    int send_fds[SOCKET_RIGHTS_QMAX];
    int fd_bytes;
    int nfd;

    if(copyin(pgdir, &cmsg, (uint)msg.msg_control, sizeof(cmsg)) < 0) {
      if(kbuf)
        kmalloc_free(kbuf);
      kmalloc_free(iov);
      return -1;
    }
    if(cmsg.cmsg_level == SOL_SOCKET && cmsg.cmsg_type == SCM_RIGHTS &&
       cmsg.cmsg_len >= CMSG_LEN(sizeof(int)) &&
       msg.msg_controllen >= cmsg.cmsg_len) {
      fd_bytes = (int)cmsg.cmsg_len - (int)CMSG_LEN(0);
      if(fd_bytes <= 0 || (fd_bytes % (int)sizeof(int)) != 0) {
        if(kbuf)
          kmalloc_free(kbuf);
        kmalloc_free(iov);
        return -1;
      }
      nfd = fd_bytes / (int)sizeof(int);
      if(nfd > SOCKET_RIGHTS_QMAX) {
        if(kbuf)
          kmalloc_free(kbuf);
        kmalloc_free(iov);
        return -1;
      }
      if(copyin(pgdir, send_fds,
                (uint)msg.msg_control + (uint)CMSG_LEN(0), (uint)fd_bytes) < 0) {
        if(kbuf)
          kmalloc_free(kbuf);
        kmalloc_free(iov);
        return -1;
      }
      for(i = 0; i < nfd; i++) {
        right_files[right_count] = fd_get_file_ref(send_fds[i]);
        if(right_files[right_count] == 0) {
          while(right_count > 0) {
            right_count--;
            fileclose(right_files[right_count]);
            right_files[right_count] = 0;
          }
          if(kbuf)
            kmalloc_free(kbuf);
          kmalloc_free(iov);
          return -1;
        }
        right_count++;
      }
    }
  }

  s = getfd_socket(sockfd);
  if(s == 0) {
    while(right_count > 0) {
      right_count--;
      fileclose(right_files[right_count]);
    }
    if(kbuf)
      kmalloc_free(kbuf);
    kmalloc_free(iov);
    return -1;
  }

  if(s->family != AF_UNIX || s->type != SOCK_STREAM) {
    while(right_count > 0) {
      right_count--;
      fileclose(right_files[right_count]);
    }
    if(kbuf)
      kmalloc_free(kbuf);
    kmalloc_free(iov);
    return -1;
  }

  acquire(&socket_lock);
  sent = socket_local_stream_send_locked(s, kbuf, total, right_files, right_count);
  release(&socket_lock);

  if(sent < 0) {
    while(right_count > 0) {
      right_count--;
      fileclose(right_files[right_count]);
    }
  }

  if(kbuf)
    kmalloc_free(kbuf);
  kmalloc_free(iov);
  return sent;
}

int
sys_recvmsg(void)
{
  int sockfd;
  int msg_raw;
  int flags;
  struct socket *s;
  struct msghdr msg;
  struct iovec *iov;
  char *kbuf;
  int i;
  int total;
  int n;
  int copied;
  struct proc *p;
  pde_t *pgdir;
  int out_flags;
  int control_used;
  char cbuf[CMSG_SPACE(sizeof(int) * SOCKET_RIGHTS_QMAX)];
  int recv_fds[SOCKET_RIGHTS_QMAX];
  struct file *rf;

  if(argint(0, &sockfd) < 0 || argint(1, &msg_raw) < 0 || argint(2, &flags) < 0)
    return -1;
  if(flags & ~(MSG_PEEK | MSG_DONTWAIT))
    return -1;

  p = myproc();
  pgdir = p ? proc_pgdir(p) : 0;
  if(pgdir == 0)
    return -1;

  if(copyin(pgdir, &msg, (uint)msg_raw, sizeof(msg)) < 0)
    return -1;
  if(msg.msg_iovlen <= 0 || msg.msg_iovlen > SOCKET_MSG_IOV_MAX)
    return -1;

  iov = (struct iovec*)kmalloc((uint)msg.msg_iovlen * sizeof(*iov));
  if(iov == 0)
    return -1;
  if(copyin(pgdir, iov, (uint)msg.msg_iov,
            (uint)msg.msg_iovlen * sizeof(*iov)) < 0) {
    kmalloc_free(iov);
    return -1;
  }

  total = 0;
  for(i = 0; i < msg.msg_iovlen; i++) {
    if(iov[i].iov_len > (size_t)(SOCKET_MSG_BYTES_MAX - total)) {
      kmalloc_free(iov);
      return -1;
    }
    total += (int)iov[i].iov_len;
  }

  if(total > 0) {
    kbuf = (char*)kmalloc((uint)total);
    if(kbuf == 0) {
      kmalloc_free(iov);
      return -1;
    }
  } else {
    kbuf = 0;
  }

  s = getfd_socket(sockfd);
  if(s == 0 || s->family != AF_UNIX || s->type != SOCK_STREAM) {
    if(kbuf)
      kmalloc_free(kbuf);
    kmalloc_free(iov);
    return -1;
  }

  out_flags = 0;
  control_used = 0;
  rf = 0;

  acquire(&socket_lock);
  if(flags & MSG_DONTWAIT) {
    if(s->recv_len == 0) {
      if(socket_stream_eof_locked(s)) {
        release(&socket_lock);
        if(kbuf)
          kmalloc_free(kbuf);
        kmalloc_free(iov);
        return 0;
      }
      release(&socket_lock);
      if(kbuf)
        kmalloc_free(kbuf);
      kmalloc_free(iov);
      return -1;
    }
  } else {
    while(s->recv_len == 0) {
      if(socket_stream_eof_locked(s)) {
        release(&socket_lock);
        if(kbuf)
          kmalloc_free(kbuf);
        kmalloc_free(iov);
        return 0;
      }
      sleep(s, &socket_lock);
    }
  }

  n = total;
  if((uint)n > s->recv_len)
    n = (int)s->recv_len;
  if(n > 0)
    memmove(kbuf, s->recv_buf, (uint)n);
  if((uint)n < s->recv_len)
    memmove(s->recv_buf, s->recv_buf + n, s->recv_len - (uint)n);
  s->recv_len -= (uint)n;

  if(s->rights_len > 0) {
    int pending;
    int maxfds;
    int take;
    int ok;

    pending = (int)s->rights_len;
    if(msg.msg_control == 0 || msg.msg_controllen < CMSG_SPACE(sizeof(int))) {
      socket_rights_drop_locked(s, pending);
      out_flags |= MSG_CTRUNC;
    } else {
      struct cmsghdr *cmsg;

      maxfds = ((int)msg.msg_controllen - (int)CMSG_LEN(0)) / (int)sizeof(int);
      if(maxfds <= 0) {
        socket_rights_drop_locked(s, pending);
        out_flags |= MSG_CTRUNC;
      } else {
        take = pending;
        if(take > maxfds) {
          out_flags |= MSG_CTRUNC;
          take = maxfds;
        }

        ok = 0;
        for(i = 0; i < take; i++) {
          rf = socket_rights_dequeue_locked(s);
          if(rf == 0)
            break;
          recv_fds[ok] = socket_fdalloc(rf);
          if(recv_fds[ok] < 0) {
            fileclose(rf);
            out_flags |= MSG_CTRUNC;
            continue;
          }
          ok++;
        }

        if(pending > take)
          socket_rights_drop_locked(s, pending - take);

        if(ok > 0) {
        cmsg = (struct cmsghdr*)cbuf;
          cmsg->cmsg_len = CMSG_LEN((size_t)ok * sizeof(int));
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
          memmove(CMSG_DATA(cmsg), recv_fds, (uint)(ok * (int)sizeof(int)));
          control_used = CMSG_SPACE((size_t)ok * sizeof(int));
        }
      }
    }
  }
  release(&socket_lock);

  copied = 0;
  for(i = 0; i < msg.msg_iovlen && copied < n; i++) {
    int chunk;

    chunk = n - copied;
    if((size_t)chunk > iov[i].iov_len)
      chunk = (int)iov[i].iov_len;
    if(chunk > 0 && copyout(pgdir, (uint)iov[i].iov_base, kbuf + copied, (uint)chunk) < 0) {
      if(kbuf)
        kmalloc_free(kbuf);
      kmalloc_free(iov);
      return -1;
    }
    copied += chunk;
  }

  if(control_used > 0) {
    if(copyout(pgdir, (uint)msg.msg_control, cbuf, (uint)control_used) < 0) {
      if(kbuf)
        kmalloc_free(kbuf);
      kmalloc_free(iov);
      return -1;
    }
  }

  msg.msg_namelen = 0;
  msg.msg_controllen = (socklen_t)control_used;
  msg.msg_flags = out_flags;
  if(copyout(pgdir, (uint)msg_raw, &msg, sizeof(msg)) < 0) {
    if(kbuf)
      kmalloc_free(kbuf);
    kmalloc_free(iov);
    return -1;
  }

  if(kbuf)
    kmalloc_free(kbuf);
  kmalloc_free(iov);
  return n;
}

// bind(sockfd, addr, addrlen) syscall
int
sys_bind(void)
{
  int sockfd, addrlen, addr_raw;
  int i;
  struct socket *s;
  struct sockaddr_in addr;
  struct proc *p;
  pde_t *pgdir;
  
  if(argint(0, &sockfd) < 0 || argint(2, &addrlen) < 0)
    return -1;
  
  if(addrlen < (int)sizeof(struct sockaddr_in)) {
    cprintf("bind: invalid address length\n");
    return -1;
  }
  
  if(argint(1, &addr_raw) < 0)
    return -1;

  p = myproc();
  pgdir = p ? proc_pgdir(p) : 0;
  if(pgdir == 0)
    return -1;
  if(copyin(pgdir, &addr, (uint)addr_raw, sizeof(addr)) < 0)
    return -1;
  
  // Get socket from fd
  s = getfd_socket(sockfd);
  if(!s) {
    cprintf("bind: invalid socket fd %d\n", sockfd);
    return -1;
  }

  if(addr.sin_port == 0)
    return -1;
  
  acquire(&socket_lock);

  // Check for port conflict; allow if both sockets have SO_REUSEADDR.
  if(addr.sin_port != 0) {
    for(i = 0; i < NSOCKET; i++) {
      if(&sockets[i] == s)
        continue;
      if(sockets[i].ref == 0)
        continue;
      if(sockets[i].family != addr.sin_family)
        continue;
      if(sockets[i].local_addr.sin_port != addr.sin_port)
        continue;
      // Check address overlap: INADDR_ANY overlaps with everything.
      if(addr.sin_addr.s_addr != INADDR_ANY &&
         sockets[i].local_addr.sin_addr.s_addr != INADDR_ANY &&
         sockets[i].local_addr.sin_addr.s_addr != addr.sin_addr.s_addr)
        continue;
      // Port is in use.  Allow only if both sides have SO_REUSEADDR.
      if(!s->reuseaddr || !sockets[i].reuseaddr) {
        release(&socket_lock);
        return -1;
      }
    }
  }

  memmove(&s->local_addr, &addr, sizeof(struct sockaddr_in));
  s->state = SOCK_BOUND;
  release(&socket_lock);
  
  NETDBG("bind: fd=%d port=%d\n", sockfd, s->local_addr.sin_port);
  
  return 0;
}

// connect(sockfd, addr, addrlen) syscall
int
sys_connect(void)
{
  int sockfd, addrlen, addr_raw;
  struct socket *s;
  struct sockaddr_in addr;
  struct ifnet *ifp;
  uint route_src;
  struct proc *p;
  pde_t *pgdir;

  if(argint(0, &sockfd) < 0 || argint(2, &addrlen) < 0)
    return -1;
  if(addrlen < (int)sizeof(struct sockaddr_in))
    return -1;
  if(argint(1, &addr_raw) < 0)
    return -1;

  p = myproc();
  pgdir = p ? proc_pgdir(p) : 0;
  if(pgdir == 0)
    return -1;
  if(copyin(pgdir, &addr, (uint)addr_raw, sizeof(addr)) < 0)
    return -1;

  s = getfd_socket(sockfd);
  if(!s)
    return -1;
  if(addr.sin_family != AF_INET)
    return -1;

  route_src = 0;
  ifp = route_lookup(addr.sin_addr.s_addr, &route_src, 0);
  if(ifp == 0)
    return -1;

  acquire(&socket_lock);
  if(s->local_addr.sin_addr.s_addr == 0)
    s->local_addr.sin_addr.s_addr = route_src;
  release(&socket_lock);

  if(s->type == SOCK_STREAM)
    return tcp_connect(ifp, s, &addr);

  acquire(&socket_lock);
  memmove(&s->remote_addr, &addr, sizeof(struct sockaddr_in));
  s->state = SOCK_CONNECT;
  release(&socket_lock);

  return 0;
}

// listen(sockfd, backlog) syscall
int
sys_listen(void)
{
  int sockfd, backlog;
  struct socket *s;

  if(argint(0, &sockfd) < 0 || argint(1, &backlog) < 0)
    return -1;

  s = getfd_socket(sockfd);
  if(!s)
    return -1;

  acquire(&socket_lock);
  if(s->type != SOCK_STREAM || s->state != SOCK_BOUND) {
    release(&socket_lock);
    return -1;
  }
  if(backlog <= 0)
    backlog = 1;
  if(backlog > SOCKET_LISTENQ_MAX)
    backlog = SOCKET_LISTENQ_MAX;
  s->state = SOCK_LISTEN;
  s->tcp.state = TCPS_LISTEN;
  s->backlog = (uint)backlog;
  s->qhead = 0;
  s->qtail = 0;
  s->qlen = 0;
  release(&socket_lock);

  return 0;
}

// accept(sockfd) syscall
int
sys_accept(void)
{
  int sockfd;
  int addr_raw;
  int addrlenp_raw;
  int fd;
  struct socket *listener;
  struct socket *accepted;
  struct file *f;
  struct proc *p;
  pde_t *pgdir;
  struct sockaddr_in peer;
  int outlen;

  if(argint(0, &sockfd) < 0)
    return -1;

  addr_raw = 0;
  addrlenp_raw = 0;
  if(argint(1, &addr_raw) < 0)
    addr_raw = 0;
  if(argint(2, &addrlenp_raw) < 0)
    addrlenp_raw = 0;

  listener = getfd_socket(sockfd);
  if(listener == 0)
    return -1;

  acquire(&socket_lock);
  if(listener->type != SOCK_STREAM || listener->state != SOCK_LISTEN) {
    release(&socket_lock);
    return -1;
  }

  while(listener->qlen == 0)
    sleep(listener, &socket_lock);

  accepted = socket_listenq_dequeue_locked(listener);
  release(&socket_lock);

  if(accepted == 0)
    return -1;

  f = filealloc();
  if(f == 0) {
    socket_close(accepted);
    return -1;
  }
  f->type = FD_SOCKET;
  f->socket = accepted;
  f->readable = 1;
  f->writable = 1;

  fd = socket_fdalloc(f);
  if(fd < 0) {
    fileclose(f);
    return -1;
  }

  if(addr_raw != 0 && addrlenp_raw != 0) {
    p = myproc();
    pgdir = p ? proc_pgdir(p) : 0;
    if(pgdir != 0) {
      if(copyin(pgdir, &outlen, (uint)addrlenp_raw, sizeof(outlen)) == 0 &&
         outlen >= (int)sizeof(struct sockaddr_in)) {
        acquire(&socket_lock);
        memmove(&peer, &accepted->remote_addr, sizeof(peer));
        release(&socket_lock);
        copyout(pgdir, (uint)addr_raw, &peer, sizeof(peer));
        outlen = (int)sizeof(struct sockaddr_in);
        copyout(pgdir, (uint)addrlenp_raw, &outlen, sizeof(outlen));
      }
    }
  }

  return fd;
}

// send(sockfd, buf, len) syscall
int
sys_send(void)
{
  int sockfd, len, buf_raw;
  int flags;
  uint buf_u;
  char *kbuf;
  struct socket *s;
  struct sockaddr_in src;
  struct sockaddr_in dst;
  int type;
  uint proto;
  uint tcp_state;
  struct ifnet *ifp;
  uint route_src;
  struct proc *p;
  pde_t *pgdir;

  if(argint(0, &sockfd) < 0 || argint(2, &len) < 0)
    return -1;
  if(len < 0)
    return -1;
  if(argint(1, &buf_raw) < 0)
    return -1;
  if(argint(3, &flags) < 0)
    flags = 0;
  if(flags & ~(MSG_NOSIGNAL | MSG_DONTWAIT))
    return -1;

  p = myproc();
  pgdir = p ? proc_pgdir(p) : 0;
  if(pgdir == 0)
    return -1;
  buf_u = (uint)buf_raw;

  if(len == 0)
    return 0;

  kbuf = (char*)kmalloc((uint)len);
  if(kbuf == 0)
    return -1;
  if(copyin(pgdir, kbuf, buf_u, (uint)len) < 0){
    kmalloc_free(kbuf);
    return -1;
  }

  s = getfd_socket(sockfd);
  if(!s)
  {
    kmalloc_free(kbuf);
    return -1;
  }

  if(s->family == AF_UNIX && s->type == SOCK_STREAM) {
    int sent;

    acquire(&socket_lock);
    sent = socket_local_stream_send_locked(s, kbuf, len, 0, 0);
    release(&socket_lock);
    kmalloc_free(kbuf);
    return sent;
  }

  acquire(&socket_lock);
  if(s->shut_wr) {
    release(&socket_lock);
    return -1;
  }
  memmove(&src, &s->local_addr, sizeof(src));
  memmove(&dst, &s->remote_addr, sizeof(dst));
  type = s->type;
  proto = s->protocol;
  tcp_state = s->tcp.state;
  release(&socket_lock);

  route_src = 0;
  ifp = route_lookup(dst.sin_addr.s_addr, &route_src, 0);
  if(ifp == 0)
    goto send_fail;
  if(src.sin_addr.s_addr == 0)
    src.sin_addr.s_addr = route_src;

  if(type == SOCK_DGRAM) {
    if((uint)len > MBUF_SIZE - sizeof(struct udp_hdr))
      goto send_fail;
    if(src.sin_port == 0 || dst.sin_port == 0)
      goto send_fail;
    if(udp_output(ifp, &src, &dst, kbuf, (uint)len) < 0)
      goto send_fail;
  } else if(type == SOCK_STREAM) {
    // TCP: send in segments; tcp_output() clamps each call to MSS.
    const char *sp = kbuf;
    int remaining = len;
    int total_sent = 0;
    int r;

    if(src.sin_port == 0 || dst.sin_port == 0)
      goto send_fail;
    if(tcp_state != TCPS_ESTABLISHED)
      goto send_fail;

    while(remaining > 0) {
      r = tcp_output(ifp, &src, &dst, (char*)sp, (uint)remaining);
      if(r <= 0)
        break;
      total_sent += r;
      sp += r;
      remaining -= r;
    }
    if(total_sent == 0)
      goto send_fail;
    kmalloc_free(kbuf);
    return total_sent;
  } else if(type == SOCK_RAW) {
    uchar snd_ttl;
    if((uint)len > MBUF_SIZE - sizeof(struct ip_hdr))
      goto send_fail;
    if(dst.sin_addr.s_addr == 0)
      goto send_fail;
    if(src.sin_addr.s_addr == 0)
      src.sin_addr.s_addr = route_src;
    acquire(&socket_lock);
    snd_ttl = s->ttl ? s->ttl : 64;
    release(&socket_lock);
    if(ip_output_ttl(ifp, (uchar)proto, src.sin_addr.s_addr, dst.sin_addr.s_addr, kbuf, (uint)len, snd_ttl) < 0)
      goto send_fail;
  } else {
    goto send_fail;
  }

  kmalloc_free(kbuf);
  return len;

send_fail:
  kmalloc_free(kbuf);
  return -1;
}

// sendto(sockfd, buf, len, flags, dst, dstlen) syscall
// Sends a datagram to dst.  If dst is NULL falls back to connected remote_addr.
// flags must be 0; SOCK_STREAM is not supported.
// An unbound SOCK_DGRAM socket is auto-assigned an ephemeral source port.
int
sys_sendto(void)
{
  int sockfd, len, flags, dstlen, dst_raw, buf_raw;
  uint buf_u;
  char *kbuf;
  struct socket *s;
  struct sockaddr_in udst;
  int have_dst;
  struct sockaddr_in src, rdst;
  int type;
  uint proto;
  struct ifnet *ifp;
  uint route_src;
  struct proc *p;
  pde_t *pgdir;

  if(argint(0, &sockfd) < 0 || argint(2, &len) < 0)
    return -1;
  if(argint(3, &flags) < 0 || flags != 0)
    return -1;
  if(len < 0 || (uint)len > MBUF_SIZE - sizeof(struct udp_hdr))
    return -1;
  if(argint(1, &buf_raw) < 0)
    return -1;
  p = myproc();
  pgdir = p ? proc_pgdir(p) : 0;
  if(pgdir == 0)
    return -1;
  buf_u = (uint)buf_raw;

  if(len > 0){
    kbuf = (char*)kmalloc((uint)len);
    if(kbuf == 0)
      return -1;
    if(copyin(pgdir, kbuf, buf_u, (uint)len) < 0){
      kmalloc_free(kbuf);
      return -1;
    }
  } else {
    kbuf = 0;
  }

  // arg4 is the destination pointer – may be 0 (NULL)
  if(argint(4, &dst_raw) < 0)
    goto sendto_fail;
  have_dst = 0;
  if(dst_raw != 0) {
    if(argint(5, &dstlen) < 0 || dstlen < (int)sizeof(struct sockaddr_in))
      goto sendto_fail;
    if(copyin(pgdir, &udst, (uint)dst_raw, sizeof(udst)) < 0)
      goto sendto_fail;
    if(udst.sin_family != AF_INET)
      goto sendto_fail;
    have_dst = 1;
  }

  s = getfd_socket(sockfd);
  if(!s)
    goto sendto_fail;

  acquire(&socket_lock);
  type = s->type;
  proto = s->protocol;
  memmove(&src, &s->local_addr, sizeof(src));
  if(have_dst)
    memmove(&rdst, &udst, sizeof(rdst));
  else
    memmove(&rdst, &s->remote_addr, sizeof(rdst));
  release(&socket_lock);

  // Unsupported on SOCK_STREAM – use send()
  if(type != SOCK_DGRAM && type != SOCK_RAW)
    goto sendto_fail;

  if(rdst.sin_addr.s_addr == 0)
    goto sendto_fail;

  route_src = 0;
  ifp = route_lookup(rdst.sin_addr.s_addr, &route_src, 0);
  if(!ifp)
    goto sendto_fail;
  if(src.sin_addr.s_addr == 0)
    src.sin_addr.s_addr = route_src;

  // Auto-bind an ephemeral source port when the socket has not been bound yet
  if(type == SOCK_DGRAM && src.sin_port == 0) {
    acquire(&socket_lock);
    if(s->local_addr.sin_port == 0) {
      ushort p = alloc_ephemeral_port_locked();
      if(p == 0) {
        release(&socket_lock);
        goto sendto_fail;
      }
      s->local_addr.sin_family = AF_INET;
      s->local_addr.sin_port = p;
      s->local_addr.sin_addr.s_addr = src.sin_addr.s_addr;
      if(s->state == SOCK_CLOSED)
        s->state = SOCK_BOUND;
    }
    src.sin_port = s->local_addr.sin_port;
    release(&socket_lock);
  }

  if(type == SOCK_DGRAM) {
    if(rdst.sin_port == 0)
      goto sendto_fail;
    if(udp_output(ifp, &src, &rdst, kbuf, (uint)len) < 0)
      goto sendto_fail;
  } else {
    // SOCK_RAW - use per-socket TTL
    uchar snd_ttl;
    acquire(&socket_lock);
    snd_ttl = s->ttl ? s->ttl : 64;
    release(&socket_lock);
    if(ip_output_ttl(ifp, (uchar)proto, src.sin_addr.s_addr, rdst.sin_addr.s_addr, kbuf, (uint)len, snd_ttl) < 0)
      goto sendto_fail;
  }

  NETDBG("sendto: fd=%d len=%d dport=%d\n", sockfd, len, rdst.sin_port);
  if(kbuf)
    kmalloc_free(kbuf);
  return len;

sendto_fail:
  if(kbuf)
    kmalloc_free(kbuf);
  return -1;
}

// recv(sockfd, buf, len) syscall
int
sys_recv(void)
{
  int sockfd, len, buf_raw;
  int flags;
  uint buf_u;
  char *kbuf;
  int n;
  struct socket *s;
  struct proc *p;
  pde_t *pgdir;

  if(argint(0, &sockfd) < 0 || argint(2, &len) < 0)
    return -1;
  if(len < 0)
    return -1;
  if(argint(1, &buf_raw) < 0)
    return -1;
  if(argint(3, &flags) < 0)
    flags = 0;
  if(flags & ~(MSG_PEEK | MSG_DONTWAIT))
    return -1;

  p = myproc();
  pgdir = p ? proc_pgdir(p) : 0;
  if(pgdir == 0)
    return -1;
  buf_u = (uint)buf_raw;

  if(len == 0)
    return 0;

  kbuf = (char*)kmalloc((uint)len);
  if(kbuf == 0)
    return -1;

  s = getfd_socket(sockfd);
  if(!s){
    kmalloc_free(kbuf);
    return -1;
  }

  acquire(&socket_lock);
  if(flags & MSG_DONTWAIT) {
    if(s->recv_len == 0) {
      if(socket_stream_eof_locked(s)) {
        release(&socket_lock);
        kmalloc_free(kbuf);
        return 0;
      }
      release(&socket_lock);
      kmalloc_free(kbuf);
      return -1;
    }
  } else {
    while(s->recv_len == 0) {
      if(socket_stream_eof_locked(s)) {
        release(&socket_lock);
        kmalloc_free(kbuf);
        return 0;
      }
      sleep(s, &socket_lock);
    }
  }

  if(flags & MSG_PEEK) {
    n = len;
    if((uint)n > s->recv_len)
      n = (int)s->recv_len;
    if(n > 0)
      memmove(kbuf, s->recv_buf, (uint)n);
  } else {
    n = socket_recv_copy_locked(s, kbuf, len, 0);
  }

  release(&socket_lock);

  if(n > 0 && copyout(pgdir, buf_u, kbuf, (uint)n) < 0){
    kmalloc_free(kbuf);
    return -1;
  }

  kmalloc_free(kbuf);

  if((flags & MSG_PEEK) == 0)
    socket_stream_window_update(s, n);
  return n;
}

// recvfrom(sockfd, buf, len, flags, src, srclen) syscall
// Receives data and optionally fills src with the sender's address.
// src and srclen may be NULL.  flags must be 0.
int
sys_recvfrom(void)
{
  int sockfd, len, flags, src_raw, buf_raw;
  int slen_raw;
  int srclen_addr;
  uint buf_u;
  char *kbuf;
  int n;
  struct socket *s;
  struct sockaddr_in peer;
  struct proc *p;
  pde_t *pgdir;

  if(argint(0, &sockfd) < 0 || argint(2, &len) < 0)
    return -1;
  if(argint(3, &flags) < 0 || flags != 0)
    return -1;
  if(len < 0)
    return -1;
  if(argint(1, &buf_raw) < 0)
    return -1;

  p = myproc();
  pgdir = p ? proc_pgdir(p) : 0;
  if(pgdir == 0)
    return -1;
  buf_u = (uint)buf_raw;

  if(len > 0){
    kbuf = (char*)kmalloc((uint)len);
    if(kbuf == 0)
      return -1;
  } else {
    kbuf = 0;
  }

  // arg4 is the source address pointer – may be 0 (NULL)
  if(argint(4, &src_raw) < 0)
    goto recvfrom_fail;
  srclen_addr = 0;
  if(src_raw != 0) {
    // arg5 is the addrlen pointer – may also be NULL
    if(argint(5, &slen_raw) >= 0 && slen_raw != 0)
      srclen_addr = slen_raw;
  }

  s = getfd_socket(sockfd);
  if(!s)
    goto recvfrom_fail;

  acquire(&socket_lock);
  while(s->recv_len == 0) {
    if(socket_stream_eof_locked(s)) {
      release(&socket_lock);
      return 0;
    }
    sleep(s, &socket_lock);
  }

  n = socket_recv_copy_locked(s, kbuf, len, &peer);
  release(&socket_lock);

  if(n > 0 && copyout(pgdir, buf_u, kbuf, (uint)n) < 0)
    goto recvfrom_fail;

  if(src_raw != 0){
    if(copyout(pgdir, (uint)src_raw, &peer, sizeof(peer)) < 0)
      goto recvfrom_fail;
    if(srclen_addr != 0){
      int sl = (int)sizeof(struct sockaddr_in);
      if(copyout(pgdir, (uint)srclen_addr, &sl, sizeof(sl)) < 0)
        goto recvfrom_fail;
    }
  }

  socket_stream_window_update(s, n);

  if(kbuf)
    kmalloc_free(kbuf);

  NETDBG("recvfrom: fd=%d n=%d sport=%d\n", sockfd, n, peer.sin_port);
  return n;

recvfrom_fail:
  if(kbuf)
    kmalloc_free(kbuf);
  return -1;
}

// recvtimeout(sockfd, buf, len, timeout_ticks) syscall
// Returns RECV_TIMEOUT_EXPIRED on timeout with no data.
int
sys_recvtimeout(void)
{
  int sockfd, len, timeout_ticks, buf_raw;
  uint buf_u;
  char *kbuf;
  int n;
  uint start;
  uint now;
  struct socket *s;
  struct proc *p;
  pde_t *pgdir;

  if(argint(0, &sockfd) < 0 || argint(2, &len) < 0 || argint(3, &timeout_ticks) < 0)
    return -1;
  if(len < 0 || timeout_ticks < 0)
    return -1;
  if(argint(1, &buf_raw) < 0)
    return -1;

  p = myproc();
  pgdir = p ? proc_pgdir(p) : 0;
  if(pgdir == 0)
    return -1;
  buf_u = (uint)buf_raw;

  if(len > 0){
    kbuf = (char*)kmalloc((uint)len);
    if(kbuf == 0)
      return -1;
  } else {
    kbuf = 0;
  }

  s = getfd_socket(sockfd);
  if(!s)
    goto recvtimeout_fail;

  if(timeout_ticks == 0)
    return 0;

  acquire(&tickslock);
  start = ticks;
  release(&tickslock);

  acquire(&socket_lock);
  while(s->recv_len == 0) {
    if(socket_stream_eof_locked(s)) {
      release(&socket_lock);
      if(kbuf)
        kmalloc_free(kbuf);
      return 0;
    }
    release(&socket_lock);

    acquire(&tickslock);
    now = ticks;
    if(now - start >= (uint)timeout_ticks) {
      release(&tickslock);
      if(kbuf)
        kmalloc_free(kbuf);
      return RECV_TIMEOUT_EXPIRED;
    }

    sleep(&ticks, &tickslock);
    release(&tickslock);

    acquire(&socket_lock);
  }

  n = socket_recv_copy_locked(s, kbuf, len, 0);

  release(&socket_lock);

  if(n > 0 && copyout(pgdir, buf_u, kbuf, (uint)n) < 0)
    goto recvtimeout_fail;

  if(kbuf)
    kmalloc_free(kbuf);

  socket_stream_window_update(s, n);
  return n;

recvtimeout_fail:
  if(kbuf)
    kmalloc_free(kbuf);
  return -1;
}

int
sys_netifinfo(void)
{
  int out_addr;
  uint out_u;
  struct netif_info *kout;
  struct proc *p;
  pde_t *pgdir;
  int max;
  int n;

  if(argint(1, &max) < 0)
    return -1;
  if(max <= 0)
    return -1;
  if(max > MAXNETIF)
    max = MAXNETIF;
  if(argint(0, &out_addr) < 0)
    return -1;
  out_u = (uint)out_addr;

  kout = (struct netif_info*)kmalloc(max * sizeof(*kout));
  if(kout == 0)
    return -1;

  n = if_dump(kout, max);
  if(n < 0){
    kmalloc_free(kout);
    return -1;
  }

  if(n > 0){
    p = myproc();
    pgdir = p ? proc_pgdir(p) : 0;
    if(pgdir == 0 || copyout(pgdir, out_u, kout, n * sizeof(*kout)) < 0){
      kmalloc_free(kout);
      return -1;
    }
  }

  kmalloc_free(kout);
  return n;
}

int
sys_routeinfo(void)
{
  int out_addr;
  uint out_u;
  struct route_info *kout;
  struct proc *p;
  pde_t *pgdir;
  int max;
  int n;

  if(argint(1, &max) < 0)
    return -1;
  if(max <= 0)
    return -1;
  if(max > NET_ROUTE_TABLE_MAX)
    max = NET_ROUTE_TABLE_MAX;
  if(argint(0, &out_addr) < 0)
    return -1;
  out_u = (uint)out_addr;

  kout = (struct route_info*)kmalloc(max * sizeof(*kout));
  if(kout == 0)
    return -1;

  n = route_dump(kout, max);
  if(n < 0){
    kmalloc_free(kout);
    return -1;
  }

  if(n > 0){
    p = myproc();
    pgdir = p ? proc_pgdir(p) : 0;
    if(pgdir == 0 || copyout(pgdir, out_u, kout, n * sizeof(*kout)) < 0){
      kmalloc_free(kout);
      return -1;
    }
  }

  kmalloc_free(kout);
  return n;
}

int
sys_arpinfo(void)
{
  int out_addr;
  uint out_u;
  struct arp_info *kout;
  struct proc *p;
  pde_t *pgdir;
  int max;
  int n;

  if(argint(1, &max) < 0)
    return -1;
  if(max <= 0)
    return -1;
  if(max > NET_ARP_CACHE_MAX)
    max = NET_ARP_CACHE_MAX;
  if(argint(0, &out_addr) < 0)
    return -1;
  out_u = (uint)out_addr;

  kout = (struct arp_info*)kmalloc(max * sizeof(*kout));
  if(kout == 0)
    return -1;

  n = arp_dump(kout, max);
  if(n < 0){
    kmalloc_free(kout);
    return -1;
  }

  if(n > 0){
    p = myproc();
    pgdir = p ? proc_pgdir(p) : 0;
    if(pgdir == 0 || copyout(pgdir, out_u, kout, n * sizeof(*kout)) < 0){
      kmalloc_free(kout);
      return -1;
    }
  }

  kmalloc_free(kout);
  return n;
}

int
sys_routeadd(void)
{
  int dst;
  int mask;
  int gateway;
  int src;
  int ifindex;
  struct ifnet *ifp;

  if(argint(0, &dst) < 0 || argint(1, &mask) < 0 || argint(2, &gateway) < 0 ||
     argint(3, &src) < 0 || argint(4, &ifindex) < 0)
    return -1;

  ifp = if_byindex((uint)ifindex);
  if(ifp == 0)
    return -1;

  return route_add((uint)dst, (uint)mask, (uint)gateway, (uint)src, ifp,
                   RTF_UP | (gateway ? RTF_GATEWAY : 0));
}

int
sys_routedel(void)
{
  int dst;
  int mask;
  int ifindex;
  struct ifnet *ifp;

  if(argint(0, &dst) < 0 || argint(1, &mask) < 0 || argint(2, &ifindex) < 0)
    return -1;

  ifp = if_byindex((uint)ifindex);
  if(ifp == 0)
    return -1;

  return route_delete((uint)dst, (uint)mask, ifp);
}

int
sys_netifsetaddr(void)
{
  int ifindex;
  int addr;
  int mask;

  if(argint(0, &ifindex) < 0 || argint(1, &addr) < 0 || argint(2, &mask) < 0)
    return -1;
  if(ifindex <= 0)
    return -1;

  return if_set_addr_byindex((uint)ifindex, (uint)addr, (uint)mask);
}

// close(fd) syscall - we'll handle sockets in the existing close
// This is called from sys_close when the fd points to a socket
//
// Teardown ref protocol:
//   When we initiate a TCP FIN (ESTABLISHED or CLOSE_WAIT), we bump the
//   socket's refcount before dropping the user ref.  tcp.close_pending is
//   set to signal that the extra ref must be released once TCP reaches
//   TCPS_CLOSED.  All TCPS_CLOSED transition paths in tcp.c check this flag.
void
socket_close(struct socket *s)
{
  struct ifnet *ifp;
  uint route_src;

  if(!s) return;
  
  // For TCP sockets, initiate graceful close if connected
  if(s->type == SOCK_STREAM && s->family == AF_INET) {
    acquire(&socket_lock);
    if(s->tcp.state == TCPS_ESTABLISHED || s->tcp.state == TCPS_CLOSE_WAIT) {
      // Bump ref so the socket survives FIN teardown (close_pending ref).
      s->ref++;
      s->tcp.close_pending = 1;
      // Find interface for this connection
      ifp = route_lookup(s->remote_addr.sin_addr.s_addr, &route_src, 0);
      release(&socket_lock);
      if(ifp) {
        tcp_close(s, ifp);
      } else {
        // No route; abort TCP cleanly.
        acquire(&socket_lock);
        if(s->tcp.close_pending) {
          s->tcp.close_pending = 0;
          s->ref--;  // undo the extra ref we just added
        }
        if(s->tcp.unacked_buf) {
          kfree(s->tcp.unacked_buf);
          s->tcp.unacked_buf = 0;
        }
        s->tcp.state = TCPS_CLOSED;
        s->state = SOCK_CLOSED;
        release(&socket_lock);
      }
    } else {
      // Not in a state that needs FIN, just mark closed
      if(s->tcp.unacked_buf) {
        kfree(s->tcp.unacked_buf);
        s->tcp.unacked_buf = 0;
      }
      s->tcp.state = TCPS_CLOSED;
      s->state = SOCK_CLOSED;
      release(&socket_lock);
    }
  } else {
    acquire(&socket_lock);
    if(s->family == AF_UNIX && s->peer) {
      s->peer->peer = 0;
      wakeup(s->peer);
      s->peer = 0;
    }
    s->state = SOCK_CLOSED;
    wakeup(s);
    release(&socket_lock);
  }
  
  socket_deref(s);
}

// Helper: get socket from file descriptor
// setsockopt(sockfd, level, optname, optval, optlen) syscall
int
sys_setsockopt(void)
{
  int sockfd, level, optname, optlen;
  int optval_raw;
  uint optval_u;
  struct socket *s;
  int v;
  struct proc *p;
  pde_t *pgdir;

  if(argint(0, &sockfd) < 0 || argint(1, &level) < 0 || argint(2, &optname) < 0)
    return -1;
  if(argint(4, &optlen) < 0 || optlen <= 0 || optlen > 64)
    return -1;
  if(argint(3, &optval_raw) < 0)
    return -1;
  optval_u = (uint)optval_raw;

  p = myproc();
  pgdir = p ? proc_pgdir(p) : 0;
  if(pgdir == 0)
    return -1;

  s = getfd_socket(sockfd);
  if(!s)
    return -1;

  if(level == IPPROTO_IP) {
    if(optname == IP_TTL) {
      if(optlen < (int)sizeof(int))
        return -1;
      if(copyin(pgdir, &v, optval_u, sizeof(int)) < 0)
        return -1;
      if(v < 1 || v > 255)
        return -1;
      acquire(&socket_lock);
      s->ttl = (uchar)v;
      release(&socket_lock);
      return 0;
    }
  }

  if(level == SOL_SOCKET) {
    if(optname == SO_REUSEADDR) {
      if(optlen < (int)sizeof(int))
        return -1;
      if(copyin(pgdir, &v, optval_u, sizeof(int)) < 0)
        return -1;
      acquire(&socket_lock);
      s->reuseaddr = v ? 1 : 0;
      release(&socket_lock);
      return 0;
    }
  }

  return -1;
}

// getsockopt(sockfd, level, optname, optval, optlen) syscall
int
sys_getsockopt(void)
{
  int sockfd, level, optname, optval_raw, optlenp_raw;
  uint optval_u;
  uint optlenp_u;
  int optlen;
  int outv;
  struct socket *s;
  struct proc *p;
  pde_t *pgdir;

  if(argint(0, &sockfd) < 0 || argint(1, &level) < 0 || argint(2, &optname) < 0)
    return -1;
  if(argint(3, &optval_raw) < 0 || argint(4, &optlenp_raw) < 0)
    return -1;

  optval_u = (uint)optval_raw;
  optlenp_u = (uint)optlenp_raw;

  p = myproc();
  pgdir = p ? proc_pgdir(p) : 0;
  if(pgdir == 0)
    return -1;
  if(copyin(pgdir, &optlen, optlenp_u, sizeof(int)) < 0)
    return -1;
  if(optlen < (int)sizeof(int))
    return -1;

  s = getfd_socket(sockfd);
  if(!s)
    return -1;

  if(level == IPPROTO_IP) {
    if(optname == IP_TTL) {
      acquire(&socket_lock);
      outv = (int)s->ttl;
      release(&socket_lock);
      if(copyout(pgdir, optval_u, &outv, sizeof(int)) < 0)
        return -1;
      optlen = sizeof(int);
      if(copyout(pgdir, optlenp_u, &optlen, sizeof(int)) < 0)
        return -1;
      return 0;
    }
  }

  if(level == SOL_SOCKET) {
    if(optname == SO_REUSEADDR) {
      acquire(&socket_lock);
      outv = (int)s->reuseaddr;
      release(&socket_lock);
      if(copyout(pgdir, optval_u, &outv, sizeof(int)) < 0)
        return -1;
      optlen = sizeof(int);
      if(copyout(pgdir, optlenp_u, &optlen, sizeof(int)) < 0)
        return -1;
      return 0;
    }
  }

  return -1;
}

// shutdown(sockfd, how) syscall
// how: SHUT_RD=0, SHUT_WR=1, SHUT_RDWR=2
int
sys_shutdown(void)
{
  int sockfd, how;
  struct socket *s;
  struct ifnet *ifp;
  uint route_src;

  if(argint(0, &sockfd) < 0 || argint(1, &how) < 0)
    return -1;
  if(how < 0 || how > 2)
    return -1;

  s = getfd_socket(sockfd);
  if(!s)
    return -1;

  acquire(&socket_lock);

  if(how == SHUT_RD || how == SHUT_RDWR)
    s->shut_rd = 1;

  if(how == SHUT_WR || how == SHUT_RDWR) {
    s->shut_wr = 1;
    // For TCP, send a FIN if the connection is still active.
    if(s->type == SOCK_STREAM &&
       (s->tcp.state == TCPS_ESTABLISHED || s->tcp.state == TCPS_CLOSE_WAIT)) {
      // Hold teardown ref (matches socket_close protocol).
      s->ref++;
      s->tcp.close_pending = 1;
      release(&socket_lock);
      route_src = 0;
      ifp = route_lookup(s->remote_addr.sin_addr.s_addr, &route_src, 0);
      if(ifp) {
        tcp_close(s, ifp);
      } else {
        acquire(&socket_lock);
        if(s->tcp.close_pending) {
          s->tcp.close_pending = 0;
          s->ref--;
        }
        s->tcp.state = TCPS_CLOSED;
        s->state = SOCK_CLOSED;
        release(&socket_lock);
      }
      // Do NOT socket_deref here — the FD is still open.  The extra ref
      // is released when TCP teardown completes via close_pending.
      // But we need to undo one extra ref since no socket_deref() is called
      // from here (unlike socket_close).  So release:
      socket_deref(s);
      return 0;
    }
  }

  wakeup(s);   // wake readers/writers so they see the shut flags
  release(&socket_lock);
  return 0;
}

// getsockname(sockfd, addr, addrlen) syscall
int
sys_getsockname(void)
{
  int sockfd;
  int addr_raw;
  int addrlenp_raw;
  struct socket *s;
  struct sockaddr_in kaddr;
  int klen;
  struct proc *p;
  pde_t *pgdir;

  if(argint(0, &sockfd) < 0)
    return -1;
  if(argint(1, &addr_raw) < 0)
    return -1;
  if(argint(2, &addrlenp_raw) < 0)
    return -1;

  p = myproc();
  pgdir = p ? proc_pgdir(p) : 0;
  if(pgdir == 0)
    return -1;
  if(copyin(pgdir, &klen, (uint)addrlenp_raw, sizeof(int)) < 0)
    return -1;
  if(klen < (int)sizeof(struct sockaddr_in))
    return -1;

  s = getfd_socket(sockfd);
  if(!s)
    return -1;

  acquire(&socket_lock);
  memmove(&kaddr, &s->local_addr, sizeof(kaddr));
  release(&socket_lock);

  if(copyout(pgdir, (uint)addr_raw, &kaddr, sizeof(kaddr)) < 0)
    return -1;
  klen = (int)sizeof(struct sockaddr_in);
  if(copyout(pgdir, (uint)addrlenp_raw, &klen, sizeof(klen)) < 0)
    return -1;
  return 0;
}

// getpeername(sockfd, addr, addrlen) syscall
int
sys_getpeername(void)
{
  int sockfd;
  int addr_raw;
  int addrlenp_raw;
  struct socket *s;
  struct sockaddr_in kaddr;
  int klen;
  struct proc *p;
  pde_t *pgdir;

  if(argint(0, &sockfd) < 0)
    return -1;
  if(argint(1, &addr_raw) < 0)
    return -1;
  if(argint(2, &addrlenp_raw) < 0)
    return -1;

  p = myproc();
  pgdir = p ? proc_pgdir(p) : 0;
  if(pgdir == 0)
    return -1;
  if(copyin(pgdir, &klen, (uint)addrlenp_raw, sizeof(int)) < 0)
    return -1;
  if(klen < (int)sizeof(struct sockaddr_in))
    return -1;

  s = getfd_socket(sockfd);
  if(!s)
    return -1;

  acquire(&socket_lock);
  if(s->state != SOCK_ESTAB && s->state != SOCK_CONNECT) {
    release(&socket_lock);
    return -1;
  }
  memmove(&kaddr, &s->remote_addr, sizeof(kaddr));
  release(&socket_lock);

  if(copyout(pgdir, (uint)addr_raw, &kaddr, sizeof(kaddr)) < 0)
    return -1;
  klen = (int)sizeof(struct sockaddr_in);
  if(copyout(pgdir, (uint)addrlenp_raw, &klen, sizeof(klen)) < 0)
    return -1;
  return 0;
}

struct socket*
getfd_socket(int fd)
{
  struct file *f;
  
  if(myproc() == 0 || myproc()->fdtable == 0 || fd < 0 || fd >= myproc()->fdtable->nfds)
    return 0;

  if(!myproc()->fdtable->entries[fd])
    return 0;
  
  f = myproc()->fdtable->entries[fd];
  
  if(f->type == FD_SOCKET)
    return f->socket;
  
  return 0;
}

void
socket_poll_events(struct socket *s, int *readable, int *writable, int *error)
{
  int rd;
  int wr;
  int err;

  if(s == 0)
    return;

  acquire(&socket_lock);

  rd = 0;
  wr = 0;
  err = 0;

  if(s->state == SOCK_CLOSED)
    err = 1;

  if(s->type == SOCK_STREAM && s->state == SOCK_LISTEN)
    rd = (s->qlen > 0);
  else
    rd = (s->recv_len > 0);

  if(s->type == SOCK_STREAM)
    wr = (s->family == AF_UNIX)
           ? (s->state == SOCK_ESTAB && s->peer != 0 && s->peer->state != SOCK_CLOSED)
           : (s->state == SOCK_ESTAB && s->tcp.state == TCPS_ESTABLISHED);
  else if(s->type == SOCK_DGRAM)
    wr = (s->state == SOCK_BOUND || s->state == SOCK_CONNECT || s->state == SOCK_ESTAB);
  else if(s->type == SOCK_RAW)
    wr = (s->state == SOCK_BOUND || s->state == SOCK_CONNECT);

  if(readable)
    *readable = rd;
  if(writable)
    *writable = wr;
  if(error)
    *error = err;

  release(&socket_lock);
}

/*
 * socket_get_table - snapshot all non-closed sockets into caller's buffer.
 *
 * Acquires socket_lock, copies up to max entries, releases the lock, and
 * returns the count of entries written (>= 0) or -1 on bad arguments.
 * All address/port fields use host byte order.
 */
int
socket_get_table(struct socket_info_k *out, int max)
{
  int i;
  int n;
  struct socket *s;

  if(out == 0 || max <= 0)
    return -1;

  n = 0;
  acquire(&socket_lock);
  for(i = 0; i < NSOCKET && n < max; i++){
    s = &sockets[i];
    if(s->state == SOCK_CLOSED || s->ref == 0)
      continue;
    out[n].family     = s->family;
    out[n].type       = s->type;
    out[n].state      = s->state;
    out[n].tcp_state  = s->tcp.state;
    out[n].local_ip   = s->local_addr.sin_addr.s_addr;
    out[n].local_port = s->local_addr.sin_port;
    out[n].remote_ip  = s->remote_addr.sin_addr.s_addr;
    out[n].remote_port= s->remote_addr.sin_port;
    out[n].recv_len   = s->recv_len;
    out[n].send_len   = s->send_len;
    out[n].pid        = -1;
    n++;
  }
  release(&socket_lock);
  return n;
}
