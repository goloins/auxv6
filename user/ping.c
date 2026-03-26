#include "../include/types.h"
#include "../include/user.h"
#include "../include/socket.h"
#include "../include/net.h"

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
  int fd;
  int n;
  int tries;
  char buf[128];
  int pid;
  struct sockaddr_in dst;
  struct {
    struct icmp_hdr h;
    char data[16];
  } pkt;
  struct icmp_hdr *rh;

  (void)argc;
  (void)argv;

  fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if(fd < 0) {
    printf(1, "ping: socket failed\n");
    exit();
  }

  memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_addr = INADDR_LOOPBACK;

  if(connect(fd, &dst, sizeof(dst)) < 0) {
    printf(1, "ping: connect failed\n");
    close(fd);
    exit();
  }

  pid = getpid();
  memset(&pkt, 0, sizeof(pkt));
  pkt.h.type = ICMP_ECHO;
  pkt.h.code = 0;
  pkt.h.ident = (ushort)pid;
  pkt.h.seq = 1;
  memmove(pkt.data, "auxv6-ping", 10);
  pkt.h.csum = 0;
  pkt.h.csum = icmp_csum(&pkt, sizeof(pkt));

  if(send(fd, &pkt, sizeof(pkt)) < 0) {
    printf(1, "ping: send failed\n");
    close(fd);
    exit();
  }

  for(tries = 0; tries < 4; tries++) {
    n = recv(fd, buf, sizeof(buf));
    if(n < (int)sizeof(struct icmp_hdr))
      continue;

    rh = (struct icmp_hdr*)buf;
    if(rh->type == ICMP_ECHO_REPLY && rh->ident == (ushort)pid) {
      printf(1, "ping: PASS bytes=%d seq=%d\n", n, rh->seq);
      close(fd);
      exit();
    }
  }

  printf(1, "ping: no echo reply\n");
  close(fd);
  exit();
}
