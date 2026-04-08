#include "types.h"
#include "auxv6/user.h"
#include "fcntl.h"
#include "stat.h"
#include "string.h"
#include "stdio.h"

#define XWMTRACE_MAX_LINE 512

typedef struct {
  const char *path;
  const char *tag;
  int off;
  char partial[XWMTRACE_MAX_LINE];
  int plen;
  char last_line[XWMTRACE_MAX_LINE];
  int dup_count;
} trace_src;

static int g_all_lines;

static int
line_interesting(const char *line)
{
  if(!line)
    return 0;
  if(g_all_lines)
    return 1;

  if(strstr(line, "DamageNotify"))
    return 0;

  if(strstr(line, "event dropped raw='EVENT DamageNotify"))
    return 0;

  if(strstr(line, "WM_MAP") || strstr(line, "wm_map step="))
    return 1;
  if(strstr(line, "x6:map ") || strstr(line, "x11:map "))
    return 1;
  if(strstr(line, "CONFIGURE") || strstr(line, "Configure"))
    return 1;
  if(strstr(line, "MapRequest") || strstr(line, "MapNotify"))
    return 1;
  if(strstr(line, "PENDING configure") || strstr(line, "OK configure") || strstr(line, "OK configured"))
    return 1;
  if(strstr(line, "DamageNotify") || strstr(line, "ShapeNotify") || strstr(line, "RandRNotify"))
    return 1;
  if(strstr(line, "queue guard hit"))
    return 1;
  return 0;
}

static void
flush_dup_summary(trace_src *src)
{
  if(!src)
    return;
  if(src->dup_count <= 0)
    return;
  dprintf(1, "%s: [suppressed %d duplicate lines] %s\n", src->tag, src->dup_count,
          src->last_line[0] ? src->last_line : "(unknown)");
  src->dup_count = 0;
}

static void
emit_line(const trace_src *src, const char *line)
{
  trace_src *mut;

  if(!src || !line)
    return;
  if(!line_interesting(line))
    return;

  mut = (trace_src*)src;
  if(mut->last_line[0] && strcmp(mut->last_line, line) == 0) {
    mut->dup_count++;
    return;
  }

  flush_dup_summary(mut);
  snprintf(mut->last_line, sizeof(mut->last_line), "%s", line);
  dprintf(1, "%s: %s\n", src->tag, line);
}

static void
append_chunk(trace_src *src, const char *buf, int n)
{
  int i;

  if(!src || !buf || n <= 0)
    return;

  for(i = 0; i < n; i++) {
    char c = buf[i];
    if(c == '\r')
      continue;
    if(c == '\n') {
      src->partial[src->plen] = '\0';
      emit_line(src, src->partial);
      src->plen = 0;
      continue;
    }

    if(src->plen < XWMTRACE_MAX_LINE - 1) {
      src->partial[src->plen++] = c;
    } else {
      src->partial[src->plen] = '\0';
      emit_line(src, src->partial);
      src->plen = 0;
    }
  }
}

static void
pump_source(trace_src *src)
{
  int fd;
  int n;
  char buf[256];

  if(!src)
    return;

  fd = open(src->path, O_RDONLY);
  if(fd < 0)
    return;

  if(src->off < 0)
    src->off = 0;
  if(lseek(fd, src->off, 0) < 0) {
    src->off = 0;
    if(lseek(fd, 0, 0) < 0) {
      close(fd);
      return;
    }
  }

  while((n = read(fd, buf, sizeof(buf))) > 0) {
    src->off += n;
    append_chunk(src, buf, n);
  }

  close(fd);
}

static void
usage(void)
{
  dprintf(2, "usage: xwmtrace [-a]\n");
  dprintf(2, "  -a  print all log lines (default: filtered trace lines)\n");
}

int
main(int argc, char **argv)
{
  int i;
  trace_src sources[2];

  for(i = 1; i < argc; i++) {
    if(strcmp(argv[i], "-a") == 0) {
      g_all_lines = 1;
      continue;
    }
    if(strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      usage();
      return 0;
    }
    usage();
    return 1;
  }

  memset(sources, 0, sizeof(sources));
  sources[0].path = "/tmp/x6-debug.log";
  sources[0].tag = "x6trace";
  sources[1].path = "/tmp/x11-debug.log";
  sources[1].tag = "x11trace";

  dprintf(1, "xwmtrace: watching /tmp/x6-debug.log and /tmp/x11-debug.log\n");

  while(1) {
    pump_source(&sources[0]);
    pump_source(&sources[1]);
    flush_dup_summary(&sources[0]);
    flush_dup_summary(&sources[1]);
    sleep(1);
  }

  return 0;
}
