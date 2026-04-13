#include "types.h"
#include "auxv6/user.h"
#include "termios.h"

struct probe_cfg {
  int mouse_mode;
  int mouse_encoding;
  int focus;
  int bracketed_paste;
  int query;
  int decode;
  int selftest;
  int focus_test;
  int paste_test;
  int raw;
};

struct seq_state {
  char buf[128];
  int len;
  int in_esc;
};

static void
usage(void)
{
  dprintf(2,
          "usage: xtermprobe [-m 1000|1002|1003] [-e normal|utf8|sgr|urxvt] [-f] [-q] [-d] [-r] [-t] [-T] [-P]\n");
}

static int
streq(const char *a, const char *b)
{
  return strcmp(a, b) == 0;
}

static void
enable_probe_modes(const struct probe_cfg *cfg)
{
  dprintf(1, "\033[?1000l\033[?1002l\033[?1003l\033[?1004l\033[?1005l\033[?1006l\033[?1015l\033[?2004l");

  if(cfg->mouse_mode == 1000 || cfg->mouse_mode == 1002 || cfg->mouse_mode == 1003)
    dprintf(1, "\033[?%dh", cfg->mouse_mode);

  if(cfg->focus)
    dprintf(1, "\033[?1004h");

  if(cfg->bracketed_paste)
    dprintf(1, "\033[?2004h");

  if(cfg->mouse_encoding == 1005 || cfg->mouse_encoding == 1006 || cfg->mouse_encoding == 1015)
    dprintf(1, "\033[?%dh", cfg->mouse_encoding);
}

static void
disable_probe_modes(void)
{
  dprintf(1, "\033[?1000l\033[?1002l\033[?1003l\033[?1004l\033[?1005l\033[?1006l\033[?1015l\033[?2004l");
}

static void
emit_queries(void)
{
  dprintf(1, "\033[c");   /* DA */
  dprintf(1, "\033[>c");  /* Secondary DA */
  dprintf(1, "\033[6n");  /* CPR */
}

static void
print_byte(uchar b)
{
  if(b >= 32 && b <= 126)
    dprintf(1, "0x%02x '%c'\n", b, b);
  else
    dprintf(1, "0x%02x\n", b);
}

static int
parse_uint(const char *s, int n)
{
  char tmp[32];

  if(n <= 0)
    return 0;
  if(n >= (int)sizeof(tmp))
    n = (int)sizeof(tmp) - 1;
  memmove(tmp, s, n);
  tmp[n] = 0;
  return atoi(tmp);
}

static int
contains_token(const char *buf, int n, const char *tok)
{
  int i;
  int j;
  int tlen;

  if(!buf || !tok)
    return 0;
  tlen = strlen(tok);
  if(tlen <= 0 || n < tlen)
    return 0;

  for(i = 0; i <= n - tlen; i++) {
    for(j = 0; j < tlen; j++) {
      if(buf[i + j] != tok[j])
        break;
    }
    if(j == tlen)
      return 1;
  }
  return 0;
}

static int
read_reply_contains(int fd, const char *tok, char *buf, int buflen, int max_reads)
{
  int used;
  int i;

  used = 0;
  if(buf && buflen > 0)
    buf[0] = 0;

  for(i = 0; i < max_reads; i++) {
    char ch;
    int n;

    n = read(fd, &ch, 1);
    if(n < 0)
      return -1;
    if(n == 0)
      continue;

    if(buf && used < buflen - 1) {
      buf[used++] = ch;
      buf[used] = 0;
      if(tok && contains_token(buf, used, tok))
        return 1;
    }
  }

  if(tok == 0)
    return 1;
  return 0;
}

static void
drain_input(int fd)
{
  char ch;
  int i;

  for(i = 0; i < 64; i++) {
    int n = read(fd, &ch, 1);
    if(n <= 0)
      break;
  }
}

