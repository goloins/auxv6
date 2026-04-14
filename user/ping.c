#include "types.h"
#include "time.h"
#include "auxv6/user.h"
#include "socket.h"
#include "net.h"
#include "signal.h"

#define PING_TICK_HZ             100
#define PING_DEFAULT_PAYLOAD     56
#define PING_DEFAULT_TIMEOUT_MS  1000
#define PING_DEFAULT_INTERVAL_MS 1000
#define PING_MAX_PAYLOAD         1400

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

static void
print_usage(void)
{
  dprintf(2,
          "usage: ping [-4anqv] [-c count] [-i interval] [-W timeout] [-w deadline] [-s packetsize] [-t ttl] destination\n");
  dprintf(2,
          "       ping -h\n");
}

static const char *
icmp_type_name(int type)
{
  if(type == ICMP_ECHO_REPLY)
    return "echo-reply";
  if(type == ICMP_UNREACH)
    return "dest-unreach";
  if(type == ICMP_TIMXCEED)
    return "time-exceeded";
  if(type == ICMP_ECHO)
    return "echo";
  return "icmp";
}

static unsigned long long
isqrt_u64(unsigned long long x)
{
  unsigned long long lo;
  unsigned long long hi;
  unsigned long long ans;

  lo = 0ULL;
  hi = x;
  ans = 0ULL;
  while(lo <= hi) {
    unsigned long long mid;
    unsigned long long q;

    mid = lo + ((hi - lo) >> 1);
    if(mid == 0ULL) {
      ans = 0ULL;
      lo = 1ULL;
      continue;
    }
    q = x / mid;
    if(mid <= q) {
      ans = mid;
      lo = mid + 1ULL;
    } else {
      hi = mid - 1ULL;
    }
  }
  return ans;
}
static int
parse_u32(const char *s, uint *out)
{
  uint v;

  if(s == 0 || *s == '\0' || out == 0)
    return -1;

  v = 0;
  while(*s) {
    if(*s < '0' || *s > '9')
      return -1;
    v = v * 10U + (uint)(*s - '0');
    s++;
  }

  *out = v;
  return 0;
}

/* Parse seconds with optional decimal fraction into milliseconds. */
static int
parse_seconds_to_msec(const char *s, uint *out)
{
  uint sec;
  uint frac;
  uint frac_digits;
  uint scale;

  if(s == 0 || *s == '\0' || out == 0)
    return -1;

  sec = 0;
  while(*s && *s != '.') {
    if(*s < '0' || *s > '9')
      return -1;
    sec = sec * 10U + (uint)(*s - '0');
    s++;
  }

  frac = 0;
  frac_digits = 0;
  if(*s == '.') {
    s++;
    while(*s) {
      if(*s < '0' || *s > '9')
        return -1;
      if(frac_digits < 3) {
        frac = frac * 10U + (uint)(*s - '0');
        frac_digits++;
      }
      s++;
    }
  }

  scale = 1;
  while(frac_digits < 3) {
    scale *= 10U;
    frac_digits++;
  }

  *out = sec * 1000U + frac * scale;
  return 0;
}

static int
msec_to_ticks(uint msec)
{
  uint t;

  t = (msec * PING_TICK_HZ + 999U) / 1000U;
  if(t == 0)
    t = 1;
  return (int)t;
}

static void
print_ip(uint ip)
{
  dprintf(1, "%d.%d.%d.%d",
          (ip >> 24) & 0xff, (ip >> 16) & 0xff,
          (ip >> 8) & 0xff, ip & 0xff);
}

