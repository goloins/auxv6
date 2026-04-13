#include "types.h"
#include "auxv6/user.h"
#include "fcntl.h"
#include "stdio.h"
#include "string.h"
#include "sys/ioctl.h"

#define TUNTEST_TIMEOUT_MS 1000
#define TUNTEST_PAYLOAD "auxv6-tuntest"
#define ETHERTYPE_ARP 0x0806
#define ARP_HW_ETHER 1
#define ARP_PROTO_IP 0x0800
#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY 2

struct tuntest_echo_pkt {
  struct ip_hdr ip;
  struct icmp_hdr icmp;
  char payload[sizeof(TUNTEST_PAYLOAD)];
} __attribute__((packed));

struct tuntest_eth_hdr {
  uchar dst[ETH_ADDR_LEN];
  uchar src[ETH_ADDR_LEN];
  ushort type;
} __attribute__((packed));

struct tuntest_arp_hdr {
  ushort htype;
  ushort ptype;
  uchar hlen;
  uchar plen;
  ushort oper;
} __attribute__((packed));

struct tuntest_arp_eth_ipv4 {
  struct tuntest_arp_hdr hdr;
  uchar sha[ETH_ADDR_LEN];
  uchar spa[4];
  uchar tha[ETH_ADDR_LEN];
  uchar tpa[4];
} __attribute__((packed));

struct tuntest_tap_arp_pkt {
  struct tuntest_eth_hdr eth;
  struct tuntest_arp_eth_ipv4 arp;
} __attribute__((packed));

static void
usage(void)
{
  dprintf(2, "usage: tuntest nonblock-empty <ifname>\n");
  dprintf(2, "       tuntest poll-empty <ifname> [timeout-ms]\n");
  dprintf(2, "       tuntest icmp-self <ifname> <local-ip> <peer-ip>\n");
  dprintf(2, "       tuntest run-all <ifname> <local-ip> <peer-ip>\n");
  dprintf(2, "       tuntest tap-arp-self <ifname> <local-ip> <peer-ip>\n");
  dprintf(2, "       tuntest run-all-tap <ifname> <local-ip> <peer-ip>\n");
  exit(1);
}

static ushort
net_csum(void *buf, uint len)
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

static int
mode_from_ifname(const char *ifname)
{
  if(ifname && strncmp(ifname, "tap", 3) == 0)
    return IFF_TAP;
  return IFF_TUN;
}

static void
format_ipv4(uint ip, char *buf, int bufsz)
{
  snprintf(buf, (size_t)bufsz, "%d.%d.%d.%d",
           (ip >> 24) & 0xff, (ip >> 16) & 0xff,
           (ip >> 8) & 0xff, ip & 0xff);
}

static int
load_netdev_counters(const char *ifname, uint *rx_pkts, uint *tx_pkts)
{
  char buf[1024];
  char name[32];
  char link[16];
  int fd;
  int n;
  int off;
  int line_start;
  int line_len;
  uint rx_bytes;
  uint rx_err;
  uint tx_bytes;
  uint tx_err;

  fd = open("/proc/net_dev", O_RDONLY);
  if(fd < 0)
    return -1;
  n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if(n <= 0)
    return -1;
  buf[n] = 0;

  off = 0;
  while(off < n) {
    line_start = off;
    while(off < n && buf[off] != '\n')
      off++;
    line_len = off - line_start;
    if(off < n)
      buf[off++] = 0;
    if(line_len <= 0)
      continue;
    if(sscanf(buf + line_start, "%31s %15s %u %u %u %u %u %u",
              name, link, rx_pkts, &rx_bytes, &rx_err,
              tx_pkts, &tx_bytes, &tx_err) == 8) {
      if(strcmp(name, ifname) == 0)
        return 0;
    }
  }

  return -1;
}

static int
open_bound_mode(const char *ifname, int mode, struct ifreq *ifr)
{
  int fd;

  fd = open("/dev/net/tun", O_RDWR);
  if(fd < 0)
    return -1;

  memset(ifr, 0, sizeof(*ifr));
  ifr->ifr_flags = (short)(mode | IFF_NO_PI);
  strncpy(ifr->ifr_name, ifname, sizeof(ifr->ifr_name) - 1);
  ifr->ifr_name[sizeof(ifr->ifr_name) - 1] = 0;
  if(ioctl(fd, TUNSETIFF, ifr) < 0){
    close(fd);
    return -1;
  }
  return fd;
}