static int
run_selftest(int fd)
{
  char buf[256];
  int fails;

  fails = 0;
  dprintf(1, "selftest: DA/CPR + private mode query checks (1000/1002/1003/1004/1005/1006/1015/2004)\n");

  drain_input(fd);

  write(1, "\033[c", 3);
  if(read_reply_contains(fd, "[?1;0c", buf, sizeof(buf), 256) == 1)
    dprintf(1, "ok  da-primary\n");
  else {
    dprintf(1, "fail da-primary\n");
    fails++;
  }

  write(1, "\033[>c", 4);
  if(read_reply_contains(fd, "[>0;0;0c", buf, sizeof(buf), 256) == 1)
    dprintf(1, "ok  da-secondary\n");
  else {
    dprintf(1, "fail da-secondary\n");
    fails++;
  }

  write(1, "\033[2;3H", 6);
  write(1, "\033[6n", 4);
  if(read_reply_contains(fd, "[2;3R", buf, sizeof(buf), 256) == 1)
    dprintf(1, "ok  cpr\n");
  else {
    dprintf(1, "fail cpr\n");
    fails++;
  }

  write(1, "\033[?1000l\033[?1002l\033[?1003l", 24);

  write(1, "\033[?1004l", 8);
  write(1, "\033[?1004$p", 9);
  if(read_reply_contains(fd, "?1004;2$y", buf, sizeof(buf), 256) == 1)
    dprintf(1, "ok  mode-1004-off\n");
  else {
    dprintf(1, "fail mode-1004-off\n");
    fails++;
  }
  write(1, "\033[?1004h", 8);
  write(1, "\033[?1004$p", 9);
  if(read_reply_contains(fd, "?1004;1$y", buf, sizeof(buf), 256) == 1)
    dprintf(1, "ok  mode-1004-on\n");
  else {
    dprintf(1, "fail mode-1004-on\n");
    fails++;
  }
  write(1, "\033[?1004l", 8);

  write(1, "\033[?1000$p", 9);
  if(read_reply_contains(fd, "?1000;2$y", buf, sizeof(buf), 256) == 1)
    dprintf(1, "ok  mode-1000-off\n");
  else {
    dprintf(1, "fail mode-1000-off\n");
    fails++;
  }
  write(1, "\033[?1002$p", 9);
  if(read_reply_contains(fd, "?1002;2$y", buf, sizeof(buf), 256) == 1)
    dprintf(1, "ok  mode-1002-off\n");
  else {
    dprintf(1, "fail mode-1002-off\n");
    fails++;
  }
  write(1, "\033[?1003$p", 9);
  if(read_reply_contains(fd, "?1003;2$y", buf, sizeof(buf), 256) == 1)
    dprintf(1, "ok  mode-1003-off\n");
  else {
    dprintf(1, "fail mode-1003-off\n");
    fails++;
  }

  write(1, "\033[?1000h\033[?1002h\033[?1003h", 24);
  write(1, "\033[?1000$p", 9);
  if(read_reply_contains(fd, "?1000;1$y", buf, sizeof(buf), 256) == 1)
    dprintf(1, "ok  mode-1000-on\n");
  else {
    dprintf(1, "fail mode-1000-on\n");
    fails++;
  }
  write(1, "\033[?1002$p", 9);
  if(read_reply_contains(fd, "?1002;1$y", buf, sizeof(buf), 256) == 1)
    dprintf(1, "ok  mode-1002-on\n");
  else {
    dprintf(1, "fail mode-1002-on\n");
    fails++;
  }
  write(1, "\033[?1003$p", 9);
  if(read_reply_contains(fd, "?1003;1$y", buf, sizeof(buf), 256) == 1)
    dprintf(1, "ok  mode-1003-on\n");
  else {
    dprintf(1, "fail mode-1003-on\n");
    fails++;
  }

  write(1, "\033[?1000l\033[?1002l\033[?1003l", 24);

  write(1, "\033[?1005l\033[?1006l", 16);
  write(1, "\033[?1005$p", 9);
  if(read_reply_contains(fd, "?1005;2$y", buf, sizeof(buf), 256) == 1)
    dprintf(1, "ok  mode-1005-off\n");
  else {
    dprintf(1, "fail mode-1005-off\n");
    fails++;
  }
  write(1, "\033[?1006$p", 9);
  if(read_reply_contains(fd, "?1006;2$y", buf, sizeof(buf), 256) == 1)
    dprintf(1, "ok  mode-1006-off\n");
  else {
    dprintf(1, "fail mode-1006-off\n");
    fails++;
  }

  write(1, "\033[?1005h", 8);
  write(1, "\033[?1005$p", 9);
  if(read_reply_contains(fd, "?1005;1$y", buf, sizeof(buf), 256) == 1)
    dprintf(1, "ok  mode-1005-on\n");
  else {
    dprintf(1, "fail mode-1005-on\n");
    fails++;
  }
  write(1, "\033[?1006$p", 9);
  if(read_reply_contains(fd, "?1006;2$y", buf, sizeof(buf), 256) == 1)
    dprintf(1, "ok  mode-1006-still-off\n");
  else {
    dprintf(1, "fail mode-1006-still-off\n");
    fails++;
  }

  write(1, "\033[?1006h", 8);
  write(1, "\033[?1006$p", 9);
  if(read_reply_contains(fd, "?1006;1$y", buf, sizeof(buf), 256) == 1)
    dprintf(1, "ok  mode-1006-on\n");
  else {
    dprintf(1, "fail mode-1006-on\n");
    fails++;
  }
  write(1, "\033[?1005$p", 9);
  if(read_reply_contains(fd, "?1005;1$y", buf, sizeof(buf), 256) == 1)
    dprintf(1, "ok  mode-1005-still-on\n");
  else {
    dprintf(1, "fail mode-1005-still-on\n");
    fails++;
  }

  write(1, "\033[?1015l", 8);
  write(1, "\033[?1015$p", 9);
  if(read_reply_contains(fd, "?1015;2$y", buf, sizeof(buf), 256) == 1)
    dprintf(1, "ok  mode-1015-off\n");
  else {
    dprintf(1, "fail mode-1015-off\n");
    fails++;
  }
  write(1, "\033[?1015h", 8);
  write(1, "\033[?1015$p", 9);
  if(read_reply_contains(fd, "?1015;1$y", buf, sizeof(buf), 256) == 1)
    dprintf(1, "ok  mode-1015-on\n");
  else {
    dprintf(1, "fail mode-1015-on\n");
    fails++;
  }
  write(1, "\033[?1006$p", 9);
  if(read_reply_contains(fd, "?1006;1$y", buf, sizeof(buf), 256) == 1)
    dprintf(1, "ok  mode-1006-still-on\n");
  else {
    dprintf(1, "fail mode-1006-still-on\n");
    fails++;
  }
  write(1, "\033[?1015l", 8);

  write(1, "\033[?2004l", 8);
  write(1, "\033[?2004$p", 9);
  if(read_reply_contains(fd, "?2004;2$y", buf, sizeof(buf), 256) == 1)
    dprintf(1, "ok  mode-2004-off\n");
  else {
    dprintf(1, "fail mode-2004-off\n");
    fails++;
  }
  write(1, "\033[?2004h", 8);
  write(1, "\033[?2004$p", 9);
  if(read_reply_contains(fd, "?2004;1$y", buf, sizeof(buf), 256) == 1)
    dprintf(1, "ok  mode-2004-on\n");
  else {
    dprintf(1, "fail mode-2004-on\n");
    fails++;
  }
  write(1, "\033[?2004l", 8);

  write(1, "\033[?1005l\033[?1006l\033[?1015l", 24);
  dprintf(1, "selftest: %s (%d fail)\n", fails ? "FAIL" : "PASS", fails);
  return fails ? 1 : 0;
}

