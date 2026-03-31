#include "../include/types.h"
#include "../include/user.h"
#include "../include/socket.h"
#include "../include/net.h"

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

static uint
rdtsc32(void)
{
  uint lo;

  asm volatile("rdtsc" : "=a" (lo) : : "edx");
  return lo;
}

int
main(int argc, char *argv[])
{
  const int npings = 5;
  int fd;
  int n;
  int i;
  int off;
  int matched;
  int timeout_ticks;
  int t0;
  int t1;
  uint c0;
  uint c1;
  int rtt;
  uint cyc;
  int received;
  int lost;
  int min_rtt;
  int max_rtt;
  int sum_rtt;
  int avg_rtt;
  uint min_cyc;
  uint max_cyc;
  uint sum_cyc;
  uint avg_cyc;
  uint dst_addr;
  char buf[128];
  int pid;
  struct sockaddr_in dst;
  struct {
    struct icmp_hdr h;
    char data[16];
  } pkt;
  struct icmp_hdr *rh;

  dst_addr = INADDR_LOOPBACK;
  if(argc > 2) {
    printf(2, "usage: ping [ipv4]\n");
    exit();
  }
  if(argc == 2 && parse_ipv4(argv[1], &dst_addr) < 0) {
    printf(2, "ping: invalid IPv4 address\n");
    exit();
  }

  fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if(fd < 0) {
    printf(1, "ping: socket failed\n");
    exit();
  }

  memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_addr = dst_addr;

  if(connect(fd, &dst, sizeof(dst)) < 0) {
    printf(1, "ping: connect failed\n");
    close(fd);
    exit();
  }

  pid = getpid();
  timeout_ticks = 50;
  received = 0;
  min_rtt = 0;
  max_rtt = 0;
  sum_rtt = 0;
  min_cyc = 0;
  max_cyc = 0;
  sum_cyc = 0;

  for(i = 1; i <= npings; i++) {
    memset(&pkt, 0, sizeof(pkt));
    pkt.h.type = ICMP_ECHO;
    pkt.h.code = 0;
    pkt.h.ident = (ushort)pid;
    pkt.h.seq = (ushort)i;
    memmove(pkt.data, "auxv6-ping", 10);
    pkt.h.csum = 0;
    pkt.h.csum = icmp_csum(&pkt, sizeof(pkt));

    t0 = uptime();
    c0 = rdtsc32();
    if(send(fd, &pkt, sizeof(pkt)) < 0) {
      printf(1, "ping: send failed seq=%d\n", i);
      close(fd);
      exit();
    }

    n = recvtimeout(fd, buf, sizeof(buf), timeout_ticks);
    c1 = rdtsc32();
    t1 = uptime();
    if(n <= 0) {
      printf(1, "ping: timeout seq=%d\n", i);
      continue;
    }

    matched = 0;
    // Raw sockets are byte-stream buffers here; scan for any matching echo reply.
    for(off = 0; off + (int)sizeof(struct icmp_hdr) <= n; off++) {
      rh = (struct icmp_hdr*)(buf + off);
      if(rh->type == ICMP_ECHO_REPLY && rh->ident == (ushort)pid && rh->seq == (ushort)i) {
        rtt = t1 - t0;
        cyc = c1 - c0;
        if(received == 0 || rtt < min_rtt)
          min_rtt = rtt;
        if(received == 0 || rtt > max_rtt)
          max_rtt = rtt;
        sum_rtt += rtt;
        if(received == 0 || cyc < min_cyc)
          min_cyc = cyc;
        if(received == 0 || cyc > max_cyc)
          max_cyc = cyc;
        sum_cyc += cyc;
        received++;

        if(rtt == 0)
          printf(1, "ping: PASS bytes=%d seq=%d time=<1 tick cycles=%u\n", n, rh->seq, cyc);
        else
          printf(1, "ping: PASS bytes=%d seq=%d time=%d ticks cycles=%u\n", n, rh->seq, rtt, cyc);
        matched = 1;
        break;
      }
    }
    if(!matched)
      printf(1, "ping: no matching echo reply seq=%d\n", i);

    if(i < npings)
      sleep(10);
  }

  lost = npings - received;
  printf(1, "ping: sent=%d received=%d lost=%d loss=%d%%\n",
         npings, received, lost, (lost * 100) / npings);
  if(received > 0) {
    avg_rtt = (sum_rtt + received / 2) / received;
      avg_cyc = (sum_cyc + (uint)received / 2) / (uint)received;
    printf(1, "ping: rtt min/avg/max = %d/%d/%d ticks\n",
           min_rtt, avg_rtt, max_rtt);
      printf(1, "ping: cycles min/avg/max = %u/%u/%u\n",
        min_cyc, avg_cyc, max_cyc);
  }

  close(fd);
  exit();
}