int
main(int argc, char *argv[])
{
  struct sigaction sa;
  int fd;
  int n;
  int i;
  int argi;
  int ipv4_only;
  int audible;
  int is_numeric;
  int quiet;
  int verbose;
  int have_deadline;
  int deadline_ticks;
  int deadline_start;
  int count;
  int interval_ticks;
  int off;
  int matched;
  int start_ticks;
  int timeout_ticks;
  int elapsed_ticks;
  int seq;
  int data_len;
  int pkt_len;
  int ttl;
  int have_ttl;
  int sent;
  int received;
  int lost;
  unsigned long long rtt_ms;
  unsigned long long min_rtt_ms;
  unsigned long long max_rtt_ms;
  unsigned long long sum_rtt_ms;
  unsigned long long sum_sq_rtt_ms;
  unsigned long long avg_rtt_ms;
  unsigned long long mdev_ms;
  uint dst_addr;
  uint nval;
  uint msec;
  char *target;
  char *pkt;
  struct icmp_hdr *ph;
  char buf[128];
  int pid;
  struct sockaddr_in dst;
  struct icmp_hdr *rh;
  struct timespec start_ts;
  struct timespec end_ts;

  if(argc <= 1) {
    print_usage();
    exit(1);
  }

  ipv4_only = 0;
  audible = 0;
  is_numeric = 0;
  quiet = 0;
  verbose = 0;
  have_deadline = 0;
  deadline_ticks = 0;
  count = -1;
  interval_ticks = msec_to_ticks(PING_DEFAULT_INTERVAL_MS);
  timeout_ticks = msec_to_ticks(PING_DEFAULT_TIMEOUT_MS);
  data_len = PING_DEFAULT_PAYLOAD;
  have_ttl = 0;
  ttl = 64;
  target = 0;

  for(argi = 1; argi < argc; argi++) {
    char *a;

    a = argv[argi];
    if(a[0] != '-' || a[1] == '\0') {
      if(target != 0) {
        print_usage();
        exit(1);
      }
      target = a;
      continue;
    }

    if(strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
      print_usage();
      exit(0);
    } else if(strcmp(a, "-4") == 0) {
      ipv4_only = 1;
      (void)ipv4_only;
    } else if(strcmp(a, "-a") == 0) {
      audible = 1;
    } else if(strcmp(a, "-n") == 0) {
      is_numeric = 1;
    } else if(strcmp(a, "-q") == 0) {
      quiet = 1;
    } else if(strcmp(a, "-v") == 0) {
      verbose = 1;
    } else if(strcmp(a, "-c") == 0 || strcmp(a, "-i") == 0 ||
              strcmp(a, "-W") == 0 || strcmp(a, "-w") == 0 ||
              strcmp(a, "-s") == 0 || strcmp(a, "-t") == 0) {
      if(argi + 1 >= argc) {
        print_usage();
        exit(1);
      }
      argi++;
      if(strcmp(a, "-c") == 0) {
        if(parse_u32(argv[argi], &nval) < 0 || nval == 0U) {
          dprintf(2, "ping: invalid count: %s\n", argv[argi]);
          exit(1);
        }
        count = (int)nval;
      } else if(strcmp(a, "-i") == 0) {
        if(parse_seconds_to_msec(argv[argi], &msec) < 0 || msec == 0U) {
          dprintf(2, "ping: invalid interval: %s\n", argv[argi]);
          exit(1);
        }
        interval_ticks = msec_to_ticks(msec);
      } else if(strcmp(a, "-W") == 0) {
        if(parse_seconds_to_msec(argv[argi], &msec) < 0 || msec == 0U) {
          dprintf(2, "ping: invalid timeout: %s\n", argv[argi]);
          exit(1);
        }
        timeout_ticks = msec_to_ticks(msec);
      } else if(strcmp(a, "-w") == 0) {
        if(parse_seconds_to_msec(argv[argi], &msec) < 0 || msec == 0U) {
          dprintf(2, "ping: invalid deadline: %s\n", argv[argi]);
          exit(1);
        }
        have_deadline = 1;
        deadline_ticks = msec_to_ticks(msec);
      } else if(strcmp(a, "-s") == 0) {
        if(parse_u32(argv[argi], &nval) < 0 || nval > (uint)PING_MAX_PAYLOAD) {
          dprintf(2, "ping: invalid packetsize: %s (max %d)\n", argv[argi], PING_MAX_PAYLOAD);
          exit(1);
        }
        data_len = (int)nval;
      } else if(strcmp(a, "-t") == 0) {
        if(parse_u32(argv[argi], &nval) < 0 || nval == 0U || nval > 255U) {
          dprintf(2, "ping: invalid ttl: %s\n", argv[argi]);
          exit(1);
        }
        have_ttl = 1;
        ttl = (int)nval;
      }
    } else {
      dprintf(2, "ping: unknown option: %s\n", a);
      print_usage();
      exit(1);
    }
  }

  if(target == 0) {
    print_usage();
    exit(1);
  }

  if(parse_ipv4(target, &dst_addr) < 0) {
    if(is_numeric || resolve_ipv4(target, &dst_addr) < 0) {
      dprintf(2, "ping: cannot resolve %s\n", target);
      exit(1);
    }
  }

  pkt_len = (int)sizeof(struct icmp_hdr) + data_len;
  pkt = malloc(pkt_len);
  if(pkt == 0) {
    dprintf(2, "ping: out of memory\n");
    exit(1);
  }

  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = ping_sigint;
  sigaction(SIGINT, &sa, 0);

  if(!quiet) {
    dprintf(1, "PING %s (", target);
    print_ip(dst_addr);
    dprintf(1, "): %d data bytes\n", data_len);
  }

  fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if(fd < 0) {
    dprintf(1, "ping: socket failed\n");
    free(pkt);
    exit(1);
  }

  memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_addr.s_addr = dst_addr;

  if(have_ttl) {
    if(setsockopt(fd, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl)) < 0) {
      dprintf(2, "ping: setsockopt IP_TTL failed\n");
      close(fd);
      free(pkt);
      exit(1);
    }
  }

  if(connect(fd, &dst, sizeof(dst)) < 0) {
    dprintf(1, "ping: connect failed\n");
    close(fd);
    free(pkt);
    exit(1);
  }

  pid = getpid();
  sent = 0;
  received = 0;
  min_rtt_ms = 0;
  max_rtt_ms = 0;
  sum_rtt_ms = 0;
  sum_sq_rtt_ms = 0;
  seq = 1;
  deadline_start = uptime();

  for(; !ping_stop; seq++) {
    if(count > 0 && sent >= count)
      break;
    if(have_deadline && uptime() - deadline_start >= deadline_ticks)
      break;

    memset(pkt, 0, pkt_len);
    ph = (struct icmp_hdr*)pkt;
    ph->type = ICMP_ECHO;
    ph->code = 0;
    ph->ident = (ushort)pid;
    ph->seq = (ushort)seq;
    for(i = 0; i < data_len; i++)
      pkt[sizeof(struct icmp_hdr) + i] = (char)('a' + (i % 26));
    ph->csum = 0;
    ph->csum = icmp_csum(pkt, (uint)pkt_len);

    if(clock_gettime(CLOCK_MONOTONIC, &start_ts) < 0) {
      dprintf(1, "ping: clock_gettime failed\n");
      close(fd);
      free(pkt);
      exit(1);
    }
    start_ticks = uptime();
    if(send(fd, pkt, pkt_len) < 0) {
      if(ping_stop)
        break;
      dprintf(1, "ping: send failed seq=%d\n", seq);
      close(fd);
      free(pkt);
      exit(1);
    }
    sent++;

    matched = 0;
    for(;;) {
      int icmp_verbose_printed;

      icmp_verbose_printed = 0;
      if(ping_stop)
        break;
      elapsed_ticks = uptime() - start_ticks;
      if(elapsed_ticks >= timeout_ticks)
        break;

      n = recvtimeout(fd, buf, sizeof(buf), timeout_ticks - elapsed_ticks);
      if(clock_gettime(CLOCK_MONOTONIC, &end_ts) < 0) {
        dprintf(1, "ping: clock_gettime failed\n");
        close(fd);
        free(pkt);
        exit(1);
      }
      if(n <= 0)
        break;

      /* Raw sockets are byte-stream buffers here; scan for any matching echo reply. */
      for(off = 0; off + (int)sizeof(struct icmp_hdr) <= n; off++) {
        rh = (struct icmp_hdr*)(buf + off);
        if(rh->type == ICMP_ECHO_REPLY && rh->ident == (ushort)pid && rh->seq == (ushort)seq) {
          rtt_ms = timespec_diff_msec(&start_ts, &end_ts);
          if(received == 0 || rtt_ms < min_rtt_ms)
            min_rtt_ms = rtt_ms;
          if(received == 0 || rtt_ms > max_rtt_ms)
            max_rtt_ms = rtt_ms;
          sum_rtt_ms += rtt_ms;
          sum_sq_rtt_ms += rtt_ms * rtt_ms;
          received++;

          if(audible)
            write(1, "\a", 1);

          if(!quiet) {
            if(rtt_ms == 0)
              dprintf(1, "%d bytes from %d.%d.%d.%d: icmp_seq=%d time<1 ms\n",
                      n, (dst_addr >> 24) & 0xff, (dst_addr >> 16) & 0xff,
                      (dst_addr >> 8) & 0xff, dst_addr & 0xff, rh->seq);
            else
              dprintf(1, "%d bytes from %d.%d.%d.%d: icmp_seq=%d time=%llu ms\n",
                      n, (dst_addr >> 24) & 0xff, (dst_addr >> 16) & 0xff,
                      (dst_addr >> 8) & 0xff, dst_addr & 0xff, rh->seq, rtt_ms);
          }
          matched = 1;
          break;
        } else if(verbose && !icmp_verbose_printed) {
          dprintf(1, "ping: icmp %s type=%d code=%d\n",
                  icmp_type_name(rh->type), rh->type, rh->code);
          icmp_verbose_printed = 1;
        }
      }
      if(matched)
        break;
    }

    if(!matched && !ping_stop && !quiet)
      dprintf(1, "ping: request timeout for icmp_seq=%d\n", seq);

    if(!ping_stop)
      sleep(interval_ticks);
  }

  close(fd);
  free(pkt);

  /* Print statistics on exit (like real ping) */
  lost = sent - received;
  dprintf(1, "\n--- ");
  print_ip(dst_addr);
  dprintf(1, " ping statistics ---\n");
  dprintf(1, "%d packets transmitted, %d received, %d%% packet loss\n",
          sent, received, sent > 0 ? (lost * 100) / sent : 0);
  if(received > 0) {
        unsigned long long mean_sq;
        unsigned long long avg_sq;
        unsigned long long var;

    avg_rtt_ms = (sum_rtt_ms + (unsigned long long)received / 2ULL) /
                 (unsigned long long)received;

        mean_sq = (sum_sq_rtt_ms + (unsigned long long)received / 2ULL) /
            (unsigned long long)received;
        avg_sq = avg_rtt_ms * avg_rtt_ms;
        var = mean_sq > avg_sq ? (mean_sq - avg_sq) : 0ULL;
        mdev_ms = isqrt_u64(var);

        dprintf(1, "rtt min/avg/max/mdev = %llu/%llu/%llu/%llu ms\n",
          min_rtt_ms, avg_rtt_ms, max_rtt_ms, mdev_ms);
  }

  exit(0);
}