static int
run_focus_switch_test(int fd)
{
  char ch;
  int esc_state;
  int seen_in;
  int seen_out;
  int idle_ticks;

  esc_state = 0;
  seen_in = 0;
  seen_out = 0;
  idle_ticks = 0;

  dprintf(1, "focus-test: enabled mode 1004\n");
  dprintf(1, "focus-test: switch away (for example F2), then back (for example F1)\n");
  dprintf(1, "focus-test: press q to abort\n");

  drain_input(fd);

  while(idle_ticks < 1200) {
    int n;

    n = read(fd, &ch, 1);
    if(n < 0)
      return 1;
    if(n == 0) {
      idle_ticks++;
      continue;
    }

    idle_ticks = 0;
    if((uchar)ch == 'q') {
      dprintf(1, "focus-test: ABORT\n");
      return 2;
    }

    if(esc_state == 0) {
      if((uchar)ch == 0x1B)
        esc_state = 1;
      continue;
    }
    if(esc_state == 1) {
      if(ch == '[')
        esc_state = 2;
      else
        esc_state = 0;
      continue;
    }

    if(ch == 'I') {
      if(!seen_in)
        dprintf(1, "ok  focus-in (CSI I)\n");
      seen_in = 1;
    } else if(ch == 'O') {
      if(!seen_out)
        dprintf(1, "ok  focus-out (CSI O)\n");
      seen_out = 1;
    }
    esc_state = 0;

    if(seen_in && seen_out) {
      dprintf(1, "focus-test: PASS\n");
      return 0;
    }
  }

  if(!seen_out)
    dprintf(1, "fail focus-out (CSI O)\n");
  if(!seen_in)
    dprintf(1, "fail focus-in (CSI I)\n");
  dprintf(1, "focus-test: FAIL\n");
  return 1;
}

