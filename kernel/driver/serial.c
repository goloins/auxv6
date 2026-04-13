/*
 * serial.c - serial character-device endpoints (/dev/ttyS*)
 *
 * ttyS0 (minor 1) is the only COM1-backed line today.
 * ttyS1..ttyS3 are isolated virtual lines so userspace can depend on node
 * presence without mutating shared COM1 hardware policy/state.
 */

#include "types.h"
#include "defs.h"
#include "param.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "stat.h"
#include "file.h"
#include "mmu.h"
#include "proc.h"
#include "signal.h"
#include "termios.h"
#include "sys/ioctl.h"
#include "fcntl.h"

#define SERIAL_INPUT_BUF 256
#define SERIAL_MIN_MINOR 1
#define SERIAL_MAX_MINOR 4
#define SERIAL_HW_MINOR 1
#define SERIAL_LINE_SLOTS (SERIAL_MAX_MINOR + 1)

struct serial_input_state {
  uint r;
  uint w;
  char buf[SERIAL_INPUT_BUF];
};

struct serial_line_state {
  int minor;
  int hw_backed;
  int open_refs;
  int fg_pgid;
  int carrier_present;
  int hungup;
  int hup_pending;
  uint hup_pgid;
  uint modem_out_bits;
  struct termios termios;
  struct winsize winsize;
  struct serial_input_state input;
};

static struct {
  struct spinlock lock;
  int initialized;
  struct serial_line_state line[SERIAL_LINE_SLOTS];
} serials;

int serialread(struct inode *ip, char *dst, uint64_t off, int n);
int serialwrite(struct inode *ip, char *buf, uint64_t off, int n);

static int
serial_minor_valid(int minor)
{
  return minor >= SERIAL_MIN_MINOR && minor <= SERIAL_MAX_MINOR;
}

static struct serial_line_state *
serial_line_from_minor_locked(int minor)
{
  if(!serial_minor_valid(minor))
    return 0;
  return &serials.line[minor];
}

static struct serial_line_state *
serial_line_from_file_locked(struct file *f)
{
  if(f == 0 || f->type != FD_INODE || f->ip == 0)
    return 0;
  if(f->ip->type != T_DEV || f->ip->major != SERIALDEV)
    return 0;
  return serial_line_from_minor_locked(f->ip->minor);
}

static int
serial_should_ignore_carrier_locked(struct serial_line_state *ln)
{
  return (ln->termios.c_cflag & CLOCAL) != 0;
}

static void
serial_deliver_pending_hup(struct serial_line_state *ln)
{
  uint pgid;

  pgid = 0;
  acquire(&serials.lock);
  if(ln->hup_pending) {
    pgid = ln->hup_pgid;
    ln->hup_pending = 0;
    ln->hup_pgid = 0;
  }
  release(&serials.lock);

  if(pgid != 0) {
    proc_signal_pgid((int)pgid, SIGHUP);
    proc_signal_pgid((int)pgid, SIGCONT);
  }
}

static void
serial_set_default_termios(struct termios *t)
{
  memset(t, 0, sizeof(*t));
  t->c_iflag = ICRNL | IXON;
  t->c_oflag = OPOST | ONLCR;
  t->c_cflag = CREAD | CS8 | CLOCAL | B9600;
  t->c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | IEXTEN;
  t->c_cc[VINTR] = 3;
  t->c_cc[VQUIT] = 28;
  t->c_cc[VERASE] = 127;
  t->c_cc[VKILL] = 21;
  t->c_cc[VEOF] = 4;
  t->c_cc[VTIME] = 0;
  t->c_cc[VMIN] = 1;
  t->c_cc[VSTART] = 17;
  t->c_cc[VSTOP] = 19;
  t->c_cc[VSUSP] = 26;
}

static int
serial_apply_termios_locked(struct serial_line_state *ln, const struct termios *tp, int optional_actions)
{
  int old_clocal;
  int new_clocal;

  if(tp == 0)
    return -1;
  if(optional_actions != TCSANOW &&
     optional_actions != TCSADRAIN &&
     optional_actions != TCSAFLUSH)
    return -1;

  old_clocal = (ln->termios.c_cflag & CLOCAL) != 0;
  ln->termios = *tp;
  new_clocal = (ln->termios.c_cflag & CLOCAL) != 0;

  if(!old_clocal && new_clocal)
    ln->hungup = 0;
  if(old_clocal && !new_clocal && ln->open_refs > 0 && !ln->carrier_present)
    ln->hungup = 1;

  if(optional_actions == TCSAFLUSH)
    ln->input.r = ln->input.w;

  wakeup(&ln->carrier_present);
  wakeup(&ln->input.r);
  return 0;
}

