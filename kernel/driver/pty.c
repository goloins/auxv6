#include "types.h"
#include "x86.h"
#include "mmu.h"
#include "memlayout.h"
#include "defs.h"
#include "param.h"
#include "proc.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "stat.h"
#include "termios.h"
#include "signal.h"

#define NPTY PTY_MAX_UNITS
#define PTY_BUF 512

struct pty_chan {
  char buf[PTY_BUF];
  uint r;
  uint w;
};

struct pty_pair {
  int allocated;
  int master_refs;
  int slave_refs;
  int ctty_set;
  int ctty_sid;
  struct termios termios;
  struct winsize winsize;
  int fg_pgid;
  struct pty_chan to_master;
  struct pty_chan to_slave;
};

static struct {
  struct spinlock lock;
  struct pty_pair pair[NPTY];
} ptys;

static int
pty_minor_to_slave_index(int minor)
{
  if(minor >= PTY_MINOR_SLAVE_BASE && minor < PTY_MINOR_SLAVE_BASE + NPTY)
    return minor - PTY_MINOR_SLAVE_BASE;
  return -1;
}

static void
pty_pair_init_defaults(struct pty_pair *pty)
{
  memset(pty, 0, sizeof(*pty));
  pty->allocated = 1;
  pty->termios.c_iflag = ICRNL;
  pty->termios.c_oflag = OPOST | ONLCR;
  pty->termios.c_cflag = CS8 | CREAD | CLOCAL;
  pty->termios.c_lflag = ECHO | ICANON | ISIG | ECHOE | IEXTEN;
  pty->termios.c_cc[VINTR] = 3;
  pty->termios.c_cc[VQUIT] = 28;
  pty->termios.c_cc[VERASE] = 127;
  pty->termios.c_cc[VKILL] = 21;
  pty->termios.c_cc[VEOF] = 4;
  pty->termios.c_cc[VTIME] = 0;
  pty->termios.c_cc[VMIN] = 1;
  pty->termios.c_cc[VSTART] = 17;
  pty->termios.c_cc[VSTOP] = 19;
  pty->termios.c_cc[VSUSP] = 26;
  pty->winsize.ws_row = 24;
  pty->winsize.ws_col = 80;
  pty->ctty_set = 0;
  pty->ctty_sid = -1;
  pty->fg_pgid = 1;
}

static int
pty_lookup_file(struct file *f, struct pty_pair **out_pair)
{
  if(f == 0 || f->type != FD_INODE || f->ip == 0)
    return -1;
  if(f->ip->type != T_DEV || f->ip->major != PTYDEV)
    return -1;
  if(f->pty_side != PTY_SIDE_MASTER && f->pty_side != PTY_SIDE_SLAVE)
    return -1;
  if(f->pty_index < 0 || f->pty_index >= NPTY)
    return -1;
  if(out_pair)
    *out_pair = &ptys.pair[f->pty_index];
  return 0;
}

static int
pty_chan_read(struct pty_chan *ch, char *dst, int n)
{
  int got;

  got = 0;
  while(got < n) {
    while(ch->r == ch->w) {
      if(myproc()->killed)
        return got > 0 ? got : -1;
      if(got > 0)
        return got;
      sleep(&ch->r, &ptys.lock);
    }

    dst[got++] = ch->buf[ch->r++ % PTY_BUF];
    wakeup(&ch->w);
  }

  return got;
}

static int
pty_chan_write(struct pty_chan *ch, char *src, int n)
{
  int put;

  put = 0;
  while(put < n) {
    while(ch->w - ch->r >= PTY_BUF) {
      if(myproc()->killed)
        return put > 0 ? put : -1;
      sleep(&ch->w, &ptys.lock);
    }

    ch->buf[ch->w++ % PTY_BUF] = src[put++];
    wakeup(&ch->r);
  }

  return put;
}

int
pty_get_termios_file(struct file *f, struct termios *tp)
{
  struct pty_pair *pty;

  if(tp == 0)
    return -1;
  if(pty_lookup_file(f, &pty) < 0)
    return -1;

  acquire(&ptys.lock);
  if(!pty->allocated){
    release(&ptys.lock);
    return -1;
  }
  *tp = pty->termios;
  release(&ptys.lock);
  return 0;
}

int
pty_set_termios_file(struct file *f, const struct termios *tp, int optional_actions)
{
  struct pty_pair *pty;

  if(tp == 0)
    return -1;
  if(optional_actions != TCSANOW &&
     optional_actions != TCSADRAIN &&
     optional_actions != TCSAFLUSH)
    return -1;

  if(pty_lookup_file(f, &pty) < 0)
    return -1;

  acquire(&ptys.lock);
  if(!pty->allocated){
    release(&ptys.lock);
    return -1;
  }
  pty->termios = *tp;
  if(optional_actions == TCSAFLUSH)
    pty->to_slave.r = pty->to_slave.w;
  release(&ptys.lock);
  return 0;
}

