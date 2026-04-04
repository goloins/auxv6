/*
 * top.c — interactive process monitor for auxv6
 *
 * Displays a periodically-refreshed process table sorted by CPU usage,
 * together with a system summary (uptime, load averages, memory).
 *
 * Data sources:
 *   /proc/ps       — process snapshot (PID PPID PGID SID TTY UID GID STAT SZ CTICKS NAME)
 *   /proc/meminfo  — MemTotal / MemFree in kB
 *   /proc/loadavg  — "1.23 4.56 7.89 run/tot\n" (Linux-compatible)
 *   uptime()       — kernel syscall returning elapsed ticks (100Hz)
 *
 * CPU % is computed by sampling cticks for each process across two snapshots
 * separated by the refresh interval and dividing the delta by elapsed ticks
 * (accounting for all CPUs: we report per-process % of one CPU, matching
 * the convention of top/htop).
 *
 * Keyboard bindings while running:
 *   q / Q    — quit
 *   r / R    — force immediate refresh
 *   Space    — force immediate refresh
 *   h / ?    — toggle help overlay
 *
 * Author: auxv6 project
 */

#include "types.h"
#include "auxv6/user.h"
#include "fcntl.h"
#include "pwd.h"
#include "signal.h"
#include "stdio.h"
#include "libterm.h"

/* -----------------------------------------------------------------------
 * Constants
 * --------------------------------------------------------------------- */

#define MAX_PROCS        128          /* matches kernel NPROC               */
#define REFRESH_TICKS    300          /* default refresh interval (3s@100Hz)*/
#define HEADER_LINES     5            /* rows consumed by the summary header */
#define PS_BUF_SIZE      8192         /* buffer for /proc/ps read           */
#define MEMINFO_BUF      256
#define LAVG_BUF         128

/* uptime() returns ticks; system timer runs at 100Hz */
#define HZ               100

/*
 * Process record as parsed from /proc/ps.
 * We keep two full tables so we can diff cticks across refresh cycles.
 */
struct proc_rec {
  int  pid;
  int  ppid;
  int  uid;
  int  gid;
  int  tty;
  int  state;          /* encoded STAT character index                     */
  unsigned int sz;     /* virtual size in bytes                            */
  unsigned int cticks; /* cumulative CPU ticks (100Hz)                     */
  char stat[12];       /* state string from /proc/ps                       */
  char name[16];       /* process name                                     */
};

/*
 * Internal display record — carries derived fields for rendering.
 */
struct disp_rec {
  struct proc_rec snap;   /* current snapshot                             */
  unsigned int    prev_cticks; /* cticks from previous snapshot           */
  int             cpu_pct_x10; /* CPU% * 10, e.g. 123 = 12.3%            */
  int             present;     /* seen in latest snapshot                 */
};

/* -----------------------------------------------------------------------
 * Globals
 * --------------------------------------------------------------------- */

static struct termstate ts;

/* The display table is maintained across frames to track cpu deltas. */
static struct disp_rec  dtable[MAX_PROCS];
static int              dtable_n;

/* Scratch buffer for reading /proc/ps */
static char ps_buf[PS_BUF_SIZE];

/* Signal flags */
static volatile int g_winch;   /* SIGWINCH received — resize pending     */
static volatile int g_quit;    /* SIGTERM/SIGINT received — exit cleanly */

/* -----------------------------------------------------------------------
 * Signal handlers
 * --------------------------------------------------------------------- */

static void
handle_winch(int sig)
{
  (void)sig;
  g_winch = 1;
}

static void
handle_quit(int sig)
{
  (void)sig;
  g_quit = 1;
}

/* -----------------------------------------------------------------------
 * /proc file helpers
 * --------------------------------------------------------------------- */

static int
read_file(const char *path, char *buf, int maxlen)
{
  int fd, n, off;

  fd = open(path, O_RDONLY);
  if(fd < 0) return -1;

  off = 0;
  while(off < maxlen - 1){
    n = read(fd, buf + off, maxlen - 1 - off);
    if(n <= 0) break;
    off += n;
  }
  buf[off] = '\0';
  close(fd);
  return off;
}

/* -----------------------------------------------------------------------
 * /proc/ps parsing
 * --------------------------------------------------------------------- */