static struct netifinfo*
find_ifinfo(struct netifinfo *ifs, int n, const char *ifname)
{
  int i;

  for(i = 0; i < n; i++) {
    if(strcmp(ifs[i].if_name, ifname) == 0)
      return &ifs[i];
  }
  return 0;
}

static int
load_ifinfo(const char *ifname, struct netifinfo *out)
{
  struct netifinfo ifs[NETIFINFO_MAX];
  struct netifinfo *ifp;
  int n;

  n = netifinfo(ifs, NETIFINFO_MAX);
  if(n < 0)
    return -1;
  ifp = find_ifinfo(ifs, n, ifname);
  if(ifp == 0)
    return -1;
  *out = *ifp;
  return 0;
}

static int
run_nonblock_empty(const char *ifname)
{
  struct ifreq ifr;
  char buf[128];
  int fd;
  int flags;
  int n;

  fd = open_bound_mode(ifname, mode_from_ifname(ifname), &ifr);
  if(fd < 0) {
    dprintf(2, "tuntest: bind failed for %s\n", ifname);
    return -1;
  }

  flags = fcntl(fd, F_GETFL, 0);
  if(flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    dprintf(2, "tuntest: F_SETFL O_NONBLOCK failed\n");
    close(fd);
    return -1;
  }

  n = read(fd, buf, sizeof(buf));
  close(fd);
  if(n >= 0) {
    dprintf(2, "tuntest: expected empty nonblock read failure, got %d\n", n);
    return -1;
  }

  dprintf(1, "PASS nonblock-empty %s\n", ifname);
  return 0;
}

static int
run_poll_empty(const char *ifname, int timeout_ms)
{
  struct ifreq ifr;
  struct pollfd pfd;
  int fd;
  int ready;

  fd = open_bound_mode(ifname, mode_from_ifname(ifname), &ifr);
  if(fd < 0) {
    dprintf(2, "tuntest: bind failed for %s\n", ifname);
    return -1;
  }

  pfd.fd = fd;
  pfd.events = POLLIN | POLLOUT;
  pfd.revents = 0;
  ready = poll(&pfd, 1, timeout_ms);
  close(fd);

  if(ready < 0) {
    dprintf(2, "tuntest: poll failed\n");
    return -1;
  }
  if((pfd.revents & POLLIN) != 0) {
    dprintf(2, "tuntest: empty queue unexpectedly reported POLLIN\n");
    return -1;
  }
  if((pfd.revents & POLLOUT) == 0) {
    dprintf(2, "tuntest: empty queue did not report POLLOUT\n");
    return -1;
  }
  if((pfd.revents & POLLERR) != 0) {
    dprintf(2, "tuntest: empty queue reported POLLERR\n");
    return -1;
  }

  dprintf(1, "PASS poll-empty %s revents=0x%x\n", ifname, pfd.revents);
  return 0;
}

static void
build_echo_request(struct tuntest_echo_pkt *pkt, uint local_ip, uint peer_ip)
{
  memset(pkt, 0, sizeof(*pkt));

  pkt->ip.vhl = 0x45;
  pkt->ip.tos = 0;
  pkt->ip.len = net_htons((ushort)sizeof(*pkt));
  pkt->ip.id = net_htons(0x1234);
  pkt->ip.off = 0;
  pkt->ip.ttl = 64;
  pkt->ip.proto = NET_IP_ICMP;
  pkt->ip.sum = 0;
  pkt->ip.src = net_htonl(peer_ip);
  pkt->ip.dst = net_htonl(local_ip);

  pkt->icmp.type = ICMP_ECHO;
  pkt->icmp.code = 0;
  pkt->icmp.ident = (ushort)getpid();
  pkt->icmp.seq = 1;
  memmove(pkt->payload, TUNTEST_PAYLOAD, sizeof(pkt->payload));
  pkt->icmp.csum = 0;
  pkt->icmp.csum = net_csum(&pkt->icmp,
                            sizeof(pkt->icmp) + sizeof(pkt->payload));
  pkt->ip.sum = net_csum(&pkt->ip, sizeof(pkt->ip));
}

