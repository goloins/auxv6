// kmemstress.c - broad kernel memory/API stress utility
//
// Purpose:
// - Exercise allocator-adjacent kernel APIs continuously under mixed pressure
// - Emit high-frequency telemetry from procfs and syscall surfaces
// - Help catch racey memory corruption that only appears under interface churn

#include "types.h"
#include "sys/stat.h"
#include "fcntl.h"
#include "dirent.h"
#include "auxv6/user.h"
#include "socket.h"

#define KMEMSTRESS_PROFILE "2026-04-06-r1"

#define PAGE_BYTES 4096
#define MAX_WORKERS 32
#define MAX_PATH 128
#define MAX_TEXT 2048
#define PROC_SAMPLE_ROUNDS 1

#define DEFAULT_ROUNDS 0        // 0 = run forever
#define DEFAULT_WORKERS 8
#define DEFAULT_CHILD_PAGES 12
#define DEFAULT_FILE_OPS 32
#define DEFAULT_SOCK_OPS 32
#define DEFAULT_SCRATCH "/tmp/kmemstress"

static int g_rounds = DEFAULT_ROUNDS;
static int g_workers = DEFAULT_WORKERS;
static int g_child_pages = DEFAULT_CHILD_PAGES;
static int g_file_ops = DEFAULT_FILE_OPS;
static int g_sock_ops = DEFAULT_SOCK_OPS;
static int g_verbose = 0;
static const char *g_scratch = DEFAULT_SCRATCH;

static int g_fail_total;
static int g_warn_total;
static int g_ftruncate_supported = -1; // -1 unknown, 0 unsupported, 1 supported
static int g_warned_scratch_scan = 0;

#define KMEM_MOUNTINFO_CAP 8
#define KMEM_NETIF_CAP 8
#define KMEM_ROUTE_CAP 16
#define KMEM_ARP_CAP 16

static struct mountinfo g_mi[KMEM_MOUNTINFO_CAP];
static struct netifinfo g_ni[KMEM_NETIF_CAP];
static struct routeinfo g_ri[KMEM_ROUTE_CAP];
static struct arpinfo g_ai[KMEM_ARP_CAP];

static void
set_preset_lite(void)
{
  g_workers = 4;
  g_child_pages = 8;
  g_file_ops = 16;
  g_sock_ops = 16;
}

static void
set_preset_balanced(void)
{
  g_workers = 8;
  g_child_pages = 12;
  g_file_ops = 32;
  g_sock_ops = 32;
}

static void
set_preset_high(void)
{
  g_workers = 16;
  g_child_pages = 24;
  g_file_ops = 64;
  g_sock_ops = 64;
}

static int
streq(const char *a, const char *b)
{
  while(*a && *b){
    if(*a != *b)
      return 0;
    a++;
    b++;
  }
  return *a == *b;
}

static int
toint(const char *s)
{
  int v;

  v = 0;
  while(*s >= '0' && *s <= '9'){
    v = v * 10 + (*s - '0');
    s++;
  }
  return v;
}

static void
int_to_str(int v, char *buf, int max)
{
  char tmp[16];
  int n;
  int pos;
  int i;

  if(max < 2)
    return;

  n = 0;
  pos = 0;
  if(v == 0){
    buf[0] = '0';
    buf[1] = 0;
    return;
  }

  if(v < 0 && pos < max - 1){
    buf[pos++] = '-';
    v = -v;
  }

  while(v > 0 && n < (int)sizeof(tmp) - 1){
    tmp[n++] = (char)('0' + (v % 10));
    v /= 10;
  }

  for(i = n - 1; i >= 0 && pos < max - 1; i--)
    buf[pos++] = tmp[i];
  buf[pos] = 0;
}

static void
str_cat(char *dst, int max, const char *src)
{
  int i;

  i = 0;
  while(dst[i])
    i++;
  while(*src && i < max - 1)
    dst[i++] = *src++;
  dst[i] = 0;
}