/*
 * Skip whitespace, return pointer past it.
 */
static const char *
skip_ws(const char *p)
{
  while(*p == ' ' || *p == '\t') p++;
  return p;
}

/*
 * Parse a decimal integer starting at p.  Advances *pp past the number.
 */
static int
parse_int(const char **pp)
{
  const char *p = *pp;
  int sign = 1, v = 0;
  if(*p == '-'){ sign = -1; p++; }
  while(*p >= '0' && *p <= '9')
    v = v * 10 + (*p++ - '0');
  *pp = p;
  return sign * v;
}

/*
 * Parse an unsigned integer starting at p.
 */
static unsigned int
parse_uint(const char **pp)
{
  const char *p = *pp;
  unsigned int v = 0;
  while(*p >= '0' && *p <= '9')
    v = v * 10 + (unsigned int)(*p++ - '0');
  *pp = p;
  return v;
}

/*
 * Copy a whitespace-delimited token into dst (max dstlen bytes including NUL).
 * Advances *pp past the token.
 */
static void
parse_tok(const char **pp, char *dst, int dstlen)
{
  const char *p = *pp;
  int i = 0;
  while(*p && *p != ' ' && *p != '\t' && *p != '\n' && i < dstlen - 1)
    dst[i++] = *p++;
  dst[i] = '\0';
  *pp = p;
}

/*
 * Parse /proc/ps into precs[].  Returns number of processes parsed.
 * Expected column order: PID PPID PGID SID TTY UID GID STAT SZ CTICKS NAME
 */
static int
parse_ps(const char *buf, struct proc_rec *precs, int maxprecs)
{
  const char *p = buf;
  int n = 0;

  /* Skip header line */
  while(*p && *p != '\n') p++;
  if(*p == '\n') p++;

  while(*p && n < maxprecs){
    struct proc_rec *r = &precs[n];
    int ppid, pgid, sid;

    p = skip_ws(p);
    if(*p == '\0' || *p == '\n'){ p++; continue; }

    r->pid  = parse_int(&p);  p = skip_ws(p);
    ppid    = parse_int(&p);  p = skip_ws(p);
    pgid    = parse_int(&p);  p = skip_ws(p);
    sid     = parse_int(&p);  p = skip_ws(p);
    r->tty  = parse_int(&p);  p = skip_ws(p);
    r->uid  = parse_int(&p);  p = skip_ws(p);
    r->gid  = parse_int(&p);  p = skip_ws(p);

    (void)ppid; (void)pgid; (void)sid;
    r->ppid = ppid;

    parse_tok(&p, r->stat, sizeof(r->stat)); p = skip_ws(p);

    r->sz     = parse_uint(&p); p = skip_ws(p);
    r->cticks = parse_uint(&p); p = skip_ws(p);

    parse_tok(&p, r->name, sizeof(r->name));

    /* Advance past rest of line */
    while(*p && *p != '\n') p++;
    if(*p == '\n') p++;

    if(r->pid <= 0) continue;
    n++;
  }
  return n;
}

/* -----------------------------------------------------------------------
 * Memory info parsing
 * --------------------------------------------------------------------- */

static int
find_int_kb(const char *buf, const char *key)
{
  const char *p;
  int klen = 0;
  while(key[klen]) klen++;

  for(p = buf; *p; p++){
    int match = 1, i;
    for(i = 0; i < klen; i++){
      if(p[i] != key[i]){ match = 0; break; }
    }
    if(!match) continue;
    p += klen;
    while(*p == ' ' || *p == '\t') p++;
    {
      const char *q = p;
      return (int)parse_uint(&q);
    }
  }
  return 0;
}

/* -----------------------------------------------------------------------
 * Load average / uptime parsing
 * --------------------------------------------------------------------- */

/* Parse "X.YY Z.WW A.BB run/tot\n" into three fixed-point integers (×100). */
static void
parse_lavg(const char *buf, unsigned int *la1, unsigned int *la5, unsigned int *la15,
           int *nrun, int *ntot)
{
  const char *p = buf;
  unsigned int w, f;

#define PARSE_FIXED(dst) do { \
    w = parse_uint(&p);       \
    f = 0;                    \
    if(*p == '.'){ p++;       \
      f = parse_uint(&p);     \
    }                         \
    /* Store as fixed-point ×100 for display */   \
    *(dst) = w * 100 + f;    \
    p = skip_ws(p);           \
  } while(0)

  PARSE_FIXED(la1);
  PARSE_FIXED(la5);
  PARSE_FIXED(la15);