static int
validate_echo_reply(char *buf, int n, uint local_ip, uint peer_ip)
{
  struct ip_hdr *ip;
  struct icmp_hdr *icmp;
  uint hlen;
  uint total_len;

  if(n < (int)(sizeof(struct ip_hdr) + sizeof(struct icmp_hdr)))
    return -1;

  ip = (struct ip_hdr*)buf;
  hlen = (uint)((ip->vhl & 0x0f) * 4);
  if((ip->vhl >> 4) != 4 || hlen < sizeof(struct ip_hdr) || hlen > (uint)n)
    return -1;
  total_len = net_ntohs(ip->len);
  if(total_len > (uint)n || total_len < hlen + sizeof(struct icmp_hdr))
    return -1;
  if(net_csum(ip, hlen) != 0)
    return -1;
  if(net_ntohl(ip->src) != local_ip || net_ntohl(ip->dst) != peer_ip)
    return -1;
  if(ip->proto != NET_IP_ICMP)
    return -1;

  icmp = (struct icmp_hdr*)(buf + hlen);
  if(icmp->type != ICMP_ECHO_REPLY || icmp->code != 0)
    return -1;
  if(icmp->ident != (ushort)getpid() || icmp->seq != 1)
    return -1;
  if(net_csum(icmp, total_len - hlen) != 0)
    return -1;
  return 0;
}

static int
run_icmp_self(const char *ifname, uint local_ip, uint peer_ip)
{
  struct ifreq ifr;
  struct pollfd pfd;
  struct tuntest_echo_pkt pkt;
  struct netifinfo before;
  struct netifinfo after;
  char buf[MBUF_SIZE];
  char lip[20];
  char pip[20];
  uint before_rx;
  uint before_tx;
  uint after_rx;
  uint after_tx;
  int fd;
  int n;
  int ready;

  if(load_ifinfo(ifname, &before) < 0) {
    dprintf(2, "tuntest: interface %s not found\n", ifname);
    return -1;
  }
  if(before.if_addr != local_ip) {
    format_ipv4(before.if_addr, lip, sizeof(lip));
    format_ipv4(local_ip, pip, sizeof(pip));
    dprintf(2, "tuntest: %s has addr %s, expected %s\n", ifname, lip, pip);
    return -1;
  }
  if(load_netdev_counters(ifname, &before_rx, &before_tx) < 0) {
    dprintf(2, "tuntest: failed to read /proc/net_dev counters for %s\n", ifname);
    return -1;
  }

  fd = open_bound_mode(ifname, IFF_TUN, &ifr);
  if(fd < 0) {
    dprintf(2, "tuntest: bind failed for %s\n", ifname);
    return -1;
  }

  build_echo_request(&pkt, local_ip, peer_ip);
  n = write(fd, &pkt, sizeof(pkt));
  if(n != (int)sizeof(pkt)) {
    dprintf(2, "tuntest: echo request write failed (%d)\n", n);
    close(fd);
    return -1;
  }

  pfd.fd = fd;
  pfd.events = POLLIN;
  pfd.revents = 0;
  ready = poll(&pfd, 1, TUNTEST_TIMEOUT_MS);
  if(ready <= 0 || (pfd.revents & POLLIN) == 0) {
    dprintf(2, "tuntest: timed out waiting for echo reply revents=0x%x\n",
            pfd.revents);
    close(fd);
    return -1;
  }

  n = read(fd, buf, sizeof(buf));
  close(fd);
  if(n < 0) {
    dprintf(2, "tuntest: read failed for echo reply\n");
    return -1;
  }
  if(validate_echo_reply(buf, n, local_ip, peer_ip) < 0) {
    dprintf(2, "tuntest: malformed echo reply\n");
    return -1;
  }
  if(load_ifinfo(ifname, &after) < 0) {
    dprintf(2, "tuntest: interface %s disappeared\n", ifname);
    return -1;
  }
  if(load_netdev_counters(ifname, &after_rx, &after_tx) < 0) {
    dprintf(2, "tuntest: failed to read /proc/net_dev counters for %s\n", ifname);
    return -1;
  }
  if(after_tx < before_tx + 1 || after_rx < before_rx + 1) {
    dprintf(2,
            "tuntest: counters did not advance tx %u->%u rx %u->%u\n",
            before_tx, after_tx, before_rx, after_rx);
    return -1;
  }

  format_ipv4(local_ip, lip, sizeof(lip));
  format_ipv4(peer_ip, pip, sizeof(pip));
  dprintf(1,
          "PASS icmp-self %s local=%s peer=%s tx %u->%u rx %u->%u\n",
          ifname, lip, pip,
      before_tx, after_tx, before_rx, after_rx);
  return 0;
}