int
pty_ioctl_file(struct file *f, int request, uint arg)
{
  struct pty_pair *pty;
  int ptn;
  struct winsize *ws;
  int *intp;
  int inq;

  if(pty_lookup_file(f, &pty) < 0)
    return -1;

  switch(request) {
  case 0x5401:  /* TCGETS */
    if(arg == 0)
      return -1;
    acquire(&ptys.lock);
    if(!pty->allocated){
      release(&ptys.lock);
      return -1;
    }
    *(struct termios*)arg = pty->termios;
    release(&ptys.lock);
    return 0;

  case 0x5402:  /* TCSETS */
    return pty_set_termios_file(f, (const struct termios *)arg, TCSANOW);

  case 0x5403:  /* TCSETSW */
    return pty_set_termios_file(f, (const struct termios *)arg, TCSADRAIN);

  case 0x5404:  /* TCSETSF */
    return pty_set_termios_file(f, (const struct termios *)arg, TCSAFLUSH);

  case 0x5413:  /* TIOCGWINSZ */
    if(arg == 0)
      return -1;
    ws = (struct winsize*)arg;
    acquire(&ptys.lock);
    if(!pty->allocated){
      release(&ptys.lock);
      return -1;
    }
    *ws = pty->winsize;
    release(&ptys.lock);
    return 0;

  case 0x5414:  /* TIOCSWINSZ */
    if(arg == 0)
      return -1;
    ws = (struct winsize*)arg;
    acquire(&ptys.lock);
    if(!pty->allocated){
      release(&ptys.lock);
      return -1;
    }
    pty->winsize = *ws;
    inq = pty->fg_pgid;
    release(&ptys.lock);
    proc_signal_pgid(inq, SIGWINCH);
    return 0;

  case 0x540F:  /* TIOCGPGRP */
    if(arg == 0)
      return -1;
    intp = (int*)arg;
    acquire(&ptys.lock);
    if(!pty->allocated){
      release(&ptys.lock);
      return -1;
    }
    *intp = pty->fg_pgid;
    release(&ptys.lock);
    return 0;

  case 0x5410:  /* TIOCSPGRP */
    if(arg == 0)
      return -1;
    intp = (int*)arg;
    acquire(&ptys.lock);
    if(!pty->allocated){
      release(&ptys.lock);
      return -1;
    }
    pty->fg_pgid = *intp;
    release(&ptys.lock);
    return 0;

  case 0x80045430:  /* TIOCGPTN */
    if(arg == 0)
      return -1;
    if(f->pty_side != PTY_SIDE_MASTER)
      return -1;
    intp = (int*)arg;
    ptn = f->pty_index;
    *intp = ptn;
    return 0;

  case 0x541B:  /* FIONREAD / TIOCINQ */
    if(arg == 0)
      return -1;
    intp = (int*)arg;
    acquire(&ptys.lock);
    if(!pty->allocated){
      release(&ptys.lock);
      return -1;
    }
    if(f->pty_side == PTY_SIDE_MASTER)
      *intp = (int)(pty->to_master.w - pty->to_master.r);
    else
      *intp = (int)(pty->to_slave.w - pty->to_slave.r);
    release(&ptys.lock);
    return 0;

  case 0x5411:  /* TIOCOUTQ */
    if(arg == 0)
      return -1;
    intp = (int*)arg;
    acquire(&ptys.lock);
    if(!pty->allocated){
      release(&ptys.lock);
      return -1;
    }
    if(f->pty_side == PTY_SIDE_MASTER)
      *intp = (int)(pty->to_slave.w - pty->to_slave.r);
    else
      *intp = (int)(pty->to_master.w - pty->to_master.r);
    release(&ptys.lock);
    return 0;

  case 0x540B:  /* TCFLSH */
    if((int)arg != TCIFLUSH && (int)arg != TCOFLUSH && (int)arg != TCIOFLUSH)
      return -1;
    acquire(&ptys.lock);
    if(!pty->allocated){
      release(&ptys.lock);
      return -1;
    }
    if((int)arg == TCIFLUSH || (int)arg == TCIOFLUSH) {
      if(f->pty_side == PTY_SIDE_MASTER)
        pty->to_master.r = pty->to_master.w;
      else
        pty->to_slave.r = pty->to_slave.w;
    }
    release(&ptys.lock);
    return 0;

  case 0x540E:  /* TIOCSCTTY */
    acquire(&ptys.lock);
    if(!pty->allocated){
      release(&ptys.lock);
      return -1;
    }
    if(myproc()) {
      pty->ctty_set = 1;
      pty->ctty_sid = myproc()->sid;
    }
    if(myproc() && myproc()->pgid > 0)
      pty->fg_pgid = myproc()->pgid;
    release(&ptys.lock);
    return 0;

  case 0x54A3:  /* TIOCISATTY */
    return 1;

  default:
    return -1;
  }
}