static int
serial_buf_putc(char *buf, uint max, uint *len, char c)
{
  if(*len >= max)
    return -1;
  buf[*len] = c;
  (*len)++;
  return 0;
}

static int
serial_buf_puts(char *buf, uint max, uint *len, const char *s)
{
  uint i;

  for(i = 0; s[i]; i++){
    if(serial_buf_putc(buf, max, len, s[i]) < 0)
      return -1;
  }
  return 0;
}

static int
serial_buf_putu(char *buf, uint max, uint *len, uint value)
{
  char tmp[16];
  uint n;
  uint i;

  n = 0;
  do {
    tmp[n++] = '0' + (value % 10);
    value /= 10;
  } while(value > 0);

  for(i = 0; i < n; i++){
    if(serial_buf_putc(buf, max, len, tmp[n - i - 1]) < 0)
      return -1;
  }
  return 0;
}

void
serialinit(void)
{
  int minor;
  uint mbits;

  initlock(&serials.lock, "serial");
  lockdep_set_rank(&serials.lock, LOCK_RANK_DEFAULT, "serial");

  for(minor = SERIAL_MIN_MINOR; minor <= SERIAL_MAX_MINOR; minor++) {
    struct serial_line_state *ln = &serials.line[minor];

    memset(ln, 0, sizeof(*ln));
    ln->minor = minor;
    ln->hw_backed = (minor == SERIAL_HW_MINOR);
    serial_set_default_termios(&ln->termios);
    ln->winsize.ws_row = 24;
    ln->winsize.ws_col = 80;
    ln->winsize.ws_xpixel = 0;
    ln->winsize.ws_ypixel = 0;
    ln->modem_out_bits = TIOCM_DTR | TIOCM_RTS;
    if(ln->hw_backed) {
      mbits = uart_get_modem_bits();
      ln->carrier_present = (mbits & TIOCM_CAR) ? 1 : 0;
      ln->hungup = ln->carrier_present ? 0 : 1;
    } else {
      /* Virtual placeholder lines stay independent from COM1 state. */
      ln->carrier_present = 1;
      ln->hungup = 0;
    }
  }

  serials.initialized = 1;
  devsw[SERIALDEV].read = serialread;
  devsw[SERIALDEV].write = serialwrite;

  /* ttyS0 policy drives the only current hardware UART instance. */
  uart_apply_termios(serials.line[SERIAL_HW_MINOR].termios.c_cflag);
  uart_set_modem_control(serials.line[SERIAL_HW_MINOR].modem_out_bits, 0);
}

int
serial_open(struct file *f, int minor, int omode)
{
  int nonblock;
  struct proc *p;
  struct serial_line_state *ln;

  nonblock = (omode & O_NONBLOCK) != 0;
  p = myproc();

  acquire(&serials.lock);
  ln = serial_line_from_minor_locked(minor);
  if(!serials.initialized || ln == 0) {
    release(&serials.lock);
    return -1;
  }

  ln->open_refs++;
  if(ln->open_refs == 1)
    ln->hungup = ln->carrier_present ? 0 : 1;

  while(!serial_should_ignore_carrier_locked(ln) && !ln->carrier_present) {
    if(nonblock || (p && p->killed)) {
      if(ln->open_refs > 0)
        ln->open_refs--;
      if(ln->open_refs == 0)
        ln->hungup = 1;
      release(&serials.lock);
      return -1;
    }
    sleep(&ln->carrier_present, &serials.lock);
  }

  ln->hungup = 0;
  release(&serials.lock);
  return 0;
}

void
serial_close(struct file *f)
{
  struct serial_line_state *ln;
  uint clear_bits;

  clear_bits = 0;
  acquire(&serials.lock);
  ln = serial_line_from_file_locked(f);
  if(ln == 0) {
    release(&serials.lock);
    return;
  }

  if(ln->open_refs > 0)
    ln->open_refs--;

  if(ln->open_refs == 0) {
    if(ln->hw_backed && (ln->termios.c_cflag & HUPCL) != 0)
      clear_bits = TIOCM_DTR | TIOCM_RTS;
    ln->hungup = 1;
  }

  wakeup(&ln->carrier_present);
  release(&serials.lock);

  if(clear_bits)
    uart_set_modem_control(0, clear_bits);
}