static void
build_path(char *out, int max, int round, int idx)
{
  char rbuf[16];
  char ibuf[16];

  out[0] = 0;
  str_cat(out, max, g_scratch);
  str_cat(out, max, "/r");
  int_to_str(round, rbuf, sizeof(rbuf));
  int_to_str(idx, ibuf, sizeof(ibuf));
  str_cat(out, max, rbuf);
  str_cat(out, max, "_f");
  str_cat(out, max, ibuf);
  str_cat(out, max, ".dat");
}

static int
read_text(const char *path, char *buf, int max)
{
  int fd;
  int off;
  int n;

  fd = open(path, O_RDONLY);
  if(fd < 0)
    return -1;

  off = 0;
  while(off < max - 1){
    n = read(fd, buf + off, max - 1 - off);
    if(n < 0){
      close(fd);
      return -1;
    }
    if(n == 0)
      break;
    off += n;
  }
  buf[off] = 0;
  close(fd);
  return off;
}

static int
parse_kv_uint(const char *txt, const char *key)
{
  int i;
  int j;

  for(i = 0; txt[i]; i++){
    for(j = 0; key[j] && txt[i + j] == key[j]; j++)
      ;
    if(key[j] != 0)
      continue;

    i += j;
    while(txt[i] && (txt[i] < '0' || txt[i] > '9'))
      i++;
    return atoi((char *)&txt[i]);
  }

  return -1;
}

static void
usage(void)
{
  dprintf(2,
    "usage: kmemstress [options]\n"
    "  -n <rounds>   number of rounds (0 = forever, default %d)\n"
    "  -w <workers>  fork workers per round (default %d, max %d)\n"
    "  -p <pages>    child pages touched per worker (default %d)\n"
    "  -f <ops>      file/proc/fd ops per round (default %d)\n"
    "  -s <ops>      UDP socket ops per round (default %d)\n"
    "  -d <dir>      scratch directory (default %s)\n"
    "  -L            lite preset (w=4 p=8 f=16 s=16)\n"
    "  -M            balanced preset (w=8 p=12 f=32 s=32)\n"
    "  -H            high preset (w=16 p=24 f=64 s=64)\n"
    "  -v            verbose proc snapshots each round\n",
    DEFAULT_ROUNDS,
    DEFAULT_WORKERS, MAX_WORKERS,
    DEFAULT_CHILD_PAGES,
    DEFAULT_FILE_OPS,
    DEFAULT_SOCK_OPS,
    DEFAULT_SCRATCH);
  exit(1);
}

static void
proc_snapshot(int round)
{
  char buf[MAX_TEXT];

  dprintf(1, "[diag] round=%d /proc/meminfo\n", round);
  if(read_text("/proc/meminfo", buf, sizeof(buf)) > 0)
    dprintf(1, "%s", buf);

  dprintf(1, "[diag] round=%d /proc/vmstat\n", round);
  if(read_text("/proc/vmstat", buf, sizeof(buf)) > 0)
    dprintf(1, "%s", buf);

  if(g_verbose){
    dprintf(1, "[diag] round=%d /proc/schedstat\n", round);
    if(read_text("/proc/schedstat", buf, sizeof(buf)) > 0)
      dprintf(1, "%s", buf);

    dprintf(1, "[diag] round=%d /proc/bcache_health\n", round);
    if(read_text("/proc/bcache_health", buf, sizeof(buf)) > 0)
      dprintf(1, "%s", buf);
  }
}

static int
stress_vm_fork(int round)
{
  int i;
  int fails;

  fails = 0;
  for(i = 0; i < g_workers; i++){
    int pid;

    pid = fork();
    if(pid < 0){
      fails++;
      continue;
    }
    if(pid == 0){
      char *base;
      int p;

      base = sbrk((intptr_t)g_child_pages * PAGE_BYTES);
      if(base == (char*)-1)
        exit(2);

      for(p = 0; p < g_child_pages; p++)
        base[p * PAGE_BYTES] = (char)((round + i + p) & 0xff);

      exit(0);
    }
  }

  for(i = 0; i < g_workers; i++){
    int st;
    int w;

    st = 0;
    w = waitpid(-1, &st, 0);
    if(w < 0 || st != 0)
      fails++;
  }

  return fails;
}

