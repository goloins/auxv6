#include "types.h"
#include "x86.h"
#include "mmu.h"
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

#define NPTY 1
#define PTY_BUF 512
#define PTY_MINOR_PTMX 0
#define PTY_MINOR_SLAVE_BASE 1

struct pty_chan {
  char buf[PTY_BUF];
  uint r;
  uint w;
};

struct pty_pair {
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
pty_minor_to_index(int minor)
{
  if(minor == PTY_MINOR_PTMX)
    return 0;
  if(minor >= PTY_MINOR_SLAVE_BASE && minor < PTY_MINOR_SLAVE_BASE + NPTY)
    return minor - PTY_MINOR_SLAVE_BASE;
  return -1;
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
pty_get_termios(int minor, struct termios *tp)
{
  int idx;

  if(tp == 0)
    return -1;
  idx = pty_minor_to_index(minor);
  if(idx < 0)
    return -1;

  acquire(&ptys.lock);
  *tp = ptys.pair[idx].termios;
  release(&ptys.lock);
  return 0;
}

int
pty_set_termios(int minor, const struct termios *tp, int optional_actions)
{
  int idx;

  if(tp == 0)
    return -1;
  if(optional_actions != TCSANOW &&
     optional_actions != TCSADRAIN &&
     optional_actions != TCSAFLUSH)
    return -1;

  idx = pty_minor_to_index(minor);
  if(idx < 0)
    return -1;

  acquire(&ptys.lock);
  ptys.pair[idx].termios = *tp;
  if(optional_actions == TCSAFLUSH)
    ptys.pair[idx].to_slave.r = ptys.pair[idx].to_slave.w;
  release(&ptys.lock);
  return 0;
}

int
pty_ioctl(int fd, int request, uint arg)
{
  struct proc *p;
  struct file *f;
  struct inode *ip;
  struct pty_pair *pty;
  struct winsize *ws;
  int *intp;
  int idx;
  int inq;

  p = myproc();
  if(p == 0 || fd < 0 || fd >= NOFILE)
    return -1;
  f = p->ofile[fd];
  if(f == 0 || f->type != FD_INODE || f->ip == 0)
    return -1;
  ip = f->ip;
  if(ip->type != T_DEV || ip->major != PTYDEV)
    return -1;

  idx = pty_minor_to_index(ip->minor);
  if(idx < 0)
    return -1;

  switch(request) {
  case 0x5401:  /* TCGETS */
    if(arg == 0)
      return -1;
    acquire(&ptys.lock);
    *(struct termios*)arg = ptys.pair[idx].termios;
    release(&ptys.lock);
    return 0;

  case 0x5402:  /* TCSETS */
    return pty_set_termios(ip->minor, (const struct termios *)arg, TCSANOW);

  case 0x5403:  /* TCSETSW */
    return pty_set_termios(ip->minor, (const struct termios *)arg, TCSADRAIN);

  case 0x5404:  /* TCSETSF */
    return pty_set_termios(ip->minor, (const struct termios *)arg, TCSAFLUSH);

  case 0x5413:  /* TIOCGWINSZ */
    if(arg == 0)
      return -1;
    ws = (struct winsize*)arg;
    acquire(&ptys.lock);
    *ws = ptys.pair[idx].winsize;
    release(&ptys.lock);
    return 0;

  case 0x5414:  /* TIOCSWINSZ */
    if(arg == 0)
      return -1;
    ws = (struct winsize*)arg;
    acquire(&ptys.lock);
    ptys.pair[idx].winsize = *ws;
    inq = ptys.pair[idx].fg_pgid;
    release(&ptys.lock);
    proc_signal_pgid(inq, SIGWINCH);
    return 0;

  case 0x540F:  /* TIOCGPGRP */
    if(arg == 0)
      return -1;
    intp = (int*)arg;
    acquire(&ptys.lock);
    *intp = ptys.pair[idx].fg_pgid;
    release(&ptys.lock);
    return 0;

  case 0x5410:  /* TIOCSPGRP */
    if(arg == 0)
      return -1;
    intp = (int*)arg;
    acquire(&ptys.lock);
    ptys.pair[idx].fg_pgid = *intp;
    release(&ptys.lock);
    return 0;

  case 0x541B:  /* FIONREAD / TIOCINQ */
    if(arg == 0)
      return -1;
    intp = (int*)arg;
    acquire(&ptys.lock);
    pty = &ptys.pair[idx];
    if(ip->minor == PTY_MINOR_PTMX)
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
    pty = &ptys.pair[idx];
    if(ip->minor == PTY_MINOR_PTMX)
      *intp = (int)(pty->to_slave.w - pty->to_slave.r);
    else
      *intp = (int)(pty->to_master.w - pty->to_master.r);
    release(&ptys.lock);
    return 0;

  case 0x540B:  /* TCFLSH */
    if((int)arg != TCIFLUSH && (int)arg != TCOFLUSH && (int)arg != TCIOFLUSH)
      return -1;
    acquire(&ptys.lock);
    pty = &ptys.pair[idx];
    if((int)arg == TCIFLUSH || (int)arg == TCIOFLUSH) {
      if(ip->minor == PTY_MINOR_PTMX)
        pty->to_master.r = pty->to_master.w;
      else
        pty->to_slave.r = pty->to_slave.w;
    }
    release(&ptys.lock);
    return 0;

  case 0x540E:  /* TIOCSCTTY */
    return 0;

  case 0x54A3:  /* TIOCISATTY */
    return 1;

  default:
    return -1;
  }
}

int
ptyread(struct inode *ip, char *dst, uint off, int n)
{
  struct pty_pair *pty;
  struct pty_chan *in;
  int idx;
  int rc;

  (void)off;

  idx = pty_minor_to_index(ip->minor);
  if(idx < 0)
    return -1;

  iunlock(ip);
  acquire(&ptys.lock);
  pty = &ptys.pair[idx];
  in = (ip->minor == PTY_MINOR_PTMX) ? &pty->to_master : &pty->to_slave;
  rc = pty_chan_read(in, dst, n);
  release(&ptys.lock);
  ilock(ip);
  return rc;
}

int
ptywrite(struct inode *ip, char *src, uint off, int n)
{
  struct pty_pair *pty;
  struct pty_chan *out;
  int idx;
  int rc;

  (void)off;

  idx = pty_minor_to_index(ip->minor);
  if(idx < 0)
    return -1;

  iunlock(ip);
  acquire(&ptys.lock);
  pty = &ptys.pair[idx];
  out = (ip->minor == PTY_MINOR_PTMX) ? &pty->to_slave : &pty->to_master;
  rc = pty_chan_write(out, src, n);
  release(&ptys.lock);
  ilock(ip);
  return rc;
}

void
ptyinit(void)
{
  struct pty_pair *pty;

  initlock(&ptys.lock, "pty");
  acquire(&ptys.lock);

  pty = &ptys.pair[0];
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
  pty->fg_pgid = 1;

  release(&ptys.lock);

  devsw[PTYDEV].read = ptyread;
  devsw[PTYDEV].write = ptywrite;
}