void
serial_modem_update(uint bits)
{
  struct serial_line_state *ln;
  int was_carrier;
  int now_carrier;
  int signal_hup;

  signal_hup = 0;
  acquire(&serials.lock);
  ln = &serials.line[SERIAL_HW_MINOR];
  was_carrier = ln->carrier_present;
  now_carrier = (bits & TIOCM_CAR) ? 1 : 0;
  ln->carrier_present = now_carrier;

  if(!was_carrier && now_carrier)
    ln->hungup = 0;

  if(was_carrier && !now_carrier && ln->open_refs > 0 &&
     !serial_should_ignore_carrier_locked(ln)) {
    ln->hungup = 1;
    if(ln->fg_pgid > 0) {
      ln->hup_pending = 1;
      ln->hup_pgid = (uint)ln->fg_pgid;
      signal_hup = 1;
    }
  }

  wakeup(&ln->carrier_present);
  wakeup(&ln->input.r);
  release(&serials.lock);

  if(signal_hup)
    serial_deliver_pending_hup(ln);
}

void
serial_rx_char(int c)
{
  struct serial_line_state *ln;
  uint iflag;

  if(c < 0)
    return;

  acquire(&serials.lock);
  if(!serials.initialized) {
    release(&serials.lock);
    return;
  }

  ln = &serials.line[SERIAL_HW_MINOR];
  iflag = ln->termios.c_iflag;

  if(c == '\r' && (iflag & ICRNL))
    c = '\n';
  else if(c == '\n' && (iflag & INLCR))
    c = '\r';

  if(c == '\r' && (iflag & IGNCR)) {
    release(&serials.lock);
    return;
  }

  if(iflag & ISTRIP)
    c &= 0x7f;

  if(ln->input.w - ln->input.r >= SERIAL_INPUT_BUF)
    ln->input.r++;

  ln->input.buf[ln->input.w++ % SERIAL_INPUT_BUF] = (char)c;
  wakeup(&ln->input.r);
  release(&serials.lock);
}

int
serial_get_termios_file(struct file *f, struct termios *tp)
{
  struct serial_line_state *ln;

  if(tp == 0)
    return -1;

  acquire(&serials.lock);
  ln = serial_line_from_file_locked(f);
  if(ln == 0) {
    release(&serials.lock);
    return -1;
  }
  *tp = ln->termios;
  release(&serials.lock);
  return 0;
}

int
serial_set_termios_file(struct file *f, const struct termios *tp, int optional_actions)
{
  struct serial_line_state *ln;
  int hw_backed;
  uint cflag;
  int rc;

  acquire(&serials.lock);
  ln = serial_line_from_file_locked(f);
  if(ln == 0) {
    release(&serials.lock);
    return -1;
  }

  rc = serial_apply_termios_locked(ln, tp, optional_actions);
  hw_backed = ln->hw_backed;
  cflag = ln->termios.c_cflag;
  release(&serials.lock);

  if(rc < 0)
    return rc;

  if(hw_backed)
    uart_apply_termios(cflag);
  return 0;
}

