#include "types.h"
#include "defs.h"
#include "termios.h"
#include "signal.h"
#include "tty_ldisc.h"

static void
ld_echo_char(char c, char *echo_out, int echo_cap, int *echo_len)
{
  if(*echo_len >= echo_cap)
    return;
  echo_out[(*echo_len)++] = c;
}

static void
ld_echo_erase_one(char *echo_out, int echo_cap, int *echo_len)
{
  ld_echo_char('\b', echo_out, echo_cap, echo_len);
  ld_echo_char(' ', echo_out, echo_cap, echo_len);
  ld_echo_char('\b', echo_out, echo_cap, echo_len);
}

static int
ld_is_word_char(char c)
{
  if(c >= 'a' && c <= 'z')
    return 1;
  if(c >= 'A' && c <= 'Z')
    return 1;
  if(c >= '0' && c <= '9')
    return 1;
  if(c == '_')
    return 1;
  return 0;
}

void
tty_ldisc_init(struct tty_ldisc_state *ld)
{
  tty_ldisc_reset(ld);
}

void
tty_ldisc_reset(struct tty_ldisc_state *ld)
{
  if(ld == 0)
    return;
  memset(ld, 0, sizeof(*ld));
}

static int
ld_store_canon(struct tty_ldisc_state *ld, char c)
{
  if(ld->canon_len >= TTY_LDISC_CANON_BUFSZ)
    return -1;
  ld->canon_buf[ld->canon_len++] = c;
  return 0;
}