static void
encode_ipv4_be(uchar out[4], uint ip)
{
  uint be;

  be = net_htonl(ip);
  memmove(out, &be, sizeof(be));
}

static int
validate_tap_arp_reply(char *buf, int n, uint local_ip, uint peer_ip,
                       const uchar *src_mac)
{
  struct tuntest_tap_arp_pkt *pkt;
  uint spa;
  uint tpa;

  if(n < (int)sizeof(struct tuntest_tap_arp_pkt))
    return -1;

  pkt = (struct tuntest_tap_arp_pkt*)buf;
  if(net_ntohs(pkt->eth.type) != ETHERTYPE_ARP)
    return -1;
  if(memcmp(pkt->eth.dst, src_mac, ETH_ADDR_LEN) != 0)
    return -1;

  if(net_ntohs(pkt->arp.hdr.htype) != ARP_HW_ETHER ||
     net_ntohs(pkt->arp.hdr.ptype) != ARP_PROTO_IP ||
     pkt->arp.hdr.hlen != ETH_ADDR_LEN ||
     pkt->arp.hdr.plen != 4)
    return -1;
  if(net_ntohs(pkt->arp.hdr.oper) != ARP_OP_REPLY)
    return -1;
  if(memcmp(pkt->arp.tha, src_mac, ETH_ADDR_LEN) != 0)
    return -1;

  memmove(&spa, pkt->arp.spa, sizeof(spa));
  memmove(&tpa, pkt->arp.tpa, sizeof(tpa));
  spa = net_ntohl(spa);
  tpa = net_ntohl(tpa);
  if(spa != local_ip || tpa != peer_ip)
    return -1;

  return 0;
}