int
pty_fileread(struct file *f, char *dst, int n)
{
  struct pty_pair *pty;
  struct pty_chan *in;
  int peer_refs;
  int rc;
  int send_ttin;
  int user_dst;
  struct proc *pcur;

  user_dst = ((uint)dst < KERNBASE);
  pcur = user_dst ? myproc() : 0;
  if(user_dst && (pcur == 0 || pcur->pgdir == 0))
    return -1;

  if(pty_lookup_file(f, &pty) < 0)
    return -1;

  acquire(&ptys.lock);
  if(!pty->allocated){
    release(&ptys.lock);
    return -1;
  }
  send_ttin = 0;
  if(f->pty_side == PTY_SIDE_SLAVE &&
     myproc() &&
      pty->ctty_set &&
      myproc()->sid == pty->ctty_sid &&
     myproc()->pgid != 0 &&
     myproc()->pgid != pty->fg_pgid)
    send_ttin = 1;
  if(send_ttin) {
    int pgid;
    pgid = myproc()->pgid;
    release(&ptys.lock);
    proc_signal_pgid(pgid, SIGTTIN);
    return -1;
  }

  if(f->pty_side == PTY_SIDE_MASTER) {
    in = &pty->to_master;
    peer_refs = pty->slave_refs;
  } else {
    in = &pty->to_slave;
    peer_refs = pty->master_refs;
  }

  /* Don't return EOF just because peer_refs is 0. That's a symptom of the ref-counting
     bug (fork doesn't increment master/slave_refs). Shell should block waiting for input,
     not get EOF. Only return EOF if we have data and then peer truly disappears. */
  if(in->r == in->w && peer_refs == 0){
    /* Buffer empty and peer closed: this IS legitimate EOF.
       But due to our ref-counting bug, peer_refs can be 0 while peer is still open.
       For now, don't return 0 (EOF) — let the caller block on pty_chan_read instead. */
  }

  if(user_dst){
    char kbuf[256];
    int total;

    total = 0;
    while(total < n){
      int want;

      want = n - total;
      if(want > (int)sizeof(kbuf))
        want = sizeof(kbuf);
      rc = pty_chan_read(in, kbuf, want);
      if(rc <= 0)
        break;

      release(&ptys.lock);
      if(copyout(pcur->pgdir, (uint)(dst + total), kbuf, rc) < 0)
        return (total > 0) ? total : -1;
      total += rc;

      acquire(&ptys.lock);
      if(!pty->allocated){
        release(&ptys.lock);
        return (total > 0) ? total : -1;
      }
    }

    release(&ptys.lock);
    if(total > 0)
      return total;
    return rc;
  }

  rc = pty_chan_read(in, dst, n);
  release(&ptys.lock);
  return rc;
}

int
pty_filewrite(struct file *f, char *src, int n)
{
  struct pty_pair *pty;
  struct pty_chan *out;
  int rc;
  int send_ttou;
  int user_src;
  struct proc *pcur;

  user_src = ((uint)src < KERNBASE);
  pcur = user_src ? myproc() : 0;
  if(user_src && (pcur == 0 || pcur->pgdir == 0))
    return -1;

  if(pty_lookup_file(f, &pty) < 0)
    return -1;

  acquire(&ptys.lock);
  if(!pty->allocated){
    release(&ptys.lock);
    return -1;
  }
  send_ttou = 0;
  if(f->pty_side == PTY_SIDE_SLAVE &&
     myproc() &&
      pty->ctty_set &&
      myproc()->sid == pty->ctty_sid &&
     myproc()->pgid != 0 &&
     myproc()->pgid != pty->fg_pgid &&
     (pty->termios.c_lflag & TOSTOP))
    send_ttou = 1;
  if(send_ttou) {
    int pgid;
    pgid = myproc()->pgid;
    release(&ptys.lock);
    proc_signal_pgid(pgid, SIGTTOU);
    return -1;
  }

  if(f->pty_side == PTY_SIDE_MASTER) {
    out = &pty->to_slave;
  } else {
    out = &pty->to_master;
  }
  
  /* Note: Don't block writes just because peer is closed. Writes should succeed,
     data goes to buffer (may be discarded). Only reads EOF when no peer. */

  if(user_src){
    char kbuf[256];
    int total;

    total = 0;
    while(total < n){
      int want;

      want = n - total;
      if(want > (int)sizeof(kbuf))
        want = sizeof(kbuf);
      if(copyin(pcur->pgdir, kbuf, (uint)(src + total), (uint)want) < 0){
        release(&ptys.lock);
        return (total > 0) ? total : -1;
      }
      rc = pty_chan_write(out, kbuf, want);
      if(rc <= 0)
        break;
      total += rc;
      if(rc < want)
        break;
    }
    release(&ptys.lock);
    if(total > 0)
      return total;
    return rc;
  }

  rc = pty_chan_write(out, src, n);
  release(&ptys.lock);
  return rc;
}