#undef PARSE_FIXED

  /* run/tot */
  *nrun = parse_int(&p);
  if(*p == '/') p++;
  *ntot = parse_int(&p);
}

/* -----------------------------------------------------------------------
 * Sorting
 * --------------------------------------------------------------------- */

/*
 * Insertion sort the display table by cpu_pct_x10 descending, then by pid.
 * This is O(n²) but MAX_PROCS=128 so it's negligible.
 */
static void
sort_dtable(int n)
{
  int i, j;
  struct disp_rec tmp;

  for(i = 1; i < n; i++){
    tmp = dtable[i];
    j = i - 1;
    while(j >= 0){
      if(dtable[j].cpu_pct_x10 < tmp.cpu_pct_x10 ||
         (dtable[j].cpu_pct_x10 == tmp.cpu_pct_x10 &&
          dtable[j].snap.pid > tmp.snap.pid)){
        dtable[j + 1] = dtable[j];
        j--;
      } else {
        break;
      }
    }
    dtable[j + 1] = tmp;
  }
}

/* -----------------------------------------------------------------------
 * Display helpers
 * --------------------------------------------------------------------- */

/* Convert kB to a short human-readable string in buf (≥8 bytes). */
static int
fmt_kb(char *buf, int kb)
{
  if(kb >= 1024 * 1024){
    int g = kb / (1024 * 1024);
    int m = (kb % (1024 * 1024)) * 10 / (1024 * 1024);
    return snprintf(buf, 8, "%d.%dG", g, m);
  }
  if(kb >= 1024){
    int m = kb / 1024;
    int k = (kb % 1024) * 10 / 1024;
    return snprintf(buf, 8, "%d.%dM", m, k);
  }
  return snprintf(buf, 8, "%dK", kb);
}

/* Format virtual size in bytes to short string. */
static int
fmt_vsz(char *buf, unsigned int bytes)
{
  return fmt_kb(buf, (int)(bytes / 1024));
}

/* Format an uptime (in ticks at HZ) as "Nd HH:MM:SS". */
static void
fmt_uptime(char *buf, int ticks)
{
  int secs  = ticks / HZ;
  int days  = secs / 86400; secs %= 86400;
  int hours = secs / 3600;  secs %= 3600;
  int mins  = secs / 60;    secs %= 60;

  if(days > 0)
    snprintf(buf, 24, "%dd %02d:%02d:%02d", days, hours, mins, secs);
  else
    snprintf(buf, 24, "%02d:%02d:%02d", hours, mins, secs);
}

/* Format a load average stored as integer×100 (e.g., 112 → "1.12"). */
static int
fmt_lavg_x100(char *buf, unsigned int v)
{
  unsigned int w = v / 100;
  unsigned int f = v % 100;
  return snprintf(buf, 8, "%u.%02u", w, f);
}

/* Map uid to a short user name (≤8 chars).  Reads /etc/passwd naively. */
static void
uid_to_name(int uid, char *out, int outlen)
{
  struct passwd *pw;

  pw = getpwuid((uid_t)uid);
  if(pw != 0) {
    snprintf(out, outlen, "%s", pw->pw_name);
    return;
  }

  snprintf(out, outlen, "%d", uid);
}

/* -----------------------------------------------------------------------
 * Snapshot update — merges a fresh proc_rec array into dtable
 * --------------------------------------------------------------------- */