int
serialread(struct inode *ip, char *dst, uint64_t off, int n)
{
  struct serial_line_state *ln;
  int target;
  int c;
  int canonical;
  int got;
  int vmin;
  int vtime;
  int timed_mode;
  uint deadline;
  uint now;
  uchar veof;
  uchar veol;
  struct proc *p;

  (void)off;
  if(n < 0 || ip == 0)
    return -1;

  iunlock(ip);
  acquire(&serials.lock);
  ln = serial_line_from_minor_locked(ip->minor);
  if(ln == 0) {
    release(&serials.lock);
    ilock(ip);
    return -1;
  }

  target = n;
  canonical = (ln->termios.c_lflag & ICANON) != 0;
  got = 0;
  p = myproc();

  vmin = ln->termios.c_cc[VMIN];
  vtime = ln->termios.c_cc[VTIME];
  timed_mode = 0;
  deadline = 0;
  veof = ln->termios.c_cc[VEOF];
  veol = ln->termios.c_cc[VEOL];

  if(!canonical) {
    if(vmin < 0) vmin = 0;
    if(vmin > target) vmin = target;

    if(vmin == 0 && vtime == 0) {
      while(n > 0 && ln->input.r != ln->input.w) {
        *dst++ = ln->input.buf[ln->input.r++ % SERIAL_INPUT_BUF];
        got++;
        n--;
      }
      release(&serials.lock);
      ilock(ip);
      return got;
    }

    if(vtime > 0 && vmin == 0) {
      acquire(&tickslock);
      deadline = ticks + (uint)(vtime * 10);
      timed_mode = 1;
      release(&tickslock);
    }
  }

  while(n > 0) {
    while(ln->input.r == ln->input.w) {
      if(ln->hungup && !serial_should_ignore_carrier_locked(ln)) {
        release(&serials.lock);
        ilock(ip);
        return got;
      }

      if(p && p->killed) {
        release(&serials.lock);
        ilock(ip);
        return -1;
      }

      if(!canonical && vtime > 0) {
        release(&serials.lock);

        acquire(&tickslock);
        if(vmin > 0 && got > 0 && !timed_mode) {
          deadline = ticks + (uint)(vtime * 10);
          timed_mode = 1;
        }

        now = ticks;
        if(timed_mode && (int)(now - deadline) >= 0) {
          release(&tickslock);
          ilock(ip);
          return got;
        }

        sleep(&ticks, &tickslock);
        release(&tickslock);
        acquire(&serials.lock);
        continue;
      }

      sleep(&ln->input.r, &serials.lock);
    }

    c = ln->input.buf[ln->input.r++ % SERIAL_INPUT_BUF];

    if(canonical && veof && c == (int)veof) {
      if(n < target)
        ln->input.r--;
      break;
    }

    *dst++ = c;
    got++;
    n--;

    if(!canonical) {
      if(vtime > 0 && vmin > 0) {
        acquire(&tickslock);
        deadline = ticks + (uint)(vtime * 10);
        timed_mode = 1;
        release(&tickslock);
      }

      if(vmin == 0)
        break;
      if(got >= vmin)
        break;
      continue;
    }

    if(c == '\n' || (veol && c == (int)veol))
      break;
  }

  release(&serials.lock);
  ilock(ip);
  return got;
}

int
serialwrite(struct inode *ip, char *buf, uint64_t off, int n)
{
  struct serial_line_state *ln;
  int i;
  int c;
  uint oflag;
  int hw_backed;

  (void)off;
  if(n < 0 || ip == 0)
    return -1;

  iunlock(ip);
  acquire(&serials.lock);
  ln = serial_line_from_minor_locked(ip->minor);
  if(ln == 0) {
    release(&serials.lock);
    ilock(ip);
    return -1;
  }

  if(ln->hungup && !serial_should_ignore_carrier_locked(ln)) {
    release(&serials.lock);
    ilock(ip);
    return -1;
  }

  oflag = ln->termios.c_oflag;
  hw_backed = ln->hw_backed;

  for(i = 0; i < n; i++) {
    c = buf[i] & 0xff;
    if(hw_backed) {
      if((oflag & OPOST) && (oflag & ONLCR) && c == '\n')
        uartputc('\r');
      uartputc(c);
    }
  }

  release(&serials.lock);
  ilock(ip);
  return n;
}