int
tty_ldisc_process_input(const struct termios *tp,
                        struct tty_ldisc_state *ld,
                        const char *in,
                        int in_len,
                        char *slave_out,
                        int slave_cap,
                        char *echo_out,
                        int echo_cap,
                        int *echo_len,
                        int *sig_out)
{
  int i;
  int out_len;

  if(tp == 0 || ld == 0 || in == 0 || in_len < 0 || slave_out == 0 || slave_cap < 0 ||
      echo_out == 0 || echo_cap < 0 || echo_len == 0 || sig_out == 0)
    return -1;

  out_len = 0;
  *echo_len = 0;
  *sig_out = 0;

  for(i = 0; i < in_len; i++){
    char c;
    c = in[i];

    if(c == '\r') {
      if(tp->c_iflag & IGNCR)
        continue;
      if(tp->c_iflag & ICRNL)
        c = '\n';
    } else if(c == '\n') {
      if(tp->c_iflag & INLCR)
        c = '\r';
    }

    if(tp->c_iflag & IXON) {
      if(c == (char)tp->c_cc[VSTOP]) {
        ld->flow_stopped = 1;
        continue;
      }
      if(c == (char)tp->c_cc[VSTART]) {
        ld->flow_stopped = 0;
        continue;
      }
    }

    if(tp->c_lflag & ICANON) {
      if((tp->c_lflag & ISIG) && c == (char)tp->c_cc[VINTR]) {
        *sig_out = SIGINT;
        if((tp->c_lflag & ECHO) && (tp->c_lflag & ECHOCTL)) {
          ld_echo_char('^', echo_out, echo_cap, echo_len);
          ld_echo_char('C', echo_out, echo_cap, echo_len);
          ld_echo_char('\n', echo_out, echo_cap, echo_len);
        }
        if(!(tp->c_lflag & NOFLSH))
          ld->canon_len = 0;
        continue;
      }
      if((tp->c_lflag & ISIG) && c == (char)tp->c_cc[VQUIT]) {
        *sig_out = SIGQUIT;
        if((tp->c_lflag & ECHO) && (tp->c_lflag & ECHOCTL)) {
          ld_echo_char('^', echo_out, echo_cap, echo_len);
          ld_echo_char('\\', echo_out, echo_cap, echo_len);
          ld_echo_char('\n', echo_out, echo_cap, echo_len);
        }
        if(!(tp->c_lflag & NOFLSH))
          ld->canon_len = 0;
        continue;
      }
      if((tp->c_lflag & ISIG) && c == (char)tp->c_cc[VSUSP]) {
        *sig_out = SIGTSTP;
        if((tp->c_lflag & ECHO) && (tp->c_lflag & ECHOCTL)) {
          ld_echo_char('^', echo_out, echo_cap, echo_len);
          ld_echo_char('Z', echo_out, echo_cap, echo_len);
          ld_echo_char('\n', echo_out, echo_cap, echo_len);
        }
        if(!(tp->c_lflag & NOFLSH))
          ld->canon_len = 0;
        continue;
      }

      if(c == (char)tp->c_cc[VERASE]) {
        if(ld->canon_len > 0) {
          ld->canon_len--;
          if(tp->c_lflag & ECHOE) {
            ld_echo_erase_one(echo_out, echo_cap, echo_len);
          } else if(tp->c_lflag & ECHO) {
            ld_echo_char(c, echo_out, echo_cap, echo_len);
          }
        }
        continue;
      }

      if((tp->c_lflag & IEXTEN) && c == (char)tp->c_cc[VWERASE]) {
        int erased;

        erased = 0;
        while(ld->canon_len > 0 &&
              (ld->canon_buf[ld->canon_len - 1] == ' ' ||
               ld->canon_buf[ld->canon_len - 1] == '\t')) {
          ld->canon_len--;
          erased++;
        }
        while(ld->canon_len > 0 && ld_is_word_char(ld->canon_buf[ld->canon_len - 1])) {
          ld->canon_len--;
          erased++;
        }

        if((tp->c_lflag & ECHOE) && erased > 0) {
          while(erased-- > 0)
            ld_echo_erase_one(echo_out, echo_cap, echo_len);
        } else if(tp->c_lflag & ECHO) {
          ld_echo_char(c, echo_out, echo_cap, echo_len);
        }
        continue;
      }

      if((tp->c_lflag & IEXTEN) && c == (char)tp->c_cc[VREPRINT]) {
        int j;

        if(tp->c_lflag & ECHO) {
          if(tp->c_lflag & ECHOCTL) {
            ld_echo_char('^', echo_out, echo_cap, echo_len);
            ld_echo_char('R', echo_out, echo_cap, echo_len);
          } else {
            ld_echo_char(c, echo_out, echo_cap, echo_len);
          }
          ld_echo_char('\n', echo_out, echo_cap, echo_len);
          for(j = 0; j < ld->canon_len; j++)
            ld_echo_char(ld->canon_buf[j], echo_out, echo_cap, echo_len);
        }
        continue;
      }

      if(c == (char)tp->c_cc[VKILL]) {
        ld->canon_len = 0;
        if(tp->c_lflag & ECHOK)
          ld_echo_char('\n', echo_out, echo_cap, echo_len);
        continue;
      }

      if(c == (char)tp->c_cc[VEOF]) {
        int j;
        for(j = 0; j < ld->canon_len && out_len < slave_cap; j++)
          slave_out[out_len++] = ld->canon_buf[j];
        ld->canon_len = 0;
        continue;
      }

      if(ld_store_canon(ld, c) == 0) {
        if((tp->c_lflag & ECHO) || (c == '\n' && (tp->c_lflag & ECHONL)))
          ld_echo_char(c, echo_out, echo_cap, echo_len);
      }

      if(c == '\n' || c == (char)tp->c_cc[VEOL] || c == (char)tp->c_cc[VEOL2]) {
        int j;
        for(j = 0; j < ld->canon_len && out_len < slave_cap; j++)
          slave_out[out_len++] = ld->canon_buf[j];
        ld->canon_len = 0;
      }
      continue;
    }

    if(out_len < slave_cap)
      slave_out[out_len++] = c;
    if(tp->c_lflag & ECHO)
      ld_echo_char(c, echo_out, echo_cap, echo_len);
  }

  return out_len;
}

int
tty_ldisc_process_output(const struct termios *tp,
                         struct tty_ldisc_state *ld,
                         const char *in,
                         int in_len,
                         char *master_out,
                         int master_cap)
{
  int i;
  int out_len;

  if(tp == 0 || ld == 0 || in == 0 || in_len < 0 || master_out == 0 || master_cap < 0)
    return -1;

  out_len = 0;
  for(i = 0; i < in_len; i++) {
    char c;
    c = in[i];

    if(tp->c_oflag & OPOST) {
      if(c == '\r' && (tp->c_oflag & OCRNL))
        c = '\n';

      if(c == '\r' && (tp->c_oflag & ONOCR) && ld->out_col == 0)
        continue;

      if(c == '\n' && (tp->c_oflag & ONLCR)) {
        if(out_len + 2 <= master_cap) {
          master_out[out_len++] = '\r';
          master_out[out_len++] = '\n';
        }
        ld->out_col = 0;
        continue;
      }
    }

    if(out_len < master_cap)
      master_out[out_len++] = c;

    if(c == '\r') {
      ld->out_col = 0;
    } else if(c == '\n') {
      if(tp->c_oflag & ONLRET)
        ld->out_col = 0;
    } else {
      ld->out_col++;
    }
  }

  return out_len;
}