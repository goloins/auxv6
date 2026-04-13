#include "types.h"
#include "time.h"
#include "auxv6/user.h"
#include "socket.h"
#include "net.h"
#include "signal.h"

static volatile int ping_stop = 0;

static void
ping_sigint(int sig)
{
  (void)sig;
  ping_stop = 1;
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

static ushort
icmp_csum(void *buf, uint len)
{
  ushort *w;
  uint sum;

  w = (ushort*)buf;
  sum = 0;
  while(len > 1) {
    sum += *w++;
    len -= 2;
  }
  if(len)
    sum += *(uchar*)w;

  while(sum >> 16)
    sum = (sum & 0xffff) + (sum >> 16);
  return (ushort)(~sum);
}

int
main(int argc, char *argv[])
{
  struct sigaction sa;
  int fd;
  int n;
  int i;
  int off;
  int matched;
  int start_ticks;
  int timeout_ticks;
  int elapsed_ticks;
  int sent;
  int received;
  int lost;
  unsigned long long rtt_ms;
  unsigned long long min_rtt_ms;
  unsigned long long max_rtt_ms;
  unsigned long long sum_rtt_ms;
  unsigned long long avg_rtt_ms;
  uint dst_addr;
  int used_resolver;
  char buf[128];
  int pid;
  struct sockaddr_in dst;
  struct {
    struct icmp_hdr h;
    char data[16];
  } pkt;
  struct icmp_hdr *rh;
  struct timespec start_ts;
  struct timespec end_ts;

  dst_addr = INADDR_LOOPBACK;
  used_resolver = 0;
  if(argc > 2) {
    dprintf(2, "usage: ping [ipv4-or-hostname]\n");
    exit(1);
  }
  if(argc == 2) {
    if(parse_ipv4(argv[1], &dst_addr) < 0) {
      if(resolve_ipv4(argv[1], &dst_addr) < 0) {
        dprintf(2, "ping: cannot resolve %s\n", argv[1]);
        exit(1);
      }
      used_resolver = 1;
    }
  }

  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = ping_sigint;
  sigaction(SIGINT, &sa, 0);

  dprintf(1, "PING %d.%d.%d.%d: %d data bytes\n",
          (dst_addr >> 24) & 0xff, (dst_addr >> 16) & 0xff,
          (dst_addr >> 8) & 0xff, dst_addr & 0xff,
          (int)sizeof(pkt));

  if(used_resolver) {
    dprintf(1, "(%s)\n", argv[1]);
  }

  fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if(fd < 0) {
    dprintf(1, "ping: socket failed\n");
    exit(1);
  }

  memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_addr.s_addr = dst_addr;

  if(connect(fd, &dst, sizeof(dst)) < 0) {
    dprintf(1, "ping: connect failed\n");
    close(fd);
    exit(1);
  }

  pid = getpid();
  timeout_ticks = 100;  /* 1 second at 100 Hz */
  sent = 0;
  received = 0;
  min_rtt_ms = 0;
  max_rtt_ms = 0;
  sum_rtt_ms = 0;

  for(i = 1; !ping_stop; i++) {
    memset(&pkt, 0, sizeof(pkt));
    pkt.h.type = ICMP_ECHO;
    pkt.h.code = 0;
    pkt.h.ident = (ushort)pid;
    pkt.h.seq = (ushort)i;
    memmove(pkt.data, "auxv6-ping", 10);
    pkt.h.csum = 0;
    pkt.h.csum = icmp_csum(&pkt, sizeof(pkt));

    if(clock_gettime(CLOCK_MONOTONIC, &start_ts) < 0) {
      dprintf(1, "ping: clock_gettime failed\n");
      close(fd);
      exit(1);
    }
    start_ticks = uptime();
    if(send(fd, &pkt, sizeof(pkt)) < 0) {
      if(ping_stop)
        break;
      dprintf(1, "ping: send failed seq=%d\n", i);
      close(fd);
      exit(1);
    }
    sent++;

    matched = 0;
    for(;;) {
      if(ping_stop)
        break;
      elapsed_ticks = uptime() - start_ticks;
      if(elapsed_ticks >= timeout_ticks)
        break;

      n = recvtimeout(fd, buf, sizeof(buf), timeout_ticks - elapsed_ticks);
      if(clock_gettime(CLOCK_MONOTONIC, &end_ts) < 0) {
        dprintf(1, "ping: clock_gettime failed\n");
        close(fd);
        exit(1);
      }
      if(n <= 0)
        break;

      /* Raw sockets are byte-stream buffers here; scan for any matching echo reply. */
      for(off = 0; off + (int)sizeof(struct icmp_hdr) <= n; off++) {
        rh = (struct icmp_hdr*)(buf + off);
        if(rh->type == ICMP_ECHO_REPLY && rh->ident == (ushort)pid && rh->seq == (ushort)i) {
          rtt_ms = timespec_diff_msec(&start_ts, &end_ts);
          if(received == 0 || rtt_ms < min_rtt_ms)
            min_rtt_ms = rtt_ms;
          if(received == 0 || rtt_ms > max_rtt_ms)
            max_rtt_ms = rtt_ms;
          sum_rtt_ms += rtt_ms;
          received++;

          if(rtt_ms == 0)
            dprintf(1, "%d bytes from %d.%d.%d.%d: icmp_seq=%d time<1 ms\n",
                    n, (dst_addr >> 24) & 0xff, (dst_addr >> 16) & 0xff,
                    (dst_addr >> 8) & 0xff, dst_addr & 0xff, rh->seq);
          else
            dprintf(1, "%d bytes from %d.%d.%d.%d: icmp_seq=%d time=%llu ms\n",
                    n, (dst_addr >> 24) & 0xff, (dst_addr >> 16) & 0xff,
                    (dst_addr >> 8) & 0xff, dst_addr & 0xff, rh->seq, rtt_ms);
          matched = 1;
          break;
        }
      }
      if(matched)
        break;
    }

    if(!matched && !ping_stop)
      dprintf(1, "ping: request timeout for icmp_seq=%d\n", i);

    if(!ping_stop)
      sleep(10);  /* ~1 second between pings */
  }

  close(fd);

  /* Print statistics on exit (like real ping) */
  lost = sent - received;
  dprintf(1, "\n--- %d.%d.%d.%d ping statistics ---\n",
          (dst_addr >> 24) & 0xff, (dst_addr >> 16) & 0xff,
          (dst_addr >> 8) & 0xff, dst_addr & 0xff);
  dprintf(1, "%d packets transmitted, %d received, %d%% packet loss\n",
          sent, received, sent > 0 ? (lost * 100) / sent : 0);
  if(received > 0) {
    avg_rtt_ms = (sum_rtt_ms + (unsigned long long)received / 2ULL) /
                 (unsigned long long)received;
    dprintf(1, "rtt min/avg/max = %llu/%llu/%llu ms\n",
            min_rtt_ms, avg_rtt_ms, max_rtt_ms);
  }

  exit(0);
}