static int
run_bracketed_paste_test(int fd)
{
  static const char start_seq[] = "\033[200~";
  static const char end_seq[] = "\033[201~";
  char ch;
  int idle_ticks;
  int start_pos;
  int end_pos;
  int in_paste;
  int payload;
  int notified;

  idle_ticks = 0;
  start_pos = 0;
  end_pos = 0;
  in_paste = 0;
  payload = 0;
  notified = 0;

  dprintf(1, "paste-test: enabled mode 2004\n");
  dprintf(1, "paste-test: paste any text block; press q or ESC ESC to abort\n");
  dprintf(1, "paste-test: requires an outer terminal that wraps paste in CSI 200~/201~\n");

  drain_input(fd);

  while(idle_ticks < 300) {
    int n;
    int recheck;

    n = read(fd, &ch, 1);
    if(n < 0)
      return 1;
    if(n == 0) {
      idle_ticks++;
      if(idle_ticks == 50 && !in_paste)
        dprintf(1, "paste-test: still waiting for CSI 200~ ...\n");
      if(idle_ticks == 150 && in_paste)
        dprintf(1, "paste-test: still waiting for CSI 201~ (press q to abort)\n");
      continue;
    }

    idle_ticks = 0;

    /* q or ESC ESC abort at any time */
    if((uchar)ch == 'q') {
      dprintf(1, "paste-test: ABORT\n");
      return 2;
    }
    if((uchar)ch == 0x1B && !in_paste && notified == 0) {
      notified = 1;  /* first ESC, track for double-ESC abort */
    } else if((uchar)ch == 0x1B && notified == 1 && !in_paste) {
      dprintf(1, "paste-test: ABORT (ESC ESC)\n");
      return 2;
    } else {
      notified = 0;
    }

    if(!in_paste) {
      if(ch == start_seq[start_pos]) {
        start_pos++;
        if(start_seq[start_pos] == 0) {
          in_paste = 1;
          start_pos = 0;
          dprintf(1, "ok  paste-start (CSI 200~)\n");
          dprintf(1, "paste-test: receiving paste content; press q to abort\n");
        }
      } else {
        start_pos = (ch == start_seq[0]) ? 1 : 0;
      }
      continue;
    }

    /* Abort via q is also allowed inside paste */
    recheck = 0;
    if(ch == end_seq[end_pos]) {
      end_pos++;
      if(end_seq[end_pos] == 0) {
        dprintf(1, "ok  paste-end (CSI 201~)\n");
        if(payload > 0)
          dprintf(1, "ok  paste-payload (%d bytes)\n", payload);
        else
          dprintf(1, "fail paste-payload (0 bytes, empty paste?)\n");
        dprintf(1, "paste-test: %s\n", payload > 0 ? "PASS" : "FAIL");
        return payload > 0 ? 0 : 1;
      }
      continue;
    }

    /* Mismatch: flush partial match as payload, then re-check current char */
    if(end_pos > 0) {
      int i;
      for(i = 0; i < end_pos; i++)
        payload++;
      end_pos = 0;
      recheck = 1;
    }

    /* Re-check current char as potential start of end_seq */
    if(recheck && ch == end_seq[0]) {
      end_pos = 1;
      continue;
    }

    payload++;
  }

  if(!in_paste)
    dprintf(1, "fail paste-start (CSI 200~) -- no outer terminal with mode 2004?\n");
  else
    dprintf(1, "fail paste-end (CSI 201~) -- end marker never arrived\n");
  dprintf(1, "paste-test: FAIL\n");
  return 1;
}

