/*
 * traceroute - trace the route to a host by sending ICMP ECHO probes with
 * increasing TTL values and listening for ICMP TIME_EXCEEDED responses from
 * intermediate hops and ICMP ECHO_REPLY from the destination.
 */

#include "types.h"
#include "time.h"
#include "auxv6/user.h"
#include "socket.h"
#include "net.h"
#include "signal.h"
#include "poll.h"

#define TR_MAX_HOPS    30
#define TR_NPROBES     3
#define TR_TIMEOUT_MS  1000   /* 1 second per probe */

static volatile int tr_stop = 0;

static void
tr_sigint(int sig)
{
  (void)sig;
  tr_stop = 1;
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
  int ttl;
  int probe;
  int done;
  int seq;
  int max_hops;
  int pid;
  uint dst_addr;
  struct sockaddr_in dst;
  struct {
    struct icmp_hdr h;
    char data[16];
  } pkt;
  char buf[256];

  if(argc < 2 || argc > 3) {
    dprintf(2, "usage: traceroute host [max_hops]\n");
    exit(1);
  }

  if(parse_ipv4(argv[1], &dst_addr) < 0) {
    if(resolve_ipv4(argv[1], &dst_addr) < 0) {
      dprintf(2, "traceroute: cannot resolve %s\n", argv[1]);
      exit(1);
    }
  }

  max_hops = TR_MAX_HOPS;
  if(argc == 3) {
    max_hops = atoi(argv[2]);
    if(max_hops < 1 || max_hops > 255)
      max_hops = TR_MAX_HOPS;
  }

  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = tr_sigint;
  sigaction(SIGINT, &sa, 0);

  fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if(fd < 0) {
    dprintf(2, "traceroute: socket failed\n");
    exit(1);
  }

  memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_addr = dst_addr;

  pid = getpid();
  seq = 1;
  done = 0;

  dprintf(1, "traceroute to %d.%d.%d.%d, %d hops max, %d byte packets\n",
          (dst_addr >> 24) & 0xff, (dst_addr >> 16) & 0xff,
          (dst_addr >> 8) & 0xff, dst_addr & 0xff,
          max_hops, (int)sizeof(pkt));

  for(ttl = 1; ttl <= max_hops && !done && !tr_stop; ttl++) {
    int v;

    v = ttl;
    if(setsockopt(fd, IPPROTO_IP, IP_TTL, &v, sizeof(v)) < 0) {
      dprintf(2, "traceroute: setsockopt IP_TTL failed\n");
      break;
    }

    dprintf(1, "%2d", ttl);

    for(probe = 0; probe < TR_NPROBES && !tr_stop; probe++) {
      struct timespec t0;
      struct timespec t1;
      struct sockaddr_in peer;
      int peerlen;
      int n;
      int off;
      struct pollfd pfd;
      int ready;
      unsigned long long elapsed_ms;
      unsigned long long rtt_ms;
      uint hop_addr;
      int got_reply;
      int reply_type;

      /* Build ICMP ECHO probe */
      memset(&pkt, 0, sizeof(pkt));
      pkt.h.type = ICMP_ECHO;
      pkt.h.code = 0;
      pkt.h.ident = (ushort)pid;
      pkt.h.seq = (ushort)seq;
      memmove(pkt.data, "traceroute", 10);
      pkt.h.csum = 0;
      pkt.h.csum = icmp_csum(&pkt, sizeof(pkt));

      clock_gettime(CLOCK_MONOTONIC, &t0);

      if(sendto(fd, &pkt, sizeof(pkt), 0, &dst, sizeof(dst)) < 0) {
        dprintf(1, " !S");
        seq++;
        continue;
      }

      /* Wait for a matching response within the timeout window. */
      got_reply = 0;
      reply_type = 0;
      hop_addr = 0;
      rtt_ms = 0;

      while(!tr_stop) {
        clock_gettime(CLOCK_MONOTONIC, &t1);
        elapsed_ms = timespec_diff_msec(&t0, &t1);
        if(elapsed_ms >= TR_TIMEOUT_MS)
          break;

        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        ready = poll(&pfd, 1, (int)(TR_TIMEOUT_MS - elapsed_ms));
        if(ready <= 0)
          break;

        memset(&peer, 0, sizeof(peer));
        peerlen = sizeof(peer);
        n = recvfrom(fd, buf, sizeof(buf), 0, &peer, &peerlen);
        if(n < (int)sizeof(struct icmp_hdr))
          continue;

        clock_gettime(CLOCK_MONOTONIC, &t1);
        rtt_ms = timespec_diff_msec(&t0, &t1);

        /*
         * Scan for a matching ICMP packet in the received buffer.
         * The raw socket may concatenate multiple ICMP payloads;
         * walk the buffer looking for the one matching our probe.
         */
        for(off = 0; off + (int)sizeof(struct icmp_hdr) <= n; off++) {
          struct icmp_hdr *ih = (struct icmp_hdr *)(buf + off);

          if(ih->type == ICMP_ECHO_REPLY) {
            if(ih->ident == (ushort)pid && ih->seq == (ushort)seq) {
              hop_addr = peer.sin_addr;
              reply_type = ICMP_ECHO_REPLY;
              got_reply = 1;
            }
          } else if(ih->type == ICMP_TIMXCEED &&
                    ih->code == ICMP_TIMXCEED_INTRANS) {
            /*
             * TIME_EXCEEDED payload:
             *   [0..7]  ICMP TIME_EXCEEDED header (ident+seq are unused/zero)
             *   [8..IHL+7] Original IP header
             *   [IHL+8..IHL+15] First 8 bytes of original IP payload (= original ICMP ECHO header)
             */
            int base = off + (int)sizeof(struct icmp_hdr);
            if(base + (int)sizeof(struct ip_hdr) <= n) {
              struct ip_hdr *orig_ip = (struct ip_hdr *)(buf + base);
              int orig_ihl = (orig_ip->vhl & 0x0f) * 4;
              int inner_off = base + orig_ihl;
              if(n - inner_off >= (int)sizeof(struct icmp_hdr)) {
                struct icmp_hdr *inner = (struct icmp_hdr *)(buf + inner_off);
                if(inner->ident == (ushort)pid && inner->seq == (ushort)seq) {
                  hop_addr = peer.sin_addr;
                  reply_type = ICMP_TIMXCEED;
                  got_reply = 1;
                }
              }
            }
          }

          if(got_reply)
            break;
        }

        if(got_reply)
          break;
      }

      if(got_reply) {
        if(rtt_ms == 0)
          dprintf(1, "  %d.%d.%d.%d <1 ms",
                  (hop_addr >> 24) & 0xff, (hop_addr >> 16) & 0xff,
                  (hop_addr >> 8) & 0xff, hop_addr & 0xff);
        else
          dprintf(1, "  %d.%d.%d.%d %llu ms",
                  (hop_addr >> 24) & 0xff, (hop_addr >> 16) & 0xff,
                  (hop_addr >> 8) & 0xff, hop_addr & 0xff, rtt_ms);
        if(reply_type == ICMP_ECHO_REPLY)
          done = 1;
      } else {
        dprintf(1, "  *");
      }

      seq++;
    }

    dprintf(1, "\n");

    if(done)
      break;
  }

  if(!done && !tr_stop)
    dprintf(1, "traceroute: max hops reached without reply\n");

  close(fd);
  exit(0);
}