static int
stress_pipe_poll(int round)
{
  int i;
  int fails;
  char wbuf[256];
  char rbuf[256];

  (void)round;
  fails = 0;
  for(i = 0; i < (int)sizeof(wbuf); i++)
    wbuf[i] = (char)(i & 0x7f);

  for(i = 0; i < g_file_ops; i++){
    int pfd[2];
    struct pollfd p;

    if(pipe(pfd) < 0){
      fails++;
      continue;
    }

    if(write(pfd[1], wbuf, sizeof(wbuf)) != sizeof(wbuf))
      fails++;

    p.fd = pfd[0];
    p.events = POLLIN;
    p.revents = 0;
    if(poll(&p, 1, 0) < 0)
      fails++;

    if(read(pfd[0], rbuf, sizeof(rbuf)) != sizeof(rbuf))
      fails++;

    close(pfd[0]);
    close(pfd[1]);
  }

  return fails;
}

static int
stress_vfs_files(int round)
{
  int i;
  int fails;
  char path[MAX_PATH];
  char wbuf[512];
  char rbuf[512];

  fails = 0;
  mkdir(g_scratch);

  for(i = 0; i < (int)sizeof(wbuf); i++)
    wbuf[i] = (char)((round + i) & 0xff);

  for(i = 0; i < g_file_ops; i++){
    int fd;
    struct stat st;

    build_path(path, sizeof(path), round, i);
    fd = open(path, O_RDWR | O_CREATE | O_TRUNC);
    if(fd < 0){
      fails++;
      continue;
    }

    if(write(fd, wbuf, sizeof(wbuf)) != sizeof(wbuf))
      fails++;
    if(write(fd, wbuf, sizeof(wbuf)) != sizeof(wbuf))
      fails++;

    if(lseek(fd, 0, SEEK_SET) < 0)
      fails++;
    if(read(fd, rbuf, sizeof(rbuf)) != sizeof(rbuf))
      fails++;
    if(rbuf[0] != wbuf[0])
      fails++;

    if(g_ftruncate_supported != 0){
      if(ftruncate(fd, 700) < 0){
        if(g_ftruncate_supported < 0){
          g_ftruncate_supported = 0;
          g_warn_total++;
          dprintf(1, "[warn] ftruncate appears unsupported on active fs; downgrading this check\n");
        } else {
          fails++;
        }
      } else if(g_ftruncate_supported < 0){
        g_ftruncate_supported = 1;
      }
    }
    close(fd);

    if(stat(path, &st) < 0)
      fails++;
    if(unlink(path) < 0)
      fails++;
  }

  return fails;
}

