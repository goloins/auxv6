// gfxperf.c - framebuffer console performance probe
//
// Runs a deterministic tty output workload, samples /proc/gfxstats before and
// after, and reports derived metrics:
//   pixels_per_flush     = flush_pixels / flush_calls
//   cells_changed_sync   = cells_changed / sync_calls
//   render_efficiency    = cells_rendered / cells_changed
//
// Usage:
//   gfxperf [-l lines] [-r rounds] [-w width]

#include "types.h"
#include "stat.h"
#include "stdio.h"
#include "auxv6/user.h"
#include "fcntl.h"

#define GFXPERF_HZ 100U

#define GFXSTATS_BUF 4096

struct gfxstats_snapshot {
  uint sync_calls;
  uint cells_changed;
  uint cells_rendered;
  uint flush_calls;
  uint flush_pixels;
};

static volatile int g_stop;

static void
gfxperf_sigint(int signo)
{
  if(signo == SIGINT)
    g_stop = 1;
}

static void
usage(void)
{
  dprintf(2, "usage: gfxperf [-l lines] [-r rounds] [-w width] [-q|-Q] [-P progress_lines]\n");
}

static int
is_space(char c)
{
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int
parse_key_value_line(const char *line, int len, char *key, int ksz, uint *val)
{
  int i;
  int klen;
  int found;

  if(!line || len <= 0 || !key || ksz <= 1 || !val)
    return -1;

  i = 0;
  while(i < len && is_space(line[i]))
    i++;
  if(i >= len)
    return -1;

  klen = 0;
  while(i < len && !is_space(line[i])) {
    if(klen + 1 < ksz)
      key[klen++] = line[i];
    i++;
  }
  key[klen] = '\0';

  while(i < len && is_space(line[i]))
    i++;
  if(i >= len)
    return -1;

  *val = 0;
  found = 0;
  while(i < len && !is_space(line[i])) {
    if(line[i] < '0' || line[i] > '9')
      return -1;
    *val = (*val * 10U) + (uint)(line[i] - '0');
    found = 1;
    i++;
  }

  return found ? 0 : -1;
}

static int
read_gfxstats(struct gfxstats_snapshot *out)
{
  int fd;
  int n;
  int i;
  char buf[GFXSTATS_BUF + 1];

  if(!out)
    return -1;

  memset(out, 0, sizeof(*out));

  fd = open("/proc/gfxstats", O_RDONLY);
  if(fd < 0)
    return -1;

  n = read(fd, buf, GFXSTATS_BUF);
  close(fd);
  if(n <= 0)
    return -1;
  if(n > GFXSTATS_BUF)
    n = GFXSTATS_BUF;
  buf[n] = '\0';

  i = 0;
  while(i < n) {
    int ls = i;
    int le;
    char key[64];
    uint val;

    while(i < n && buf[i] != '\n')
      i++;
    le = i;
    if(i < n && buf[i] == '\n')
      i++;

    if(parse_key_value_line(&buf[ls], le - ls, key, sizeof(key), &val) < 0)
      continue;

    if(strcmp(key, "sync_calls") == 0)
      out->sync_calls = val;
    else if(strcmp(key, "cells_changed") == 0)
      out->cells_changed = val;
    else if(strcmp(key, "cells_rendered") == 0)
      out->cells_rendered = val;
    else if(strcmp(key, "flush_calls") == 0)
      out->flush_calls = val;
    else if(strcmp(key, "flush_pixels") == 0)
      out->flush_pixels = val;
  }

  return 0;
}

static uint
delta_u(uint after, uint before)
{
  /* Unsigned subtraction is wrap-safe for monotonically increasing counters. */
  return after - before;
}

static uint
ratio_x100(uint num, uint den)
{
  if(den == 0)
    return 0;
  return (num * 100U) / den;
}

static void
print_fixed_2(int fd, uint x100)
{
  dprintf(fd, "%d.%02d", (int)(x100 / 100U), (int)(x100 % 100U));
}

static void
print_snapshot_delta(const char *tag,
                     const struct gfxstats_snapshot *before,
                     const struct gfxstats_snapshot *after)
{
  uint d_sync;
  uint d_changed;
  uint d_rendered;
  uint d_flush;
  uint d_pixels;
  uint pixels_per_flush_x100;
  uint cells_per_sync_x100;
  uint render_eff_x100;

  d_sync = delta_u(after->sync_calls, before->sync_calls);
  d_changed = delta_u(after->cells_changed, before->cells_changed);
  d_rendered = delta_u(after->cells_rendered, before->cells_rendered);
  d_flush = delta_u(after->flush_calls, before->flush_calls);
  d_pixels = delta_u(after->flush_pixels, before->flush_pixels);

  pixels_per_flush_x100 = ratio_x100(d_pixels, d_flush ? d_flush : 1);
  cells_per_sync_x100 = ratio_x100(d_changed, d_sync ? d_sync : 1);
  render_eff_x100 = ratio_x100(d_rendered, d_changed ? d_changed : 1);

  dprintf(1, "%s: delta sync_calls=%u cells_changed=%u cells_rendered=%u flush_calls=%u flush_pixels=%u\n",
          tag, d_sync, d_changed, d_rendered, d_flush, d_pixels);
  dprintf(1, "%s: pixels_per_flush=", tag);
  print_fixed_2(1, pixels_per_flush_x100);
  dprintf(1, " cells_per_sync=");
  print_fixed_2(1, cells_per_sync_x100);
  dprintf(1, " render_efficiency=");
  print_fixed_2(1, render_eff_x100);
  dprintf(1, "\n");
}

static void
emit_workload(int lines, int rounds, int width, int progress_lines,
              struct gfxstats_snapshot *base, int *lines_done_out)
{
  int r;
  int i;
  int lines_done;
  int lines_since_progress;
  char linebuf[256];

  if(lines < 1)
    lines = 1;
  if(rounds < 1)
    rounds = 1;
  if(width < 8)
    width = 8;

  lines_done = 0;
  lines_since_progress = 0;

  for(r = 0; r < rounds; r++) {
    for(i = 0; i < lines; i++) {
      int n;
      int pad;
      int off;
      int rc;

      if(g_stop)
        goto done;

      /* Alternate SGR to exercise parser + render path under realistic output. */
      n = snprintf(linebuf, sizeof(linebuf), "\033[3%dm[gfxperf] round=%d line=%d ",
                   (i % 7) + 1, r + 1, i + 1);
      if(n < 0)
        n = 0;
      if(n > (int)sizeof(linebuf) - 1)
        n = (int)sizeof(linebuf) - 1;

      pad = width - 24;
      if(pad < 0)
        pad = 0;
      while(pad-- > 0 && n < (int)sizeof(linebuf) - 5)
        linebuf[n++] = (char)('a' + (i % 26));

      if(n < (int)sizeof(linebuf) - 4) {
        linebuf[n++] = ' ';
      }
      if(n >= (int)sizeof(linebuf) - 4)
        n = (int)sizeof(linebuf) - 4;
      linebuf[n++] = 0x1b;
      linebuf[n++] = '[';
      linebuf[n++] = '0';
      linebuf[n++] = 'm';
      if(n < (int)sizeof(linebuf) - 1)
        linebuf[n++] = '\n';

      off = 0;
      while(off < n) {
        rc = write(1, linebuf + off, (size_t)(n - off));
        if(rc <= 0)
          break;
        off += rc;
      }

      lines_done++;
      lines_since_progress++;
      if(progress_lines > 0 && lines_since_progress >= progress_lines && base) {
        struct gfxstats_snapshot now;
        if(read_gfxstats(&now) == 0)
          print_snapshot_delta("gfxperf-progress", base, &now);
        lines_since_progress = 0;
      }
    }
  }

done:
  if(lines_done_out)
    *lines_done_out = lines_done;
}

int
main(int argc, char **argv)
{
  int i;
  int lines;
  int rounds;
  int width;
  int progress_lines;
  int lines_done;
  int pass;
  uint t0;
  uint t1;
  uint dt;
  uint lines_per_sec_x100;
  struct gfxstats_snapshot before;
  struct gfxstats_snapshot after;
  uint d_sync;
  uint d_changed;
  uint d_rendered;
  uint d_flush;
  uint d_pixels;
  uint pixels_per_flush_x100;
  uint cells_per_sync_x100;
  uint render_eff_x100;

  lines = 600;
  rounds = 1;
  width = 72;
  progress_lines = 0;
  lines_done = 0;

  {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = gfxperf_sigint;
    sigaction(SIGINT, &sa, 0);
  }

  for(i = 1; i < argc; i++) {
    if(strcmp(argv[i], "-l") == 0) {
      if(i + 1 >= argc) {
        usage();
        return 1;
      }
      lines = atoi(argv[++i]);
    } else if(strcmp(argv[i], "-r") == 0) {
      if(i + 1 >= argc) {
        usage();
        return 1;
      }
      rounds = atoi(argv[++i]);
    } else if(strcmp(argv[i], "-w") == 0) {
      if(i + 1 >= argc) {
        usage();
        return 1;
      }
      width = atoi(argv[++i]);
    } else if(strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      usage();
      return 0;
    } else if(strcmp(argv[i], "-P") == 0 || strcmp(argv[i], "--progress") == 0) {
      if(i + 1 >= argc) {
        usage();
        return 1;
      }
      progress_lines = atoi(argv[++i]);
    } else if(strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quick") == 0) {
      lines = 120;
      rounds = 1;
      width = 64;
    } else if(strcmp(argv[i], "-Q") == 0 || strcmp(argv[i], "--smoke") == 0) {
      lines = 60;
      rounds = 1;
      width = 48;
    } else {
      usage();
      return 1;
    }
  }

  if(read_gfxstats(&before) < 0) {
    dprintf(2, "gfxperf: failed to read /proc/gfxstats (before)\n");
    return 1;
  }

  t0 = uptime();
  emit_workload(lines, rounds, width, progress_lines, &before, &lines_done);
  t1 = uptime();
  dt = (t1 > t0) ? (t1 - t0) : 1;
  lines_per_sec_x100 = (uint)lines_done * GFXPERF_HZ * 100U / dt;

  if(read_gfxstats(&after) < 0) {
    dprintf(2, "gfxperf: failed to read /proc/gfxstats (after)\n");
    return 1;
  }

  d_sync = delta_u(after.sync_calls, before.sync_calls);
  d_changed = delta_u(after.cells_changed, before.cells_changed);
  d_rendered = delta_u(after.cells_rendered, before.cells_rendered);
  d_flush = delta_u(after.flush_calls, before.flush_calls);
  d_pixels = delta_u(after.flush_pixels, before.flush_pixels);

  pixels_per_flush_x100 = ratio_x100(d_pixels, d_flush ? d_flush : 1);
  cells_per_sync_x100 = ratio_x100(d_changed, d_sync ? d_sync : 1);
  render_eff_x100 = ratio_x100(d_rendered, d_changed ? d_changed : 1);

  dprintf(1, "gfxperf: delta sync_calls=%u cells_changed=%u cells_rendered=%u flush_calls=%u flush_pixels=%u\n",
          d_sync, d_changed, d_rendered, d_flush, d_pixels);

  dprintf(1, "gfxperf: pixels_per_flush=");
  print_fixed_2(1, pixels_per_flush_x100);
  dprintf(1, " cells_per_sync=");
  print_fixed_2(1, cells_per_sync_x100);
  dprintf(1, " render_efficiency=");
  print_fixed_2(1, render_eff_x100);
  dprintf(1, "\n");

  dprintf(1, "gfxperf: lines_done=%d elapsed_ticks=%u lines_per_sec=", lines_done, dt);
  print_fixed_2(1, lines_per_sec_x100);
  dprintf(1, "\n");

  if(g_stop)
    dprintf(1, "gfxperf: interrupted by SIGINT, reporting partial results\n");

  pass = 1;
  if(d_sync == 0 || d_flush == 0) {
    dprintf(1, "[FAIL] gfxperf counters did not advance (sync/flush)\n");
    pass = 0;
  } else {
    dprintf(1, "[PASS] gfxperf counters advanced\n");
  }

  if(d_changed == 0 || d_rendered == 0) {
    dprintf(1, "[FAIL] gfxperf render counters did not advance\n");
    pass = 0;
  } else {
    dprintf(1, "[PASS] gfxperf render counters advanced\n");
  }

  /* Rendered cells should track changed cells closely; allow modest overhead. */
  if(d_changed > 0 && render_eff_x100 > 150U) {
    dprintf(1, "[FAIL] render_efficiency too high (>1.50x)\n");
    pass = 0;
  } else {
    dprintf(1, "[PASS] render_efficiency bounded\n");
  }

  return pass ? 0 : 1;
}