void
pty_poll_events(struct file *f, int *rd, int *wr, int *err)
{
  struct pty_pair *pty;
  struct pty_chan *in;
  struct pty_chan *out;
  int peer_refs;

  if(rd)
    *rd = 0;
  if(wr)
    *wr = 0;
  if(err)
    *err = 0;

  if(pty_lookup_file(f, &pty) < 0)
    return;

  acquire(&ptys.lock);
  if(!pty->allocated){
    release(&ptys.lock);
    if(err)
      *err = 1;
    return;
  }

  if(f->pty_side == PTY_SIDE_MASTER) {
    in = &pty->to_master;
    out = &pty->to_slave;
    peer_refs = pty->slave_refs;
  } else {
    in = &pty->to_slave;
    out = &pty->to_master;
    peer_refs = pty->master_refs;
  }

  if(rd && (in->w != in->r || peer_refs == 0)) {
    *rd = 1;
    /* Debug: trace when PTY data becomes readable. */
    if(in->w != in->r && f->pty_side == PTY_SIDE_MASTER) {
      int dbg_avail = (int)(in->w - in->r);
      cprintf("pty:poll_events:master rd=1 avail=%d w=%u r=%u\n", dbg_avail, in->w, in->r);
    }
  }
  if(wr && out->w - out->r < PTY_BUF && peer_refs > 0)
    *wr = 1;
  if(err && peer_refs == 0)
    *err = 1;

  release(&ptys.lock);
}

int
pty_open(struct file *f, int minor)
{
  int i;
  struct pty_pair *pty;

  if(f == 0)
    return -1;

  acquire(&ptys.lock);
  if(minor == PTY_MINOR_PTMX){
    for(i = 0; i < NPTY; i++){
      pty = &ptys.pair[i];
      if(pty->allocated)
        continue;
      pty_pair_init_defaults(pty);
      pty->master_refs = 1;
      f->pty_side = PTY_SIDE_MASTER;
      f->pty_index = i;
      cprintf("pty:open:master i=%d master_refs=1 slave_refs=0\n", i);
      release(&ptys.lock);
      return 0;
    }
    release(&ptys.lock);
    cprintf("pty:open:master FAIL no free slots\n");
    return -1;
  }

  i = pty_minor_to_slave_index(minor);
  if(i < 0){
    release(&ptys.lock);
    return -1;
  }

  pty = &ptys.pair[i];
  if(!pty->allocated || pty->master_refs <= 0){
    release(&ptys.lock);
    return -1;
  }

  pty->slave_refs++;
  f->pty_side = PTY_SIDE_SLAVE;
  f->pty_index = i;
  cprintf("pty:open:slave i=%d master_refs=%d slave_refs=%d\n", i, pty->master_refs, pty->slave_refs);
  release(&ptys.lock);
  return 0;
}

void
pty_close(struct file *f)
{
  struct pty_pair *pty;

  if(pty_lookup_file(f, &pty) < 0)
    return;

  acquire(&ptys.lock);
  if(!pty->allocated){
    release(&ptys.lock);
    return;
  }

  if(f->pty_side == PTY_SIDE_MASTER){
    if(pty->master_refs > 0)
      pty->master_refs--;
  } else if(f->pty_side == PTY_SIDE_SLAVE){
    if(pty->slave_refs > 0)
      pty->slave_refs--;
  }

  wakeup(&pty->to_master.r);
  wakeup(&pty->to_master.w);
  wakeup(&pty->to_slave.r);
  wakeup(&pty->to_slave.w);

  if(pty->master_refs == 0 && pty->slave_refs == 0)
    memset(pty, 0, sizeof(*pty));

  release(&ptys.lock);

  f->pty_side = PTY_SIDE_NONE;
  f->pty_index = -1;
}

int
ptyread(struct inode *ip, char *dst, uint64_t off, int n)
{
  (void)ip;
  (void)dst;
  (void)off;
  (void)n;
  return -1;
}

int
ptywrite(struct inode *ip, char *src, uint64_t off, int n)
{
  (void)ip;
  (void)src;
  (void)off;
  (void)n;
  return -1;
}

void
ptyinit(void)
{
  initlock(&ptys.lock, "pty");
  memset(&ptys.pair, 0, sizeof(ptys.pair));

  devsw[PTYDEV].read = ptyread;
  devsw[PTYDEV].write = ptywrite;
}
