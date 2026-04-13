#include "types.h"
#include "stat.h"
#include "auxv6/user.h"
#include "fcntl.h"
#include "signal.h"
#include "socket.h"
#include "time.h"
#include "net.h"

#define NTP_PORT                 123
#define NTP_PACKET_LEN           48
#define NTP_UNIX_EPOCH_DELTA     2208988800U
#define NTP_DEFAULT_SERVER       "pool.ntp.org"
#define NTP_DEFAULT_INTERVAL_SEC 900
#define NTP_RETRY_INTERVAL_SEC   30
#define NTP_TIMEOUT_TICKS        500
#define AUXV6_HZ                 100

typedef unsigned long long u64;

struct ntp_packet {
  uchar li_vn_mode;
  uchar stratum;
  uchar poll;
  uchar precision;
  uint root_delay;
  uint root_dispersion;
  uint ref_id;
  uint ref_ts_sec;
  uint ref_ts_frac;
  uint orig_ts_sec;
  uint orig_ts_frac;
  uint recv_ts_sec;
  uint recv_ts_frac;
  uint tx_ts_sec;
  uint tx_ts_frac;
} __attribute__((packed));

static volatile sig_atomic_t keep_running = 1;

static void
usage(void)
{
  dprintf(2, "usage: ntpd [-f] [-i interval_seconds] [server]\n");
  dprintf(2, "       ntpd defaults: server=%s interval=%d\n",
          NTP_DEFAULT_SERVER, NTP_DEFAULT_INTERVAL_SEC);
  exit(1);
}

static int
parse_ipv4(const char *s, uint *out)
{
  int i;
  int part;
  uint ip;

  if(s == 0 || out == 0)
    return -1;

  ip = 0;
  for(i = 0; i < 4; i++) {
    if(*s < '0' || *s > '9')
      return -1;

    part = 0;
    while(*s >= '0' && *s <= '9') {
      part = part * 10 + (*s - '0');
      if(part > 255)
        return -1;
      s++;
    }

    ip = (ip << 8) | (uint)part;
    if(i < 3) {
      if(*s != '.')
        return -1;
      s++;
    }
  }

  if(*s != '\0')
    return -1;

  *out = ip;
  return 0;
}

static void
log_addr(uint ip)
{
  dprintf(1, "%d.%d.%d.%d",
          (ip >> 24) & 0xff,
          (ip >> 16) & 0xff,
          (ip >> 8) & 0xff,
          ip & 0xff);
}

static int
resolve_server(const char *server, uint *out_ip)
{
  if(parse_ipv4(server, out_ip) == 0)
    return 0;
  return resolve_ipv4(server, out_ip);
}

static int
daemonize_self(void)
{
  int pid;
  int fd;

  pid = fork();
  if(pid < 0)
    return -1;
  if(pid > 0)
    exit(0);

  if(setsid() < 0)
    return -1;

  pid = fork();
  if(pid < 0)
    return -1;
  if(pid > 0)
    exit(0);

  chdir("/");

  close(0);
  close(1);
  close(2);

  fd = open("/dev/console", O_RDWR);
  if(fd < 0)
    return 0;

  if(fd != 0) {
    dup2(fd, 0);
    close(fd);
  }
  dup(0);
  dup(0);
  return 0;
}

static void
on_term(int signo)
{
  if(signo == SIGTERM || signo == SIGINT)
    keep_running = 0;
}

static int
sync_once(uint server_ip, const char *server_name, int verbose)
{
  int fd;
  int n;
  struct sockaddr_in dst;
  struct ntp_packet pkt;
  uint tx_sec;
  uint tx_frac;
  struct timespec ts;

  fd = socket(AF_INET, SOCK_DGRAM, 0);
  if(fd < 0)
    return -1;

  memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_port = (ushort)NTP_PORT;
  dst.sin_addr = server_ip;

  if(connect(fd, &dst, sizeof(dst)) < 0) {
    close(fd);
    return -1;
  }

  memset(&pkt, 0, sizeof(pkt));
  pkt.li_vn_mode = 0x23;  /* LI=0, VN=4, Mode=3 (client) */

  if(send(fd, &pkt, sizeof(pkt)) != sizeof(pkt)) {
    close(fd);
    return -1;
  }

  n = recvtimeout(fd, &pkt, sizeof(pkt), NTP_TIMEOUT_TICKS);
  close(fd);
  if(n < (int)sizeof(pkt))
    return -1;

  if((pkt.li_vn_mode & 0x7) != 4 && (pkt.li_vn_mode & 0x7) != 5)
    return -1;
  if(pkt.stratum == 0)
    return -1;

  tx_sec = net_ntohl(pkt.tx_ts_sec);
  tx_frac = net_ntohl(pkt.tx_ts_frac);
  if(tx_sec < NTP_UNIX_EPOCH_DELTA)
    return -1;

  ts.tv_sec = (time_t)(tx_sec - NTP_UNIX_EPOCH_DELTA);
  ts.tv_nsec = (long)(((u64)tx_frac * 1000000000ULL) >> 32);

  if(clock_settime(CLOCK_REALTIME, &ts) < 0)
    return -1;

  if(verbose) {
    dprintf(1, "ntpd: synced from %s (", server_name);
    log_addr(server_ip);
    dprintf(1, ") -> %d.%09d\n", (int)ts.tv_sec, (int)ts.tv_nsec);
  }

  return 0;
}

int
main(int argc, char **argv)
{
  int i;
  int foreground;
  int interval_sec;
  const char *server;
  uint server_ip;
  int server_is_ip;
  struct sigaction sa;

  foreground = 0;
  interval_sec = NTP_DEFAULT_INTERVAL_SEC;
  server = NTP_DEFAULT_SERVER;
  server_is_ip = 0;

  for(i = 1; i < argc; i++) {
    if(strcmp(argv[i], "-f") == 0) {
      foreground = 1;
      continue;
    }
    if(strcmp(argv[i], "-i") == 0) {
      if(i + 1 >= argc)
        usage();
      interval_sec = atoi(argv[++i]);
      if(interval_sec < 30)
        interval_sec = 30;
      continue;
    }
    if(argv[i][0] == '-')
      usage();
    server = argv[i];
  }

  if(getuid() != 0) {
    dprintf(2, "ntpd: requires root privileges\n");
    exit(1);
  }

  if(parse_ipv4(server, &server_ip) == 0)
    server_is_ip = 1;

  dprintf(1, "ntpd: launch mode=%s server=%s interval=%d\n",
          foreground ? "foreground" : "daemon",
          server,
          interval_sec);

  if(!foreground) {
    if(daemonize_self() < 0)
      exit(1);
  }

  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = on_term;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGTERM, &sa, 0);
  sigaction(SIGINT, &sa, 0);

  while(keep_running) {
    int ok;
    int sleep_ticks;

    if(server_is_ip || resolve_server(server, &server_ip) == 0)
      ok = (sync_once(server_ip, server, foreground) == 0);
    else
      ok = 0;

    if(ok)
      sleep_ticks = interval_sec * AUXV6_HZ;
    else
      sleep_ticks = NTP_RETRY_INTERVAL_SEC * AUXV6_HZ;

    while(keep_running && sleep_ticks > 0) {
      int chunk;

      chunk = sleep_ticks;
      if(chunk > AUXV6_HZ)
        chunk = AUXV6_HZ;
      sleep(chunk);
      sleep_ticks -= chunk;
    }
  }

  return 0;
}