static void
update_dtable(struct proc_rec *fresh, int nfresh, unsigned int elapsed)
{
  int i, j;

  /* Mark all present entries as absent */
  for(i = 0; i < dtable_n; i++)
    dtable[i].present = 0;

  for(j = 0; j < nfresh; j++){
    struct proc_rec *r = &fresh[j];
    int found = 0;

    /* Find existing entry */
    for(i = 0; i < dtable_n; i++){
      if(dtable[i].snap.pid == r->pid){
        found = 1;
        dtable[i].prev_cticks = dtable[i].snap.cticks;
        dtable[i].snap        = *r;
        dtable[i].present     = 1;

        /* Compute CPU% × 10 */
        if(elapsed > 0){
          unsigned int delta = r->cticks - dtable[i].prev_cticks;
          dtable[i].cpu_pct_x10 = (int)(delta * 1000 / elapsed);
        } else {
          dtable[i].cpu_pct_x10 = 0;
        }
        break;
      }
    }

    if(!found && dtable_n < MAX_PROCS){
      /* New process — no CPU history yet */
      dtable[dtable_n].snap         = *r;
      dtable[dtable_n].prev_cticks  = r->cticks;
      dtable[dtable_n].cpu_pct_x10  = 0;
      dtable[dtable_n].present      = 1;
      dtable_n++;
    }
  }

  /* Compact away dead processes */
  i = 0;
  while(i < dtable_n){
    if(!dtable[i].present){
      dtable[i] = dtable[dtable_n - 1];
      dtable_n--;
    } else {
      i++;
    }
  }
}

/* -----------------------------------------------------------------------
 * Rendering
 * --------------------------------------------------------------------- */

/* Map verbose kernel state name to a single UNIX-style status character:
 *   R = running / runnable
 *   S = sleeping
 *   Z = zombie
 *   T = stopped
 *   E = embryo (not yet runnable)
 *   ? = unknown
 */
static char
stat_char(const char *s)
{
  if(s[0] == 'r'){
    if(s[1] == 'u' && s[2] == 'n' && s[3] == 'n'){
      if(s[4] == 'i') return 'R';   /* running  */
      if(s[4] == 'a') return 'R';   /* runnable */
    }
    return '?';
  }
  if(s[0] == 's' && s[1] == 'l') return 'S'; /* sleep    */
  if(s[0] == 's' && s[1] == 't') return 'T'; /* stopped  */
  if(s[0] == 'z') return 'Z';       /* zombie   */
  if(s[0] == 'e') return 'E';       /* embryo   */
  return '?';
}