static int
stress_procfs_and_dir(void)
{
  static const char *proc_paths_required[] = {
    "/proc/meminfo",
    "/proc/vmstat",
    0
  };
  static const char *proc_paths_optional[] = {
    "/proc/mounts",
    "/proc/loadavg",
    "/proc/schedstat",
    "/proc/lsof",
    "/proc/bcache_health",
    0
  };
  int i;
  int fails;

  fails = 0;
  for(i = 0; proc_paths_required[i] != 0; i++){
    char buf[256];
    if(read_text(proc_paths_required[i], buf, sizeof(buf)) < 0)
      fails++;
  }

  for(i = 0; proc_paths_optional[i] != 0; i++){
    char buf[256];
    if(read_text(proc_paths_optional[i], buf, sizeof(buf)) < 0)
      g_warn_total++;
  }

  {
    int fd;
    struct dirent ents[16];
    int n;
    struct stat st;

    if(stat(g_scratch, &st) < 0 || !S_ISDIR(st.st_mode)){
      if(!g_warned_scratch_scan){
        dprintf(1, "[warn] scratch path not a directory for getdents scan: %s\n", g_scratch);
        g_warned_scratch_scan = 1;
      }
      g_warn_total++;
      return fails;
    }

    fd = open(g_scratch, O_RDONLY);
    if(fd >= 0){
      do {
        n = getdents(fd, ents, 16);
        if(n < 0){
          if(!g_warned_scratch_scan){
            dprintf(1, "[warn] getdents failed on scratch dir scan: %s\n", g_scratch);
            g_warned_scratch_scan = 1;
          }
          g_warn_total++;
          break;
        }
      } while(n > 0);
      close(fd);
    } else {
      if(!g_warned_scratch_scan){
        dprintf(1, "[warn] unable to open scratch dir for scan: %s\n", g_scratch);
        g_warned_scratch_scan = 1;
      }
      g_warn_total++;
    }
  }

  return fails;
}

static int
stress_net_socket(int round)
{
  int i;
  int fails;

  fails = 0;
  for(i = 0; i < g_sock_ops; i++){
    int rfd;
    int sfd;
    struct sockaddr_in raddr;
    struct sockaddr_in saddr;
    char msg[32];
    char got[64];
    int n;
    int port;

    port = 21000 + ((round * g_sock_ops + i) % 2000);

    rfd = socket(AF_INET, SOCK_DGRAM, 0);
    sfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(rfd < 0 || sfd < 0){
      if(rfd >= 0) close(rfd);
      if(sfd >= 0) close(sfd);
      fails++;
      continue;
    }

    memset(&raddr, 0, sizeof(raddr));
    raddr.sin_family = AF_INET;
    raddr.sin_port = (ushort)port;
    raddr.sin_addr.s_addr = INADDR_LOOPBACK;

    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = (ushort)(port + 3000);
    saddr.sin_addr.s_addr = INADDR_LOOPBACK;

    if(bind(rfd, &raddr, sizeof(raddr)) < 0)
      fails++;
    if(bind(sfd, &saddr, sizeof(saddr)) < 0)
      fails++;
    if(connect(sfd, &raddr, sizeof(raddr)) < 0)
      fails++;

    strcpy(msg, "kmemstress-loopback");
    if(send(sfd, msg, strlen(msg)) < 0)
      fails++;

    n = recvtimeout(rfd, got, sizeof(got), 20);
    if(n <= 0)
      fails++;

    close(sfd);
    close(rfd);
  }

  return fails;
}

static int
stress_kernel_meta_apis(void)
{
  int fails;
  struct rlimit rlim;
  int kmsg_n;
  char kmsg_buf[128];

  fails = 0;

  if(mountinfo(g_mi, KMEM_MOUNTINFO_CAP) < 0)
    fails++;
  if(netifinfo(g_ni, KMEM_NETIF_CAP) < 0)
    fails++;
  if(routeinfo(g_ri, KMEM_ROUTE_CAP) < 0)
    fails++;
  if(arpinfo(g_ai, KMEM_ARP_CAP) < 0)
    fails++;

  if(getrlimit(RLIMIT_NOFILE, &rlim) < 0)
    fails++;
  if(getrlimit(RLIMIT_STACK, &rlim) < 0)
    fails++;

  kmsg_n = kmsgread(kmsg_buf, sizeof(kmsg_buf));
  if(kmsg_n < 0)
    fails++;

  return fails;
}

