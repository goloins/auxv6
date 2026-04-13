#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "stat.h"
#include "fs.h"
#include "file.h"
#include "proc.h"
#include "net.h"
#include "sys/ioctl.h"

#define TUNTAP_MAX_UNITS 16
#define TUNTAP_MAX_SESSIONS 64
#define TUNTAP_RXQ_LEN 32

struct tuntap_packet {
  int len;
  char data[MBUF_SIZE];
};

struct tuntap_unit {
  int allocated;
  int if_registered;
  int refs;
  int mode;
  int owner;
  int group;
  int persist;
  char name[IFNAMSIZ];

  struct ifnet ifp;
  struct ifnet_ops if_ops;

  uint rx_r;
  uint rx_w;
  struct tuntap_packet rxq[TUNTAP_RXQ_LEN];
};

struct tuntap_session {
  struct file *f;
  int unit;
  int nonblock;
};

static struct {
  struct spinlock lock;
  struct tuntap_unit units[TUNTAP_MAX_UNITS];
  struct tuntap_session sessions[TUNTAP_MAX_SESSIONS];
} tuntap_state;

static int
mode_valid(short flags)
{
  int has_tun;
  int has_tap;

  has_tun = (flags & IFF_TUN) != 0;
  has_tap = (flags & IFF_TAP) != 0;
  return has_tun != has_tap;
}