static void
render(unsigned int uptime_ticks, unsigned int la1, unsigned int la5,
       unsigned int la15, int nrun, int ntot, int mem_total, int mem_free)
{
  char ubuf[32], vbuf[8], cpubuf[8], userbuf[12], membuf[16];
  char la1buf[8], la5buf[8], la15buf[8];
  int i, row, maxrows;

  maxrows = ts.rows - HEADER_LINES;
  if(maxrows < 1) maxrows = 1;

  term_move(&ts, 0, 0);
  term_clear(&ts);

  /* --- Line 0: title + time --- */
  fmt_uptime(ubuf, (int)uptime_ticks);
  fmt_lavg_x100(la1buf,  la1);
  fmt_lavg_x100(la5buf,  la5);
  fmt_lavg_x100(la15buf, la15);

  term_highlight(&ts, TERM_CYAN);
  term_puts(&ts, "auxv6 top");
  term_reset_attrs(&ts);

  {
    char hdr[ts.cols + 1];
    snprintf(hdr, ts.cols,
             "   up %s   load: %s %s %s",
             ubuf, la1buf, la5buf, la15buf);
    term_puts(&ts, hdr);
  }
  term_clreol(&ts);

  /* --- Line 1: Tasks summary --- */
  term_move(&ts, 1, 0);
  {
    char tasks[ts.cols + 1];
    snprintf(tasks, ts.cols,
             "Tasks: %d total, %d running", ntot, nrun);
    term_puts(&ts, tasks);
  }
  term_clreol(&ts);

  /* --- Line 2: Memory --- */
  term_move(&ts, 2, 0);
  {
    char mline[ts.cols + 1];
    int mem_used = mem_total - mem_free;
    char tbuf[8], ubuf2[8], fbuf[8];
    fmt_kb(tbuf, mem_total);
    fmt_kb(ubuf2, mem_used);
    fmt_kb(fbuf, mem_free);
    snprintf(mline, ts.cols,
             "Mem:  %s total,  %s used,  %s free", tbuf, ubuf2, fbuf);
    term_puts(&ts, mline);
  }
  term_clreol(&ts);

  /* --- Line 3: blank --- */
  term_move(&ts, 3, 0);
  term_clreol(&ts);

  /* --- Line 4: column header --- */
  term_move(&ts, 4, 0);
  term_attr(&ts, TERM_REVERSE);
  {
    char hdr[ts.cols + 2];
    snprintf(hdr, ts.cols + 1,
             "%-6s %-8s %-1s  %5s  %5s  %5s  %s",
             "PID", "USER", "S", "VIRT", "CPU%", "TIME+", "COMMAND");
    /* Pad to column width */
    int hlen = 0; while(hdr[hlen]) hlen++;
    while(hlen < ts.cols){ hdr[hlen++] = ' '; }
    hdr[ts.cols] = '\0';
    term_puts(&ts, hdr);
  }
  term_reset_attrs(&ts);

  /* --- Process rows --- */
  row = HEADER_LINES;
  for(i = 0; i < dtable_n && (row - HEADER_LINES) < maxrows; i++){
    struct disp_rec *dr = &dtable[i];
    int cpu_w, cpu_f;
    unsigned int total_secs;
    int t_min, t_sec;
    char timbuf[12];

    if(!dr->present) continue;

    term_move(&ts, row, 0);

    /* CPU% formatted as WW.F */
    cpu_w = dr->cpu_pct_x10 / 10;
    cpu_f = dr->cpu_pct_x10 % 10;
    snprintf(cpubuf, sizeof(cpubuf), "%2d.%d", cpu_w, cpu_f);

    /* Highlight high-CPU processes */
    if(dr->cpu_pct_x10 >= 100){   /* ≥ 10.0% */
      term_highlight(&ts, TERM_RED);
    } else if(dr->cpu_pct_x10 >= 10){  /* ≥ 1.0% */
      term_highlight(&ts, TERM_YELLOW);
    }

    /* Virtual size */
    fmt_vsz(vbuf, dr->snap.sz);

    /* Cumulative CPU time as MM:SS.cc */
    total_secs = dr->snap.cticks / HZ;
    t_min = (int)(total_secs / 60);
    t_sec = (int)(total_secs % 60);
    snprintf(timbuf, sizeof(timbuf), "%d:%02d", t_min, t_sec);

    /* User name */
    uid_to_name(dr->snap.uid, userbuf, sizeof(userbuf));

    {
      char line[ts.cols + 2];
      int ll;
      ll = snprintf(line, ts.cols + 1,
                    "%-6d %-8s %-1c  %5s  %5s  %6s  %s",
                    dr->snap.pid, userbuf, stat_char(dr->snap.stat),
                    vbuf, cpubuf, timbuf, dr->snap.name);
      /* Pad to column width */
      while(ll < ts.cols){ line[ll++] = ' '; }
      line[ts.cols] = '\0';
      term_puts(&ts, line);
    }

    term_reset_attrs(&ts);
    row++;
  }

  /* Status line at bottom */
  term_move(&ts, ts.rows - 1, 0);
  term_attr(&ts, TERM_DIM);
  {
    char sbuf[ts.cols + 1];
    int ll = snprintf(sbuf, ts.cols,
                      "q:quit  r:refresh  h:help   "
                      "showing %d/%d processes", i, dtable_n);
    while(ll < ts.cols - 1) sbuf[ll++] = ' ';
    sbuf[ll] = '\0';
    term_puts(&ts, sbuf);
  }
  term_reset_attrs(&ts);

  /* flush any buffered output */
  (void)membuf;
}

/* -----------------------------------------------------------------------
 * Help overlay
 * --------------------------------------------------------------------- */

static void
show_help(void)
{
  static const char *lines[] = {
    "  q / Q      Quit top",
    "  r / R      Force immediate refresh",
    "  Space      Force immediate refresh",
    "  h / ?      Toggle this help overlay",
    "",
    "  Processes are sorted by CPU% descending.",
    "  CPU% shows percentage of a single CPU.",
    "  VIRT shows virtual address space size.",
    "  TIME+ shows cumulative CPU time (mm:ss).",
    0
  };
  int i, row = ts.rows / 4;

  term_attr(&ts, TERM_REVERSE);
  term_move(&ts, row - 1, ts.cols / 4);
  {
    char hdr[48];
    snprintf(hdr, 48, " %-38s ", "  top keyboard shortcuts");
    term_puts(&ts, hdr);
  }
  term_reset_attrs(&ts);

  for(i = 0; lines[i]; i++, row++){
    term_move(&ts, row, ts.cols / 4);
    term_attr(&ts, TERM_REVERSE);
    {
      char ln[48];
      snprintf(ln, 48, " %-38s ", lines[i]);
      term_puts(&ts, ln);
    }
    term_reset_attrs(&ts);
  }
}