static int
decode_utf8_cp(const char *s, int n, int *consumed)
{
  uchar b0;

  if(consumed)
    *consumed = 0;
  if(n <= 0)
    return -1;

  b0 = (uchar)s[0];
  if(b0 < 0x80) {
    if(consumed)
      *consumed = 1;
    return b0;
  }
  if((b0 & 0xE0) == 0xC0) {
    uchar b1;
    if(n < 2)
      return -1;
    b1 = (uchar)s[1];
    if((b1 & 0xC0) != 0x80)
      return -1;
    if(consumed)
      *consumed = 2;
    return ((int)(b0 & 0x1F) << 6) | (int)(b1 & 0x3F);
  }
  if((b0 & 0xF0) == 0xE0) {
    uchar b1;
    uchar b2;
    if(n < 3)
      return -1;
    b1 = (uchar)s[1];
    b2 = (uchar)s[2];
    if((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80)
      return -1;
    if(consumed)
      *consumed = 3;
    return ((int)(b0 & 0x0F) << 12) | ((int)(b1 & 0x3F) << 6) | (int)(b2 & 0x3F);
  }
  return -1;
}

static const char *
mouse_button_name(int btn)
{
  if(btn == 0)
    return "left";
  if(btn == 1)
    return "middle";
  if(btn == 2)
    return "right";
  if(btn == 64)
    return "wheel-up";
  if(btn == 65)
    return "wheel-down";
  return "other";
}

static void
print_mouse_mods(int cb)
{
  if(cb & 4)
    dprintf(1, " shift");
  if(cb & 8)
    dprintf(1, " alt");
  if(cb & 16)
    dprintf(1, " ctrl");
}

static void
decode_mouse_cbxy(const char *label, int cb, int x, int y, int release)
{
  int motion;
  int wheel;
  int btn;

  motion = (cb & 32) ? 1 : 0;
  wheel = (cb & 64) ? 1 : 0;
  btn = cb & 3;

  dprintf(1, "decode: %s cb=%d x=%d y=%d", label, cb, x, y);
  if(release)
    dprintf(1, " release");
  else if(motion)
    dprintf(1, " motion");
  else
    dprintf(1, " press");

  if(wheel) {
    dprintf(1, " button=%s", mouse_button_name((cb & 1) ? 65 : 64));
  } else if(release || btn == 3) {
    dprintf(1, " button=release");
  } else {
    dprintf(1, " button=%s", mouse_button_name(btn));
  }
  print_mouse_mods(cb);
  dprintf(1, "\n");
}

static int
parse_csi_numeric_triplet(const char *s, int n, int vals[3])
{
  int i;
  int start;
  int count;

  start = 0;
  count = 0;
  for(i = 0; i < n; i++) {
    if(s[i] == ';') {
      if(count < 3)
        vals[count++] = parse_uint(s + start, i - start);
      start = i + 1;
    }
  }
  if(count < 3 && start <= n)
    vals[count++] = parse_uint(s + start, n - start);
  return count;
}

static void
decode_csi_sequence(const char *seq, int len)
{
  char final;

  if(len < 3)
    return;
  final = seq[len - 1];

  if(final == 'A' || final == 'B' || final == 'C' || final == 'D') {
    int p1;
    int p2;
    int vals[3];
    int nvals;

    p1 = 1;
    p2 = 1;
    nvals = parse_csi_numeric_triplet(seq + 2, len - 3, vals);
    if(nvals >= 1 && vals[0] > 0)
      p1 = vals[0];
    if(nvals >= 2 && vals[1] > 0)
      p2 = vals[1];
    dprintf(1, "decode: CSI cursor key final=%c count=%d mod=%d", final, p1, p2);
    if(p2 > 1) {
      if(p2 & 1)
        dprintf(1, "");
      if((p2 - 1) & 1)
        dprintf(1, " shift");
      if((p2 - 1) & 2)
        dprintf(1, " alt");
      if((p2 - 1) & 4)
        dprintf(1, " ctrl");
    }
    dprintf(1, "\n");
    return;
  }
  if(final == 'H' || final == 'F') {
    dprintf(1, "decode: CSI home/end final=%c\n", final);
    return;
  }
  if(final == 'R') {
    dprintf(1, "decode: CPR reply\n");
    return;
  }
  if(final == 'c') {
    dprintf(1, "decode: DA reply\n");
    return;
  }
  if(final == '~') {
    int vals[3];
    int nvals;
    int key;
    int mod;

    nvals = parse_csi_numeric_triplet(seq + 2, len - 3, vals);
    key = (nvals >= 1) ? vals[0] : 0;
    mod = (nvals >= 2 && vals[1] > 0) ? vals[1] : 1;
    dprintf(1, "decode: CSI tilde key=%d mod=%d", key, mod);
    if(mod > 1) {
      if((mod - 1) & 1)
        dprintf(1, " shift");
      if((mod - 1) & 2)
        dprintf(1, " alt");
      if((mod - 1) & 4)
        dprintf(1, " ctrl");
    }
    dprintf(1, "\n");
    return;
  }
  if(final == 'I') {
    dprintf(1, "decode: focus in\n");
    return;
  }
  if(final == 'O') {
    dprintf(1, "decode: focus out\n");
    return;
  }

  if(final == 'M' && len >= 6 && seq[2] == '<') {
    int vals[3];
    int nvals;

    nvals = parse_csi_numeric_triplet(seq + 3, len - 4, vals);
    if(nvals == 3)
      decode_mouse_cbxy("SGR mouse", vals[0], vals[1], vals[2], 0);
    else
      dprintf(1, "decode: SGR mouse press/motion\n");
    return;
  }

  if(final == 'm' && len >= 6 && seq[2] == '<') {
    int vals[3];
    int nvals;

    nvals = parse_csi_numeric_triplet(seq + 3, len - 4, vals);
    if(nvals == 3)
      decode_mouse_cbxy("SGR mouse", vals[0], vals[1], vals[2], 1);
    else
      dprintf(1, "decode: SGR mouse release\n");
    return;
  }

  if(final == 'M' && seq[2] >= '0' && seq[2] <= '9') {
    int vals[3];
    int nvals;

    nvals = parse_csi_numeric_triplet(seq + 2, len - 3, vals);
    if(nvals == 3)
      decode_mouse_cbxy("urxvt mouse", vals[0], vals[1], vals[2], 0);
    else
      dprintf(1, "decode: urxvt mouse\n");
    return;
  }

  if(final == 'M' && len >= 6 && seq[2] != '<') {
    int cb;
    int x;
    int y;
    int consumed;
    int off;

    off = 3;
    cb = decode_utf8_cp(seq + off, len - off, &consumed);
    if(cb < 0)
      return;
    off += consumed;
    x = decode_utf8_cp(seq + off, len - off, &consumed);
    if(x < 0)
      return;
    off += consumed;
    y = decode_utf8_cp(seq + off, len - off, &consumed);
    if(y < 0)
      return;

    cb -= 32;
    x -= 32;
    y -= 32;
    decode_mouse_cbxy("X10/UTF8 mouse", cb, x, y, 0);
    return;
  }

  if(final == 'M' && seq[2] != '<') {
    dprintf(1, "decode: X10/UTF8 mouse sequence (truncated)\n");
    return;
  }

  dprintf(1, "decode: CSI sequence final=%c\n", final);
}

static void
decode_sequence(const char *seq, int len)
{
  if(len < 2 || seq[0] != '\033')
    return;

  if(seq[1] == '[') {
    decode_csi_sequence(seq, len);
    return;
  }
  if(seq[1] == 'O') {
    if(len >= 3)
      dprintf(1, "decode: SS3 key final=%c\n", seq[2]);
    else
      dprintf(1, "decode: SS3 sequence\n");
    return;
  }
  if(seq[1] == ']') {
    dprintf(1, "decode: OSC sequence\n");
    return;
  }
  dprintf(1, "decode: ESC sequence\n");
}

static void
seq_flush(struct seq_state *st, int decode)
{
  if(!st)
    return;
  if(decode && st->len > 0)
    decode_sequence(st->buf, st->len);
  st->len = 0;
  st->in_esc = 0;
}

static void
seq_push_byte(struct seq_state *st, uchar b, int decode)
{
  if(!st)
    return;

  if(!st->in_esc) {
    if(b == 0x1B) {
      st->in_esc = 1;
      st->len = 0;
      st->buf[st->len++] = (char)b;
    }
    return;
  }

  if(st->len < (int)sizeof(st->buf) - 1)
    st->buf[st->len++] = (char)b;

  if(st->len >= 2 && st->buf[1] == '[') {
    if(st->len >= 3 && st->buf[2] == 'M' && st->buf[1] == '[') {
      int off;
      int consumed;
      int cp_count;

      off = 3;
      cp_count = 0;
      while(off < st->len && cp_count < 3) {
        int cp = decode_utf8_cp(st->buf + off, st->len - off, &consumed);
        if(cp < 0 || consumed <= 0)
          break;
        cp_count++;
        off += consumed;
      }
      if(cp_count >= 3) {
        seq_flush(st, decode);
        return;
      }
    } else if((b >= '@' && b <= '~') || b == 'm') {
      seq_flush(st, decode);
      return;
    }
  } else if(st->len >= 2 && st->buf[1] == 'O') {
    if(st->len >= 3) {
      seq_flush(st, decode);
      return;
    }
  } else if(st->len >= 2 && st->buf[1] == ']') {
    if(b == 0x07) {
      seq_flush(st, decode);
      return;
    }
    if(st->len >= 3 && st->buf[st->len - 2] == 0x1B && b == '\\') {
      seq_flush(st, decode);
      return;
    }
  } else if(st->len >= 2) {
    seq_flush(st, decode);
    return;
  }

  if(st->len >= (int)sizeof(st->buf) - 1)
    seq_flush(st, decode);
}

int
main(int argc, char **argv)
{
  struct probe_cfg cfg;
  struct termios oldt;
  struct termios raw;
  struct seq_state st;
  char ch;
  int i;
  int n;

  cfg.mouse_mode = 1003;
  cfg.mouse_encoding = 1006;
  cfg.focus = 0;
  cfg.bracketed_paste = 0;
  cfg.query = 0;
  cfg.decode = 0;
  cfg.selftest = 0;
  cfg.focus_test = 0;
  cfg.paste_test = 0;
  cfg.raw = 1;

  memset(&st, 0, sizeof(st));

  for(i = 1; i < argc; i++) {
    if(streq(argv[i], "-m")) {
      if(i + 1 >= argc) {
        usage();
        exit(1);
      }
      i++;
      if(streq(argv[i], "1000"))
        cfg.mouse_mode = 1000;
      else if(streq(argv[i], "1002"))
        cfg.mouse_mode = 1002;
      else if(streq(argv[i], "1003"))
        cfg.mouse_mode = 1003;
      else {
        usage();
        exit(1);
      }
    } else if(streq(argv[i], "-e")) {
      if(i + 1 >= argc) {
        usage();
        exit(1);
      }
      i++;
      if(streq(argv[i], "normal"))
        cfg.mouse_encoding = 0;
      else if(streq(argv[i], "utf8"))
        cfg.mouse_encoding = 1005;
      else if(streq(argv[i], "sgr"))
        cfg.mouse_encoding = 1006;
      else if(streq(argv[i], "urxvt"))
        cfg.mouse_encoding = 1015;
      else {
        usage();
        exit(1);
      }
    } else if(streq(argv[i], "-f")) {
      cfg.focus = 1;
    } else if(streq(argv[i], "-q")) {
      cfg.query = 1;
    } else if(streq(argv[i], "-d")) {
      cfg.decode = 1;
      cfg.raw = 0;
    } else if(streq(argv[i], "-r")) {
      cfg.raw = 1;
    } else if(streq(argv[i], "-t")) {
      cfg.selftest = 1;
      cfg.decode = 0;
      cfg.raw = 0;
    } else if(streq(argv[i], "-T")) {
      cfg.focus_test = 1;
      cfg.focus = 1;
      cfg.decode = 0;
      cfg.raw = 0;
    } else if(streq(argv[i], "-P")) {
      cfg.paste_test = 1;
      cfg.bracketed_paste = 1;
      cfg.decode = 0;
      cfg.raw = 0;
    } else if(streq(argv[i], "-h") || streq(argv[i], "--help")) {
      usage();
      exit(0);
    } else {
      usage();
      exit(1);
    }
  }

  if((cfg.selftest && cfg.focus_test) ||
     (cfg.selftest && cfg.paste_test) ||
     (cfg.focus_test && cfg.paste_test)) {
    usage();
    exit(1);
  }

  if(!isatty(0) || !isatty(1)) {
    dprintf(2, "xtermprobe: stdin/stdout must be tty\n");
    exit(1);
  }

  if(tcgetattr(0, &oldt) < 0) {
    dprintf(2, "xtermprobe: tcgetattr failed\n");
    exit(1);
  }

  raw = oldt;
  raw.c_iflag &= ~(IXON | ICRNL | INLCR);
  raw.c_oflag &= ~(OPOST);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  if(tcsetattr(0, TCSAFLUSH, &raw) < 0) {
    dprintf(2, "xtermprobe: tcsetattr failed\n");
    exit(1);
  }
  if(cfg.selftest) {
    int rc;

    dprintf(1, "xtermprobe selftest\n");
    enable_probe_modes(&cfg);
    rc = run_selftest(0);
    disable_probe_modes();
    tcsetattr(0, TCSAFLUSH, &oldt);
    exit(rc);
  }

  if(cfg.focus_test) {
    int rc;

    dprintf(1, "xtermprobe focus-switch test\n");
    enable_probe_modes(&cfg);
    rc = run_focus_switch_test(0);
    disable_probe_modes();
    tcsetattr(0, TCSAFLUSH, &oldt);
    exit(rc);
  }

  if(cfg.paste_test) {
    int rc;

    dprintf(1, "xtermprobe bracketed-paste test\n");
    enable_probe_modes(&cfg);
    rc = run_bracketed_paste_test(0);
    disable_probe_modes();
    tcsetattr(0, TCSAFLUSH, &oldt);
    exit(rc);
  }

  dprintf(1, "\033[2J\033[H");
  dprintf(1, "xtermprobe: press keys/click mouse; press 'q' to quit\n");
  dprintf(1, "tracking=%d encoding=%d focus=%d queries=%d decode=%d raw=%d selftest=%d\n",
          cfg.mouse_mode, cfg.mouse_encoding, cfg.focus, cfg.query,
          cfg.decode, cfg.raw, cfg.selftest);

  enable_probe_modes(&cfg);

  if(cfg.query)
    emit_queries();

  for(;;) {
    n = read(0, &ch, 1);
    if(n < 0)
      break;
    if(n == 0)
      continue;
    if((uchar)ch == 'q')
      break;
    if(cfg.raw)
      print_byte((uchar)ch);
    seq_push_byte(&st, (uchar)ch, cfg.decode);
  }

  seq_flush(&st, cfg.decode);
  disable_probe_modes();
  tcsetattr(0, TCSAFLUSH, &oldt);
  dprintf(1, "\nexit\n");
  exit(0);
}