static int
name_valid(const char *name)
{
  int i;
  int c;

  if(name == 0 || name[0] == 0)
    return 0;

  for(i = 0; i < IFNAMSIZ; i++){
    c = name[i];
    if(c == 0)
      return i > 0;
    if(!((c >= 'a' && c <= 'z') ||
         (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') ||
         c == '_' || c == '-'))
      return 0;
  }
  return 0;
}

static int
name_mode(const char *name)
{
  if(name == 0)
    return 0;
  if(strncmp(name, "tun", 3) == 0)
    return IFF_TUN;
  if(strncmp(name, "tap", 3) == 0)
    return IFF_TAP;
  return 0;
}

static void
format_name(int mode, int idx, char *dst)
{
  const char *prefix;
  int p;

  if(dst == 0)
    return;
  prefix = (mode == IFF_TAP) ? "tap" : "tun";
  p = 0;
  while(prefix[p] && p < IFNAMSIZ - 1){
    dst[p] = prefix[p];
    p++;
  }
  if(idx >= 10 && p < IFNAMSIZ - 1)
    dst[p++] = '0' + (idx / 10);
  if(p < IFNAMSIZ - 1)
    dst[p++] = '0' + (idx % 10);
  dst[p] = 0;
}

static int
unit_can_use(struct tuntap_unit *u, struct proc *p)
{
  if(u == 0 || p == 0)
    return 0;
  if(p->uid == 0)
    return 1;
  if(u->owner < 0)
    return 1;
  return u->owner == p->uid;
}

static int
unit_can_admin(struct tuntap_unit *u, struct proc *p)
{
  return unit_can_use(u, p);
}

static int
find_session_locked(struct file *f)
{
  int i;

  for(i = 0; i < TUNTAP_MAX_SESSIONS; i++){
    if(tuntap_state.sessions[i].f == f)
      return i;
  }
  return -1;
}

static int
alloc_session_locked(struct file *f)
{
  int i;

  for(i = 0; i < TUNTAP_MAX_SESSIONS; i++){
    if(tuntap_state.sessions[i].f == 0){
      tuntap_state.sessions[i].f = f;
      tuntap_state.sessions[i].unit = -1;
      tuntap_state.sessions[i].nonblock = 0;
      return i;
    }
  }
  return -1;
}

static int
find_named_unit_locked(const char *name)
{
  int i;

  for(i = 0; i < TUNTAP_MAX_UNITS; i++){
    if(tuntap_state.units[i].allocated &&
       strncmp(tuntap_state.units[i].name, name, IFNAMSIZ) == 0)
      return i;
  }
  return -1;
}

static int
alloc_unit_locked(int mode)
{
  int i;

  for(i = 0; i < TUNTAP_MAX_UNITS; i++){
    if(tuntap_state.units[i].allocated)
      continue;
    memset(&tuntap_state.units[i], 0, sizeof(tuntap_state.units[i]));
    tuntap_state.units[i].allocated = 1;
    tuntap_state.units[i].mode = mode;
    tuntap_state.units[i].owner = -1;
    tuntap_state.units[i].group = -1;
    format_name(mode, i, tuntap_state.units[i].name);
    return i;
  }
  return -1;
}

static void
maybe_release_unit_locked(int unit)
{
  if(unit < 0 || unit >= TUNTAP_MAX_UNITS)
    return;
  if(!tuntap_state.units[unit].allocated)
    return;
  if(tuntap_state.units[unit].refs > 0)
    return;
  if(tuntap_state.units[unit].persist)
    return;
  if(tuntap_state.units[unit].if_registered)
    return;
  memset(&tuntap_state.units[unit], 0, sizeof(tuntap_state.units[unit]));
}

static void
unregister_if_locked(int unit)
{
  struct tuntap_unit *u;

  if(unit < 0 || unit >= TUNTAP_MAX_UNITS)
    return;
  u = &tuntap_state.units[unit];
  if(!u->allocated || !u->if_registered)
    return;

  if_unregister(&u->ifp);
  u->if_registered = 0;
  memset(&u->ifp, 0, sizeof(u->ifp));
  memset(&u->if_ops, 0, sizeof(u->if_ops));
}

static int
rxq_empty(struct tuntap_unit *u)
{
  return u->rx_r == u->rx_w;
}

static int
rxq_full(struct tuntap_unit *u)
{
  return (u->rx_w - u->rx_r) >= (uint)TUNTAP_RXQ_LEN;
}

static int
enqueue_from_mbuf_locked(struct tuntap_unit *u, struct mbuf *m)
{
  uint slot;
  int n;

  if(u == 0 || m == 0)
    return -1;
  if(rxq_full(u))
    return -1;

  slot = u->rx_w % TUNTAP_RXQ_LEN;
  n = (int)m->len;
  if(n > MBUF_SIZE)
    n = MBUF_SIZE;
  u->rxq[slot].len = n;
  if(n > 0)
    memmove(u->rxq[slot].data, m->data, (uint)n);
  u->rx_w++;
  wakeup(&u->rx_r);
  return n;
}

static int
if_output_tuntap(struct ifnet *ifp, struct mbuf *m)
{
  struct tuntap_unit *u;
  int n;

  if(ifp == 0 || m == 0)
    return -1;
  u = (struct tuntap_unit*)ifp->if_softc;
  if(u == 0){
    mbuf_free(m);
    return -1;
  }

  acquire(&tuntap_state.lock);
  if(!u->allocated){
    release(&tuntap_state.lock);
    mbuf_free(m);
    return -1;
  }

  n = enqueue_from_mbuf_locked(u, m);
  if(n < 0){
    u->ifp.if_oerrors++;
    release(&tuntap_state.lock);
    mbuf_free(m);
    return -1;
  }

  u->ifp.if_opackets++;
  u->ifp.if_obytes += (uint)n;
  release(&tuntap_state.lock);
  mbuf_free(m);
  return 0;
}

static int
register_if_locked(int idx)
{
  struct tuntap_unit *u;

  if(idx < 0 || idx >= TUNTAP_MAX_UNITS)
    return -1;
  u = &tuntap_state.units[idx];
  if(!u->allocated)
    return -1;
  if(u->if_registered)
    return 0;

  memset(&u->ifp, 0, sizeof(u->ifp));
  memset(&u->if_ops, 0, sizeof(u->if_ops));
  safestrcpy(u->ifp.if_xname, u->name, sizeof(u->ifp.if_xname));
  u->ifp.if_mtu = MBUF_SIZE;
  u->ifp.if_flags = IFF_UP | IFF_RUNNING;
  if(u->mode == IFF_TUN)
    u->ifp.if_flags |= IFF_POINTOPOINT;
  u->ifp.if_softc = u;
  u->ifp.if_input = (u->mode == IFF_TAP) ? ether_input : ip_input;
  u->if_ops.if_output = if_output_tuntap;
  u->ifp.if_ops = &u->if_ops;

  if(if_register(&u->ifp) < 0)
    return -1;

  u->if_registered = 1;
  return 0;
}

int
tuntap_is_ioctl(int request)
{
  switch(request) {
  case TUNSETIFF:
  case TUNGETIFF:
  case TUNSETPERSIST:
  case TUNSETOWNER:
  case TUNSETGROUP:
    return 1;
  default:
    return 0;
  }
}

int
tuntap_ioctl_arg_size(int request)
{
  switch(request) {
  case TUNSETIFF:
  case TUNGETIFF:
    return sizeof(struct ifreq);
  case TUNSETPERSIST:
  case TUNSETOWNER:
  case TUNSETGROUP:
    return sizeof(int);
  default:
    return -1;
  }
}

int
tuntap_ioctl_file(struct file *f, int request, uint arg)
{
  struct ifreq *ifr;
  int val;
  int si;
  int ui;
  short mode;
  short no_pi;
  struct tuntap_unit *u;
  struct proc *p;
  int nm;

  if(f == 0 || f->type != FD_INODE || f->ip == 0)
    return -1;
  if(f->ip->type != T_DEV || f->ip->major != TUNTAPDEV)
    return -1;
  if(!tuntap_is_ioctl(request))
    return -1;

  p = myproc();
  if(p == 0)
    return -1;

  acquire(&tuntap_state.lock);
  si = find_session_locked(f);
  if(si < 0)
    si = alloc_session_locked(f);
  if(si < 0){
    release(&tuntap_state.lock);
    return -1;
  }

  if(request == TUNSETIFF){
    ifr = (struct ifreq*)arg;
    if(ifr == 0){
      release(&tuntap_state.lock);
      return -1;
    }
    if(!mode_valid(ifr->ifr_flags)){
      release(&tuntap_state.lock);
      return -1;
    }

    mode = (ifr->ifr_flags & IFF_TAP) ? IFF_TAP : IFF_TUN;
    no_pi = (ifr->ifr_flags & IFF_NO_PI) ? IFF_NO_PI : 0;

    if(ifr->ifr_name[0] != 0){
      if(!name_valid(ifr->ifr_name)){
        release(&tuntap_state.lock);
        return -1;
      }
      nm = name_mode(ifr->ifr_name);
      if(nm != 0 && nm != mode){
        release(&tuntap_state.lock);
        return -1;
      }
    }

    if(tuntap_state.sessions[si].unit >= 0){
      ui = tuntap_state.sessions[si].unit;
      u = &tuntap_state.units[ui];
      if(!u->allocated || u->mode != mode || !unit_can_use(u, p)){
        release(&tuntap_state.lock);
        return -1;
      }
      safestrcpy(ifr->ifr_name, u->name, sizeof(ifr->ifr_name));
      ifr->ifr_flags = (short)(u->mode | no_pi);
      release(&tuntap_state.lock);
      return 0;
    }

    if(ifr->ifr_name[0] != 0){
      ui = find_named_unit_locked(ifr->ifr_name);
      if(ui >= 0){
        u = &tuntap_state.units[ui];
        if(!u->allocated || u->mode != mode || !unit_can_use(u, p)){
          release(&tuntap_state.lock);
          return -1;
        }
      } else {
        ui = alloc_unit_locked(mode);
        if(ui < 0){
          release(&tuntap_state.lock);
          return -1;
        }
        u = &tuntap_state.units[ui];
        safestrcpy(u->name, ifr->ifr_name, sizeof(u->name));
      }
    } else {
      ui = alloc_unit_locked(mode);
      if(ui < 0){
        release(&tuntap_state.lock);
        return -1;
      }
      u = &tuntap_state.units[ui];
    }

    if(u->owner < 0)
      u->owner = p->uid;
    if(u->group < 0)
      u->group = p->gid;

    if(register_if_locked(ui) < 0){
      maybe_release_unit_locked(ui);
      release(&tuntap_state.lock);
      return -1;
    }

    u->refs++;
    tuntap_state.sessions[si].unit = ui;
    safestrcpy(ifr->ifr_name, u->name, sizeof(ifr->ifr_name));
    ifr->ifr_flags = (short)(u->mode | no_pi);
    release(&tuntap_state.lock);
    return 0;
  }

  if(request == TUNGETIFF){
    ifr = (struct ifreq*)arg;
    if(ifr == 0){
      release(&tuntap_state.lock);
      return -1;
    }
    ui = tuntap_state.sessions[si].unit;
    if(ui < 0 || ui >= TUNTAP_MAX_UNITS || !tuntap_state.units[ui].allocated){
      release(&tuntap_state.lock);
      return -1;
    }
    u = &tuntap_state.units[ui];
    if(!unit_can_use(u, p)){
      release(&tuntap_state.lock);
      return -1;
    }
    safestrcpy(ifr->ifr_name, u->name, sizeof(ifr->ifr_name));
    ifr->ifr_flags = (short)u->mode;
    release(&tuntap_state.lock);
    return 0;
  }

  ui = tuntap_state.sessions[si].unit;
  if(ui < 0 || ui >= TUNTAP_MAX_UNITS || !tuntap_state.units[ui].allocated){
    release(&tuntap_state.lock);
    return -1;
  }
  u = &tuntap_state.units[ui];
  if(!unit_can_admin(u, p)){
    release(&tuntap_state.lock);
    return -1;
  }

  val = (int)arg;
  switch(request) {
  case TUNSETPERSIST:
    if(val != 0 && val != 1){
      release(&tuntap_state.lock);
      return -1;
    }
    u->persist = val;
    release(&tuntap_state.lock);
    return 0;
  case TUNSETOWNER:
    if(val < 0){
      release(&tuntap_state.lock);
      return -1;
    }
    u->owner = val;
    release(&tuntap_state.lock);
    return 0;
  case TUNSETGROUP:
    if(val < 0){
      release(&tuntap_state.lock);
      return -1;
    }
    u->group = val;
    release(&tuntap_state.lock);
    return 0;
  default:
    release(&tuntap_state.lock);
    return -1;
  }
}

int
tuntap_get_nonblock(struct file *f)
{
  int si;

  if(f == 0)
    return -1;
  acquire(&tuntap_state.lock);
  si = find_session_locked(f);
  if(si < 0){
    release(&tuntap_state.lock);
    return -1;
  }
  si = tuntap_state.sessions[si].nonblock;
  release(&tuntap_state.lock);
  return si;
}

int
tuntap_set_nonblock(struct file *f, int enabled)
{
  int si;

  if(f == 0)
    return -1;
  acquire(&tuntap_state.lock);
  si = find_session_locked(f);
  if(si < 0){
    release(&tuntap_state.lock);
    return -1;
  }
  tuntap_state.sessions[si].nonblock = enabled ? 1 : 0;
  release(&tuntap_state.lock);
  return 0;
}

void
tuntap_poll_events(struct file *f, int *rd, int *wr, int *err)
{
  int si;
  int ui;
  struct tuntap_unit *u;

  if(rd)
    *rd = 0;
  if(wr)
    *wr = 0;
  if(err)
    *err = 0;

  if(f == 0)
    return;

  acquire(&tuntap_state.lock);
  si = find_session_locked(f);
  if(si < 0){
    release(&tuntap_state.lock);
    return;
  }

  ui = tuntap_state.sessions[si].unit;
  if(ui < 0 || ui >= TUNTAP_MAX_UNITS || !tuntap_state.units[ui].allocated){
    release(&tuntap_state.lock);
    return;
  }

  u = &tuntap_state.units[ui];
  if(rd && !rxq_empty(u))
    *rd = 1;
  if(wr)
    *wr = 1;
  release(&tuntap_state.lock);
}

int
tuntap_open(struct file *f, int minor, int omode)
{
  int si;

  (void)omode;
  if(f == 0 || minor != 0)
    return -1;

  acquire(&tuntap_state.lock);
  si = find_session_locked(f);
  if(si < 0)
    si = alloc_session_locked(f);
  release(&tuntap_state.lock);

  if(si < 0)
    return -1;
  return 0;
}

void
tuntap_close(struct file *f)
{
  int si;
  int ui;

  if(f == 0)
    return;

  acquire(&tuntap_state.lock);
  si = find_session_locked(f);
  if(si < 0){
    release(&tuntap_state.lock);
    return;
  }

  ui = tuntap_state.sessions[si].unit;
  if(ui >= 0 && ui < TUNTAP_MAX_UNITS && tuntap_state.units[ui].allocated){
    if(tuntap_state.units[ui].refs > 0)
      tuntap_state.units[ui].refs--;
    if(tuntap_state.units[ui].refs == 0 && !tuntap_state.units[ui].persist)
      unregister_if_locked(ui);
    maybe_release_unit_locked(ui);
  }

  tuntap_state.sessions[si].f = 0;
  tuntap_state.sessions[si].unit = -1;
  tuntap_state.sessions[si].nonblock = 0;
  release(&tuntap_state.lock);
}

int
tuntap_fileread(struct file *f, char *dst, int n)
{
  int si;
  int ui;
  int idx;
  int r;
  struct tuntap_unit *u;

  if(f == 0 || dst == 0 || n < 0)
    return -1;

  acquire(&tuntap_state.lock);
  si = find_session_locked(f);
  if(si < 0){
    release(&tuntap_state.lock);
    return -1;
  }

  ui = tuntap_state.sessions[si].unit;
  if(ui < 0 || ui >= TUNTAP_MAX_UNITS || !tuntap_state.units[ui].allocated){
    release(&tuntap_state.lock);
    return -1;
  }

  u = &tuntap_state.units[ui];
  while(rxq_empty(u)){
    if(tuntap_state.sessions[si].nonblock){
      release(&tuntap_state.lock);
      return -1;
    }
    if(myproc()->killed){
      release(&tuntap_state.lock);
      return -1;
    }
    sleep(&u->rx_r, &tuntap_state.lock);
  }

  idx = u->rx_r % TUNTAP_RXQ_LEN;
  r = u->rxq[idx].len;
  if(r > n)
    r = n;
  if(r > 0){
    if((uint)dst < KERNBASE){
      struct proc *p = myproc();
      pde_t *pgdir = p ? proc_pgdir(p) : 0;
      if(pgdir == 0 || copyout(pgdir, (uint)dst, u->rxq[idx].data, (uint)r) < 0){
        release(&tuntap_state.lock);
        return -1;
      }
    } else {
      memmove(dst, u->rxq[idx].data, (uint)r);
    }
  }
  u->rx_r++;
  u->ifp.if_ipackets++;
  u->ifp.if_ibytes += (uint)r;
  release(&tuntap_state.lock);
  return r;
}

int
tuntap_filewrite(struct file *f, char *src, int n)
{
  int si;
  int ui;
  int mode;
  struct ifnet *ifp;
  struct mbuf *m;
  int min_tap_frame;

  if(f == 0 || src == 0 || n < 0 || n > MBUF_SIZE)
    return -1;

  acquire(&tuntap_state.lock);
  si = find_session_locked(f);
  if(si < 0){
    release(&tuntap_state.lock);
    return -1;
  }

  ui = tuntap_state.sessions[si].unit;
  if(ui < 0 || ui >= TUNTAP_MAX_UNITS || !tuntap_state.units[ui].allocated){
    release(&tuntap_state.lock);
    return -1;
  }

  mode = tuntap_state.units[ui].mode;
  ifp = &tuntap_state.units[ui].ifp;
  release(&tuntap_state.lock);

  min_tap_frame = 14;
  if(mode != IFF_TUN && mode != IFF_TAP)
    return -1;
  if(mode == IFF_TAP && n < min_tap_frame)
    return -1;

  m = mbuf_alloc();
  if(m == 0)
    return -1;
  if(n > 0){
    if((uint)src < KERNBASE){
      struct proc *p = myproc();
      pde_t *pgdir = p ? proc_pgdir(p) : 0;
      if(pgdir == 0 || copyin(pgdir, m->data, (uint)src, (uint)n) < 0){
        mbuf_free(m);
        return -1;
      }
    } else {
      memmove(m->data, src, (uint)n);
    }
  }
  m->len = (uint)n;
  m->rcvif = ifp;
  if(mode == IFF_TAP)
    ether_input(ifp, m);
  else
    ip_input(ifp, m);
  return n;
}

int
tuntapread(struct inode *ip, char *dst, uint64_t off, int n)
{
  (void)ip;
  (void)dst;
  (void)off;
  (void)n;
  return -1;
}

int
tuntapwrite(struct inode *ip, char *src, uint64_t off, int n)
{
  (void)ip;
  (void)src;
  (void)off;
  (void)n;
  return -1;
}

void
tuntap_init(void)
{
  initlock(&tuntap_state.lock, "tuntap");
  lockdep_set_rank(&tuntap_state.lock, LOCK_RANK_DEFAULT, "tuntap");
  memset(&tuntap_state.units, 0, sizeof(tuntap_state.units));
  memset(&tuntap_state.sessions, 0, sizeof(tuntap_state.sessions));
  devsw[TUNTAPDEV].read = tuntapread;
  devsw[TUNTAPDEV].write = tuntapwrite;
}