int
serial_ioctl_file(struct file *f, int request, uint arg)
{
  struct serial_line_state *ln;
  int q;
  int avail;
  int bits;
  int pgid;
  uint old_out;
  int was_carrier;
  int now_carrier;
  int signal_hup;
  int hw_backed;
  uint apply_set;
  uint apply_clear;

  signal_hup = 0;
  apply_set = 0;
  apply_clear = 0;

  acquire(&serials.lock);
  ln = serial_line_from_file_locked(f);
  if(ln == 0) {
    release(&serials.lock);
    return -1;
  }
  hw_backed = ln->hw_backed;

  switch(request) {
  case 0x5401:  /* TCGETS */
    if(arg == 0) {
      release(&serials.lock);
      return -1;
    }
    *(struct termios *)arg = ln->termios;
    release(&serials.lock);
    return 0;

  case 0x5402:  /* TCSETS */
  case 0x5403:  /* TCSETSW */
  case 0x5404:  /* TCSETSF */
    bits = (request == 0x5402) ? TCSANOW :
           (request == 0x5403) ? TCSADRAIN : TCSAFLUSH;
    q = serial_apply_termios_locked(ln, (const struct termios *)arg, bits);
    bits = ln->termios.c_cflag;
    release(&serials.lock);
    if(q < 0)
      return -1;
    if(hw_backed)
      uart_apply_termios((uint)bits);
    return 0;

  case 0x5413:  /* TIOCGWINSZ */
    if(arg == 0) {
      release(&serials.lock);
      return -1;
    }
    *(struct winsize *)arg = ln->winsize;
    release(&serials.lock);
    return 0;

  case 0x5414:  /* TIOCSWINSZ */
    if(arg == 0) {
      release(&serials.lock);
      return -1;
    }
    ln->winsize = *(struct winsize *)arg;
    release(&serials.lock);
    return 0;

  case 0x5411:  /* TIOCOUTQ */
    if(arg == 0) {
      release(&serials.lock);
      return -1;
    }
    *(int *)arg = 0;
    release(&serials.lock);
    return 0;

  case 0x541B:  /* FIONREAD / TIOCINQ */
    if(arg == 0) {
      release(&serials.lock);
      return -1;
    }
    avail = (int)(ln->input.w - ln->input.r);
    *(int *)arg = avail;
    release(&serials.lock);
    return 0;

  case 0x5415:  /* TIOCMGET */
    if(arg == 0) {
      release(&serials.lock);
      return -1;
    }
    if(hw_backed) {
      release(&serials.lock);
      *(int *)arg = (int)uart_get_modem_bits();
      return 0;
    }
    bits = (int)ln->modem_out_bits;
    if(ln->carrier_present)
      bits |= TIOCM_CAR;
    *(int *)arg = bits;
    release(&serials.lock);
    return 0;

  case 0x5418:  /* TIOCMSET */
    if(arg == 0) {
      release(&serials.lock);
      return -1;
    }
    bits = *(int *)arg;
    was_carrier = ln->carrier_present;
    old_out = ln->modem_out_bits;
    ln->modem_out_bits = (uint)bits & (TIOCM_DTR | TIOCM_RTS);
    now_carrier = ((bits & TIOCM_CAR) != 0);
    if(was_carrier && !now_carrier && ln->open_refs > 0 &&
       !serial_should_ignore_carrier_locked(ln)) {
      ln->hungup = 1;
      if(ln->fg_pgid > 0) {
        ln->hup_pending = 1;
        ln->hup_pgid = (uint)ln->fg_pgid;
        signal_hup = 1;
      }
    }
    if(!was_carrier && now_carrier)
      ln->hungup = 0;
    ln->carrier_present = now_carrier;
    wakeup(&ln->carrier_present);
    wakeup(&ln->input.r);
    if(hw_backed) {
      apply_set = ln->modem_out_bits;
      apply_clear = old_out & ~ln->modem_out_bits;
    }
    release(&serials.lock);
    if(hw_backed)
      uart_set_modem_control(apply_set, apply_clear);
    if(signal_hup)
      serial_deliver_pending_hup(ln);
    return 0;

  case 0x5416:  /* TIOCMBIS */
    if(arg == 0) {
      release(&serials.lock);
      return -1;
    }
    bits = *(int *)arg;
    ln->modem_out_bits |= ((uint)bits & (TIOCM_DTR | TIOCM_RTS));
    if(bits & TIOCM_CAR) {
      ln->carrier_present = 1;
      ln->hungup = 0;
      wakeup(&ln->carrier_present);
      wakeup(&ln->input.r);
    }
    if(hw_backed)
      apply_set = (uint)bits & (TIOCM_DTR | TIOCM_RTS);
    release(&serials.lock);
    if(hw_backed)
      uart_set_modem_control(apply_set, 0);
    return 0;

  case 0x5417:  /* TIOCMBIC */
    if(arg == 0) {
      release(&serials.lock);
      return -1;
    }
    bits = *(int *)arg;
    ln->modem_out_bits &= ~((uint)bits & (TIOCM_DTR | TIOCM_RTS));
    if(bits & TIOCM_CAR) {
      if(ln->carrier_present && ln->open_refs > 0 &&
         !serial_should_ignore_carrier_locked(ln)) {
        ln->hungup = 1;
        if(ln->fg_pgid > 0) {
          ln->hup_pending = 1;
          ln->hup_pgid = (uint)ln->fg_pgid;
          signal_hup = 1;
        }
      }
      ln->carrier_present = 0;
      wakeup(&ln->carrier_present);
      wakeup(&ln->input.r);
    }
    if(hw_backed)
      apply_clear = (uint)bits & (TIOCM_DTR | TIOCM_RTS);
    release(&serials.lock);
    if(hw_backed)
      uart_set_modem_control(0, apply_clear);
    if(signal_hup)
      serial_deliver_pending_hup(ln);
    return 0;

  case 0x540F:  /* TIOCGPGRP */
    if(arg == 0) {
      release(&serials.lock);
      return -1;
    }
    *(int *)arg = ln->fg_pgid;
    release(&serials.lock);
    return 0;

  case 0x5410:  /* TIOCSPGRP */
    if(arg == 0) {
      release(&serials.lock);
      return -1;
    }
    pgid = *(int *)arg;
    if(pgid <= 0) {
      release(&serials.lock);
      return -1;
    }
    ln->fg_pgid = pgid;
    release(&serials.lock);
    return 0;

  case 0x540B:  /* TCFLSH */
    q = (int)arg;
    if(q != TCIFLUSH && q != TCOFLUSH && q != TCIOFLUSH) {
      release(&serials.lock);
      return -1;
    }
    if(q == TCIFLUSH || q == TCIOFLUSH)
      ln->input.r = ln->input.w;
    release(&serials.lock);
    return 0;

  case 0x54A3:  /* TIOCISATTY */
    release(&serials.lock);
    return 1;

  default:
    release(&serials.lock);
    return -1;
  }
}