int
main(int argc, char *argv[])
{
  int i;
  int round;

  set_preset_balanced();

  for(i = 1; i < argc; i++){
    if(streq(argv[i], "-n") && i + 1 < argc){
      g_rounds = toint(argv[++i]);
      if(g_rounds < 0)
        g_rounds = DEFAULT_ROUNDS;
    } else if(streq(argv[i], "-L")){
      set_preset_lite();
    } else if(streq(argv[i], "-M")){
      set_preset_balanced();
    } else if(streq(argv[i], "-H")){
      set_preset_high();
    } else if(streq(argv[i], "-w") && i + 1 < argc){
      g_workers = toint(argv[++i]);
      if(g_workers < 1 || g_workers > MAX_WORKERS)
        g_workers = DEFAULT_WORKERS;
    } else if(streq(argv[i], "-p") && i + 1 < argc){
      g_child_pages = toint(argv[++i]);
      if(g_child_pages < 1)
        g_child_pages = DEFAULT_CHILD_PAGES;
    } else if(streq(argv[i], "-f") && i + 1 < argc){
      g_file_ops = toint(argv[++i]);
      if(g_file_ops < 1)
        g_file_ops = DEFAULT_FILE_OPS;
    } else if(streq(argv[i], "-s") && i + 1 < argc){
      g_sock_ops = toint(argv[++i]);
      if(g_sock_ops < 1)
        g_sock_ops = DEFAULT_SOCK_OPS;
    } else if(streq(argv[i], "-d") && i + 1 < argc){
      g_scratch = argv[++i];
    } else if(streq(argv[i], "-v")){
      g_verbose = 1;
    } else {
      usage();
    }
  }

  mkdir(g_scratch);

  dprintf(1, "kmemstress: broad kernel memory/API stress utility\n");
  dprintf(1, "  profile=%s rounds=%d workers=%d pages=%d file_ops=%d sock_ops=%d dir=%s\n",
          KMEMSTRESS_PROFILE,
          g_rounds,
          g_workers,
          g_child_pages,
          g_file_ops,
          g_sock_ops,
          g_scratch);
    dprintf(1, "  entering stress loop (%s)\n", g_rounds == 0 ? "forever" : "finite");

  round = 0;
  while(g_rounds == 0 || round < g_rounds){
    int fail_round;
    int vm_fail;
    int pipe_fail;
    int vfs_fail;
    int proc_fail;
    int net_fail;
    int meta_fail;
    char meminfo[MAX_TEXT];
    int memfree;
    int pagesfree;

    vm_fail = stress_vm_fork(round);
    pipe_fail = stress_pipe_poll(round);
    vfs_fail = stress_vfs_files(round);
    proc_fail = stress_procfs_and_dir();
    net_fail = stress_net_socket(round);
    meta_fail = stress_kernel_meta_apis();

    fail_round = vm_fail + pipe_fail + vfs_fail + proc_fail + net_fail + meta_fail;

    memfree = -1;
    pagesfree = -1;
    if(read_text("/proc/meminfo", meminfo, sizeof(meminfo)) > 0){
      memfree = parse_kv_uint(meminfo, "MemFree:");
      pagesfree = parse_kv_uint(meminfo, "PagesFree:");
    }

    if(fail_round == 0){
      dprintf(1,
              "[round %d] PASS vm=%d pipe=%d vfs=%d proc=%d net=%d meta=%d memfree=%dKB pagesfree=%d uptime=%u\n",
              round,
              vm_fail, pipe_fail, vfs_fail, proc_fail, net_fail, meta_fail,
              memfree, pagesfree, uptime());
    } else {
      g_fail_total += fail_round;
      dprintf(1,
              "[round %d] FAIL vm=%d pipe=%d vfs=%d proc=%d net=%d meta=%d total_fail=%d memfree=%dKB pagesfree=%d uptime=%u\n",
              round,
              vm_fail, pipe_fail, vfs_fail, proc_fail, net_fail, meta_fail,
              g_fail_total,
              memfree, pagesfree, uptime());
    }

    if(g_verbose || (round % PROC_SAMPLE_ROUNDS) == 0)
      proc_snapshot(round);

    round++;
  }

  dprintf(1, "kmemstress: done rounds=%d fail_total=%d warn_total=%d\n",
          round, g_fail_total, g_warn_total);

  exit(g_fail_total ? 1 : 0);
}