/* -----------------------------------------------------------------------
 * Main loop
 * --------------------------------------------------------------------- */

static struct proc_rec fresh_procs[MAX_PROCS];
static char lavg_buf[LAVG_BUF];
static char meminfo_buf[MEMINFO_BUF];

int
main(void)
{
  unsigned int last_ut, cur_ut, elapsed;
  unsigned int la1, la5, la15;
  int nrun, ntot;
  int mem_total, mem_free;
  int nfresh;
  int key;
  int show_help_flag = 0;

  /* Install signal handlers */
  {
    struct sigaction sa;
    sa.sa_handler = handle_winch;
    sa.sa_mask    = 0;
    sa.sa_flags   = 0;
    sigaction(SIGWINCH, &sa, 0);

    sa.sa_handler = handle_quit;
    sigaction(SIGINT,  &sa, 0);
    sigaction(SIGTERM, &sa, 0);
  }

  /* Initialise terminal */
  term_init(&ts, 0, 1);
  if(term_enter(&ts) < 0){
    dprintf(2, "top: failed to enter raw mode\n");
    exit(1);
  }

  last_ut = (unsigned int)uptime();

  /* Bootstrap: take an initial snapshot so first display isn't all zeros */
  read_file("/proc/ps", ps_buf, sizeof(ps_buf));
  nfresh = parse_ps(ps_buf, fresh_procs, MAX_PROCS);
  update_dtable(fresh_procs, nfresh, 0);

  while(!g_quit){
    if(g_winch){
      g_winch = 0;
      term_update_size(&ts);
    }

    /* Collect process data */
    read_file("/proc/ps", ps_buf, sizeof(ps_buf));
    nfresh = parse_ps(ps_buf, fresh_procs, MAX_PROCS);

    /* Collect memory */
    read_file("/proc/meminfo", meminfo_buf, sizeof(meminfo_buf));
    mem_total = find_int_kb(meminfo_buf, "MemTotal:");
    mem_free  = find_int_kb(meminfo_buf, "MemFree:");

    /* Collect load average */
    la1 = la5 = la15 = 0; nrun = ntot = 0;
    if(read_file("/proc/loadavg", lavg_buf, sizeof(lavg_buf)) > 0)
      parse_lavg(lavg_buf, &la1, &la5, &la15, &nrun, &ntot);

    /* Uptime */
    cur_ut  = (unsigned int)uptime();
    elapsed = cur_ut - last_ut;
    last_ut = cur_ut;

    /* Merge snapshot, compute CPU deltas */
    update_dtable(fresh_procs, nfresh, elapsed);
    sort_dtable(dtable_n);

    /* Render */
    if(show_help_flag){
      render(cur_ut, la1, la5, la15, nrun, ntot, mem_total, mem_free);
      show_help();
    } else {
      render(cur_ut, la1, la5, la15, nrun, ntot, mem_total, mem_free);
    }

    /* Poll for input during the refresh interval */
    {
      int waited = 0;
      while(waited < REFRESH_TICKS && !g_quit && !g_winch){
        key = term_poll_key(&ts, 100);  /* 100ms slices */
        waited += 10;  /* ~100ms = 10 ticks at 100Hz */

        if(key <= 0) continue;

        switch(key){
        case 'q': case 'Q':
          g_quit = 1;
          break;
        case 'r': case 'R': case ' ':
          waited = REFRESH_TICKS;  /* break out of wait loop → refresh now */
          break;
        case 'h': case '?':
          show_help_flag = !show_help_flag;
          /* Force re-render immediately */
          waited = REFRESH_TICKS;
          break;
        }
      }
    }
  }

  term_leave(&ts);
  exit(0);
}
