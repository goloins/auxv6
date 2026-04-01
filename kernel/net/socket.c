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
#include "../../include/net.h"

#define NSOCKET 64
struct socket sockets[NSOCKET];
struct spinlock socket_lock;
static ushort next_ephemeral = 40000;
static uint tcp_iss = 1000;
static ushort alloc_ephemeral_port_locked(void);

// Allocate a file descriptor in the current process.
// Mirrors sysfile.c's internal fdalloc helper.
static int
socket_fdalloc(struct file *f)
{
  int fd;
  struct proc *curproc = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd] == 0){
      curproc->ofile[fd] = f;
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
  s->tcp.irs = 0;
  s->tcp.rcv_nxt = 0;
  s->backlog = 0;
  s->qhead = 0;
  s->qtail = 0;
  s->qlen = 0;
  for(i = 0; i < SOCKET_LISTENQ_MAX; i++)
    s->listenq[i] = 0;
}

static void
socket_free_locked(struct socket *s)
{
  uint i;
  struct socket *child;

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
    if(sockets[i].remote_addr.sin_addr != src->sin_addr)
      continue;
    if(sockets[i].local_addr.sin_addr != INADDR_ANY &&
       sockets[i].local_addr.sin_addr != dst->sin_addr)
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
      if(sockets[i].local_addr.sin_addr != INADDR_ANY &&
         sockets[i].local_addr.sin_addr != dst->sin_addr)
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
    if(sockets[i].remote_addr.sin_addr != 0 &&
       sockets[i].remote_addr.sin_addr != src->sin_addr)
      continue;
    if(sockets[i].local_addr.sin_addr != INADDR_ANY &&
       sockets[i].local_addr.sin_addr != dst->sin_addr)
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
       sockets[i].local_addr.sin_addr != INADDR_ANY &&
       sockets[i].local_addr.sin_addr != dst->sin_addr)
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
    if(sockets[i].local_addr.sin_addr != INADDR_ANY &&
       sockets[i].local_addr.sin_addr != remote->sin_addr)
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
  accepted->remote_addr.sin_addr = s->local_addr.sin_addr;
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

// bind(sockfd, addr, addrlen) syscall
int
sys_bind(void)
{
  int sockfd, addrlen;
  struct socket *s;
  struct sockaddr_in *addr;
  
  if(argint(0, &sockfd) < 0 || argint(2, &addrlen) < 0)
    return -1;
  
  if(addrlen != sizeof(struct sockaddr_in)) {
    cprintf("bind: invalid address length\n");
    return -1;
  }
  
  if(argptr(1, (char**)&addr, addrlen) < 0)
    return -1;
  
  // Get socket from fd
  s = getfd_socket(sockfd);
  if(!s) {
    cprintf("bind: invalid socket fd %d\n", sockfd);
    return -1;
  }
  
  acquire(&socket_lock);
  memmove(&s->local_addr, addr, sizeof(struct sockaddr_in));
  s->state = SOCK_BOUND;
  release(&socket_lock);
  
  NETDBG("bind: fd=%d port=%d\n", sockfd, s->local_addr.sin_port);
  
  return 0;
}

// connect(sockfd, addr, addrlen) syscall
int
sys_connect(void)
{
  int sockfd, addrlen;
  struct socket *s;
  struct sockaddr_in *addr;
  struct ifnet *ifp;
  uint route_src;

  if(argint(0, &sockfd) < 0 || argint(2, &addrlen) < 0)
    return -1;
  if(addrlen != sizeof(struct sockaddr_in))
    return -1;
  if(argptr(1, (char**)&addr, addrlen) < 0)
    return -1;

  s = getfd_socket(sockfd);
  if(!s)
    return -1;
  if(addr->sin_family != AF_INET)
    return -1;

  route_src = 0;
  ifp = route_lookup(addr->sin_addr, &route_src, 0);
  if(ifp == 0)
    return -1;

  acquire(&socket_lock);
  if(s->local_addr.sin_addr == 0)
    s->local_addr.sin_addr = route_src;
  release(&socket_lock);

  if(s->type == SOCK_STREAM)
    return tcp_connect(ifp, s, addr);

  acquire(&socket_lock);
  memmove(&s->remote_addr, addr, sizeof(struct sockaddr_in));
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
  int fd;
  struct socket *listener;
  struct socket *accepted;
  struct file *f;

  if(argint(0, &sockfd) < 0)
    return -1;

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

  return fd;
}