static int
run_tap_arp_self(const char *ifname, uint local_ip, uint peer_ip)
{
  static const uchar bcast_mac[ETH_ADDR_LEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  static const uchar src_mac[ETH_ADDR_LEN] = {0x02, 0xaa, 0xbb, 0xcc, 0xdd, 0xee};
  struct ifreq ifr;
  struct pollfd pfd;
  struct netifinfo before;
  struct netifinfo after;
  struct tuntest_tap_arp_pkt pkt;
  char buf[MBUF_SIZE];
  char lip[20];
  char pip[20];
  uint before_rx;
  uint before_tx;
  uint after_rx;
  uint after_tx;
  int fd;
  int n;
  int ready;

  if(load_ifinfo(ifname, &before) < 0) {
    dprintf(2, "tuntest: interface %s not found\n", ifname);
    return -1;
  }
  if(before.if_addr != local_ip) {
    format_ipv4(before.if_addr, lip, sizeof(lip));
    format_ipv4(local_ip, pip, sizeof(pip));
    dprintf(2, "tuntest: %s has addr %s, expected %s\n", ifname, lip, pip);
    return -1;
  }
  if(load_netdev_counters(ifname, &before_rx, &before_tx) < 0) {
    dprintf(2, "tuntest: failed to read /proc/net_dev counters for %s\n", ifname);
    return -1;
  }

  fd = open_bound_mode(ifname, IFF_TAP, &ifr);
  if(fd < 0) {
    dprintf(2, "tuntest: bind failed for %s\n", ifname);
    return -1;
  }

  memset(&pkt, 0, sizeof(pkt));
  memmove(pkt.eth.dst, bcast_mac, ETH_ADDR_LEN);
  memmove(pkt.eth.src, src_mac, ETH_ADDR_LEN);
  pkt.eth.type = net_htons(ETHERTYPE_ARP);

  pkt.arp.hdr.htype = net_htons(ARP_HW_ETHER);
  pkt.arp.hdr.ptype = net_htons(ARP_PROTO_IP);
  pkt.arp.hdr.hlen = ETH_ADDR_LEN;
  pkt.arp.hdr.plen = 4;
  pkt.arp.hdr.oper = net_htons(ARP_OP_REQUEST);
  memmove(pkt.arp.sha, src_mac, ETH_ADDR_LEN);
  encode_ipv4_be(pkt.arp.spa, peer_ip);
  encode_ipv4_be(pkt.arp.tpa, local_ip);

  n = write(fd, &pkt, sizeof(pkt));
  if(n != (int)sizeof(pkt)) {
    dprintf(2, "tuntest: tap arp request write failed (%d)\n", n);
    close(fd);
    return -1;
  }

  pfd.fd = fd;
  pfd.events = POLLIN;
  pfd.revents = 0;
  ready = poll(&pfd, 1, TUNTEST_TIMEOUT_MS);
  if(ready <= 0 || (pfd.revents & POLLIN) == 0) {
    dprintf(2, "tuntest: timed out waiting for ARP reply revents=0x%x\n",
            pfd.revents);
    close(fd);
    return -1;
  }

  n = read(fd, buf, sizeof(buf));
  close(fd);
  if(n < 0) {
    dprintf(2, "tuntest: read failed for ARP reply\n");
    return -1;
  }
  if(validate_tap_arp_reply(buf, n, local_ip, peer_ip, src_mac) < 0) {
    dprintf(2, "tuntest: malformed ARP reply on %s\n", ifname);
    return -1;
  }

  if(load_ifinfo(ifname, &after) < 0) {
    dprintf(2, "tuntest: interface %s disappeared\n", ifname);
    return -1;
  }
  if(load_netdev_counters(ifname, &after_rx, &after_tx) < 0) {
    dprintf(2, "tuntest: failed to read /proc/net_dev counters for %s\n", ifname);
    return -1;
  }
  if(after_tx < before_tx + 1 || after_rx < before_rx + 1) {
    dprintf(2,
            "tuntest: counters did not advance tx %u->%u rx %u->%u\n",
            before_tx, after_tx, before_rx, after_rx);
    return -1;
  }

  format_ipv4(local_ip, lip, sizeof(lip));
  format_ipv4(peer_ip, pip, sizeof(pip));
  dprintf(1,
          "PASS tap-arp-self %s local=%s peer=%s tx %u->%u rx %u->%u\n",
          ifname, lip, pip,
          before_tx, after_tx, before_rx, after_rx);
  return 0;
}

int
main(int argc, char **argv)
{
  int timeout_ms;
  uint local_ip;
  uint peer_ip;

  if(argc < 3)
    usage();

  if(strcmp(argv[1], "nonblock-empty") == 0) {
    if(argc != 3)
      usage();
    exit(run_nonblock_empty(argv[2]) == 0 ? 0 : 1);
  }

  if(strcmp(argv[1], "poll-empty") == 0) {
    if(argc != 3 && argc != 4)
      usage();
    timeout_ms = 0;
    if(argc == 4)
      timeout_ms = atoi(argv[3]);
    exit(run_poll_empty(argv[2], timeout_ms) == 0 ? 0 : 1);
  }

  if(strcmp(argv[1], "icmp-self") == 0) {
    if(argc != 5)
      usage();
    if(parse_ipv4(argv[3], &local_ip) < 0 || parse_ipv4(argv[4], &peer_ip) < 0)
      usage();
    exit(run_icmp_self(argv[2], local_ip, peer_ip) == 0 ? 0 : 1);
  }

  if(strcmp(argv[1], "run-all") == 0) {
    if(argc != 5)
      usage();
    if(parse_ipv4(argv[3], &local_ip) < 0 || parse_ipv4(argv[4], &peer_ip) < 0)
      usage();
    if(run_nonblock_empty(argv[2]) < 0)
      exit(1);
    if(run_poll_empty(argv[2], 0) < 0)
      exit(1);
    if(run_icmp_self(argv[2], local_ip, peer_ip) < 0)
      exit(1);
    dprintf(1, "PASS run-all %s\n", argv[2]);
    exit(0);
  }

  if(strcmp(argv[1], "tap-arp-self") == 0) {
    if(argc != 5)
      usage();
    if(parse_ipv4(argv[3], &local_ip) < 0 || parse_ipv4(argv[4], &peer_ip) < 0)
      usage();
    exit(run_tap_arp_self(argv[2], local_ip, peer_ip) == 0 ? 0 : 1);
  }

  if(strcmp(argv[1], "run-all-tap") == 0) {
    if(argc != 5)
      usage();
    if(parse_ipv4(argv[3], &local_ip) < 0 || parse_ipv4(argv[4], &peer_ip) < 0)
      usage();
    if(run_nonblock_empty(argv[2]) < 0)
      exit(1);
    if(run_poll_empty(argv[2], 0) < 0)
      exit(1);
    if(run_tap_arp_self(argv[2], local_ip, peer_ip) < 0)
      exit(1);
    dprintf(1, "PASS run-all-tap %s\n", argv[2]);
    exit(0);
  }

  usage();
}