int
serial_procfs_dump(char *buf, uint max)
{
  uint len;
  int minor;

  if(buf == 0 || max == 0)
    return -1;

  acquire(&serials.lock);

  len = 0;
  if(serial_buf_puts(buf, max, &len,
                     "minor name hw_backed carrier hungup open_refs fg_pgid dtr rts icanon clocal\n") < 0)
    goto overflow;

  for(minor = SERIAL_MIN_MINOR; minor <= SERIAL_MAX_MINOR; minor++) {
    struct serial_line_state *ln;
    const char *name;

    ln = &serials.line[minor];
    name = (minor == SERIAL_HW_MINOR) ? "ttyS0" :
           (minor == 2) ? "ttyS1" :
           (minor == 3) ? "ttyS2" : "ttyS3";

    if(serial_buf_putu(buf, max, &len, (uint)minor) < 0)
      goto overflow;
    if(serial_buf_putc(buf, max, &len, ' ') < 0)
      goto overflow;
    if(serial_buf_puts(buf, max, &len, name) < 0)
      goto overflow;
    if(serial_buf_putc(buf, max, &len, ' ') < 0)
      goto overflow;
    if(serial_buf_putu(buf, max, &len, (uint)ln->hw_backed) < 0)
      goto overflow;
    if(serial_buf_putc(buf, max, &len, ' ') < 0)
      goto overflow;
    if(serial_buf_putu(buf, max, &len, (uint)ln->carrier_present) < 0)
      goto overflow;
    if(serial_buf_putc(buf, max, &len, ' ') < 0)
      goto overflow;
    if(serial_buf_putu(buf, max, &len, (uint)ln->hungup) < 0)
      goto overflow;
    if(serial_buf_putc(buf, max, &len, ' ') < 0)
      goto overflow;
    if(serial_buf_putu(buf, max, &len, (uint)ln->open_refs) < 0)
      goto overflow;
    if(serial_buf_putc(buf, max, &len, ' ') < 0)
      goto overflow;
    if(serial_buf_putu(buf, max, &len, (uint)ln->fg_pgid) < 0)
      goto overflow;
    if(serial_buf_putc(buf, max, &len, ' ') < 0)
      goto overflow;
    if(serial_buf_putu(buf, max, &len, (ln->modem_out_bits & TIOCM_DTR) ? 1U : 0U) < 0)
      goto overflow;
    if(serial_buf_putc(buf, max, &len, ' ') < 0)
      goto overflow;
    if(serial_buf_putu(buf, max, &len, (ln->modem_out_bits & TIOCM_RTS) ? 1U : 0U) < 0)
      goto overflow;
    if(serial_buf_putc(buf, max, &len, ' ') < 0)
      goto overflow;
    if(serial_buf_putu(buf, max, &len, (ln->termios.c_lflag & ICANON) ? 1U : 0U) < 0)
      goto overflow;
    if(serial_buf_putc(buf, max, &len, ' ') < 0)
      goto overflow;
    if(serial_buf_putu(buf, max, &len, (ln->termios.c_cflag & CLOCAL) ? 1U : 0U) < 0)
      goto overflow;
    if(serial_buf_putc(buf, max, &len, '\n') < 0)
      goto overflow;
  }

  release(&serials.lock);
  return (int)len;

overflow:
  release(&serials.lock);
  return -1;
}