// send(sockfd, buf, len) syscall
int
sys_send(void)
{
  int sockfd, len;
  char *buf;
  struct socket *s;
  struct sockaddr_in src;
  struct sockaddr_in dst;
  int type;
  uint proto;
  uint tcp_state;
  struct ifnet *ifp;
  uint route_src;

  if(argint(0, &sockfd) < 0 || argint(2, &len) < 0)
    return -1;
  if(len < 0 || argptr(1, &buf, len) < 0)
    return -1;

  s = getfd_socket(sockfd);
  if(!s)
    return -1;

  if((uint)len > MBUF_SIZE - sizeof(struct udp_hdr))
    return -1;

  acquire(&socket_lock);
  memmove(&src, &s->local_addr, sizeof(src));
  memmove(&dst, &s->remote_addr, sizeof(dst));
  type = s->type;
  proto = s->protocol;
  tcp_state = s->tcp.state;
  release(&socket_lock);

  route_src = 0;
  ifp = route_lookup(dst.sin_addr, &route_src, 0);
  if(ifp == 0)
    return -1;
  if(src.sin_addr == 0)
    src.sin_addr = route_src;

  if(type == SOCK_DGRAM){
    if(src.sin_port == 0 || dst.sin_port == 0)
      return -1;
    if(udp_output(ifp, &src, &dst, buf, (uint)len) < 0)
      return -1;
  } else if(type == SOCK_STREAM){
    if(src.sin_port == 0 || dst.sin_port == 0)
      return -1;
    if(tcp_state != TCPS_ESTABLISHED)
      return -1;
    if(tcp_output(ifp, &src, &dst, buf, (uint)len) < 0)
      return -1;
  } else if(type == SOCK_RAW){
    if(dst.sin_addr == 0)
      return -1;
    if(src.sin_addr == 0)
      src.sin_addr = route_src;
    if(ip_output(ifp, (uchar)proto, src.sin_addr, dst.sin_addr, buf, (uint)len) < 0)
      return -1;
  } else {
    return -1;
  }

  return len;
}

// recv(sockfd, buf, len) syscall
int
sys_recv(void)
{
  int sockfd, len;
  char *buf;
  int n;
  struct socket *s;

  if(argint(0, &sockfd) < 0 || argint(2, &len) < 0)
    return -1;
  if(len < 0 || argptr(1, &buf, len) < 0)
    return -1;

  s = getfd_socket(sockfd);
  if(!s)
    return -1;

  acquire(&socket_lock);
  while(s->recv_len == 0)
    sleep(s, &socket_lock);

  n = len;
  if((uint)n > s->recv_len)
    n = s->recv_len;

  if(n > 0)
    memmove(buf, s->recv_buf, (uint)n);
  if((uint)n < s->recv_len)
    memmove(s->recv_buf, s->recv_buf + n, s->recv_len - (uint)n);
  s->recv_len -= (uint)n;

  release(&socket_lock);
  return n;
}

// recvtimeout(sockfd, buf, len, timeout_ticks) syscall
// Returns 0 on timeout with no data.
int
sys_recvtimeout(void)
{
  int sockfd, len, timeout_ticks;
  int n;
  uint start;
  uint now;
  char *buf;
  struct socket *s;

  if(argint(0, &sockfd) < 0 || argint(2, &len) < 0 || argint(3, &timeout_ticks) < 0)
    return -1;
  if(len < 0 || timeout_ticks < 0 || argptr(1, &buf, len) < 0)
    return -1;

  s = getfd_socket(sockfd);
  if(!s)
    return -1;

  if(timeout_ticks == 0)
    return 0;

  acquire(&tickslock);
  start = ticks;
  release(&tickslock);

  acquire(&socket_lock);
  while(s->recv_len == 0) {
    release(&socket_lock);

    acquire(&tickslock);
    now = ticks;
    if(now - start >= (uint)timeout_ticks) {
      release(&tickslock);
      return 0;
    }

    sleep(&ticks, &tickslock);
    release(&tickslock);

    acquire(&socket_lock);
  }

  n = len;
  if((uint)n > s->recv_len)
    n = s->recv_len;

  if(n > 0)
    memmove(buf, s->recv_buf, (uint)n);
  if((uint)n < s->recv_len)
    memmove(s->recv_buf, s->recv_buf + n, s->recv_len - (uint)n);
  s->recv_len -= (uint)n;

  release(&socket_lock);
  return n;
}

int
sys_netifinfo(void)
{
  struct netif_info *out;
  int max;

  if(argint(1, &max) < 0)
    return -1;
  if(max <= 0)
    return -1;
  if(argptr(0, (char**)&out, max * sizeof(*out)) < 0)
    return -1;

  return if_dump(out, max);
}

int
sys_routeinfo(void)
{
  struct route_info *out;
  int max;

  if(argint(1, &max) < 0)
    return -1;
  if(max <= 0)
    return -1;
  if(argptr(0, (char**)&out, max * sizeof(*out)) < 0)
    return -1;

  return route_dump(out, max);
}

int
sys_arpinfo(void)
{
  struct arp_info *out;
  int max;

  if(argint(1, &max) < 0)
    return -1;
  if(max <= 0)
    return -1;
  if(argptr(0, (char**)&out, max * sizeof(*out)) < 0)
    return -1;

  return arp_dump(out, max);
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
      // Find interface for this connection
      ifp = route_lookup(s->remote_addr.sin_addr, &route_src, 0);
      release(&socket_lock);
      if(ifp) {
        tcp_close(s, ifp);
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
    s->state = SOCK_CLOSED;
    release(&socket_lock);
  }
  
  socket_deref(s);
}

// Helper: get socket from file descriptor
struct socket*
getfd_socket(int fd)
{
  struct file *f;
  
  if(fd < 0 || fd >= NOFILE || !myproc()->ofile[fd])
    return 0;
  
  f = myproc()->ofile[fd];
  
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
    wr = (s->state == SOCK_ESTAB && s->tcp.state == TCPS_ESTABLISHED);
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
