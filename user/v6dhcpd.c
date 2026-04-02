#include "types.h"
#include "auxv6/user.h"
#include "socket.h"
#include "net.h"
#include "fcntl.h"
#include "netcommon.h"

#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67
#define DHCP_BROADCAST_IP 0xffffffffU

#define DHCP_OP_BOOTREQUEST 1
#define DHCP_OP_BOOTREPLY   2
#define DHCP_HTYPE_ETHERNET 1
#define DHCP_HLEN_ETHERNET  6
#define DHCP_FLAGS_BROADCAST 0x8000

#define DHCP_OPTION_PAD         0
#define DHCP_OPTION_SUBNET_MASK 1
#define DHCP_OPTION_ROUTER      3
#define DHCP_OPTION_REQ_IP      50
#define DHCP_OPTION_LEASE       51
#define DHCP_OPTION_MSG_TYPE    53
#define DHCP_OPTION_SERVER_ID   54
#define DHCP_OPTION_PARAM_REQ   55
#define DHCP_OPTION_CLIENT_ID   61
#define DHCP_OPTION_END         255

#define DHCPDISCOVER 1
#define DHCPOFFER    2
#define DHCPREQUEST  3
#define DHCPDECLINE  4
#define DHCPACK      5
#define DHCPNAK      6
#define DHCPRELEASE  7

#define DHCP_COOKIE0 99
#define DHCP_COOKIE1 130
#define DHCP_COOKIE2 83
#define DHCP_COOKIE3 99

#define DHCP_TIMEOUT_TICKS 50
#define DHCP_MAX_TRIES 4
#define DHCP_MIN_PACKET_LEN 240

#define V6DHCPD_MODE_AUTO    0
#define V6DHCPD_MODE_ACQUIRE 1
#define V6DHCPD_MODE_RENEW   2
#define V6DHCPD_MODE_RELEASE 3

#define V6DHCPD_LEASE_MAGIC   0x56364448U
#define V6DHCPD_LEASE_VERSION 1
#define V6DHCPD_PATH_MAX      64

struct dhcp_packet {
  uchar op;
  uchar htype;
  uchar hlen;
  uchar hops;
  uint xid;
  ushort secs;
  ushort flags;
  uint ciaddr;
  uint yiaddr;
  uint siaddr;
  uint giaddr;
  uchar chaddr[16];
  uchar sname[64];
  uchar file[128];
  uchar options[312];
} __attribute__((packed));

struct v6dhcpd_lease {
  uint magic;
  uint version;
  char if_name[NETIFINFO_NAME_MAX];
  uint addr;
  uint mask;
  uint router;
  uint server_id;
  uint lease;
};

struct dhcp_offer {
  uint yiaddr;
  uint server_id;
  uint subnet_mask;
  uint router;
  uint lease;
  uchar msg_type;
};

static void
usage(void)
{
  dprintf(2, "usage: v6dhcpd [ifname]\n");
  dprintf(2, "       v6dhcpd acquire [ifname]\n");
  dprintf(2, "       v6dhcpd renew [ifname]\n");
  dprintf(2, "       v6dhcpd release [ifname]\n");
  exit(0);
}

static uint
dhcp_opt_u32(uchar *p)
{
  return ((uint)p[0] << 24) | ((uint)p[1] << 16) | ((uint)p[2] << 8) | (uint)p[3];
}

static struct netifinfo*
find_default_if(struct netifinfo *ifs, int n)
{
  int i;

  for(i = 0; i < n; i++) {
    if((ifs[i].if_flags & IFF_LOOPBACK) != 0)
      continue;
    if((ifs[i].if_flags & IFF_UP) == 0)
      continue;
    return &ifs[i];
  }
  return 0;
}

static int
mac_present(uchar *mac)
{
  int i;

  for(i = 0; i < 6; i++) {
    if(mac[i] != 0)
      return 1;
  }
  return 0;
}

static int
bytes_equal(const uchar *a, const uchar *b, int n)
{
  int i;

  for(i = 0; i < n; i++) {
    if(a[i] != b[i])
      return 0;
  }
  return 1;
}

static int
append_bytes(char *dst, int *off, int max, const char *src)
{
  while(*src) {
    if(*off + 1 >= max)
      return -1;
    dst[*off] = *src;
    (*off)++;
    src++;
  }
  dst[*off] = 0;
  return 0;
}

static void
copy_cstr(char *dst, int max, const char *src)
{
  int i;

  if(max <= 0)
    return;
  for(i = 0; i + 1 < max && src[i] != 0; i++)
    dst[i] = src[i];
  dst[i] = 0;
}

static int
lease_path(char *path, int max, const char *ifname)
{
  int off;

  if(path == 0 || max <= 0 || ifname == 0)
    return -1;
  off = 0;
  path[0] = 0;
  if(append_bytes(path, &off, max, "/etc/v6dhcpd.") < 0)
    return -1;
  if(append_bytes(path, &off, max, ifname) < 0)
    return -1;
  if(append_bytes(path, &off, max, ".lease") < 0)
    return -1;
  return 0;
}

static int
load_lease(const char *ifname, struct v6dhcpd_lease *lease)
{
  char path[V6DHCPD_PATH_MAX];
  int fd;
  int n;

  if(lease_path(path, sizeof(path), ifname) < 0)
    return -1;
  fd = open(path, O_RDONLY);
  if(fd < 0)
    return -1;
  n = read(fd, lease, sizeof(*lease));
  close(fd);
  if(n != sizeof(*lease))
    return -1;
  if(lease->magic != V6DHCPD_LEASE_MAGIC || lease->version != V6DHCPD_LEASE_VERSION)
    return -1;
  if(strncmp(lease->if_name, ifname, NETIFINFO_NAME_MAX) != 0)
    return -1;
  return 0;
}

static int
save_lease(const char *ifname, struct v6dhcpd_lease *lease)
{
  char path[V6DHCPD_PATH_MAX];
  int fd;

  if(lease_path(path, sizeof(path), ifname) < 0)
    return -1;
  lease->magic = V6DHCPD_LEASE_MAGIC;
  lease->version = V6DHCPD_LEASE_VERSION;
  copy_cstr(lease->if_name, sizeof(lease->if_name), ifname);

  fd = open(path, O_CREATE | O_WRONLY | O_TRUNC);
  if(fd < 0)
    return -1;
  if(write(fd, lease, sizeof(*lease)) != sizeof(*lease)) {
    close(fd);
    return -1;
  }
  close(fd);
  return 0;
}

static void
delete_lease(const char *ifname)
{
  char path[V6DHCPD_PATH_MAX];

  if(lease_path(path, sizeof(path), ifname) < 0)
    return;
  unlink(path);
}

static void
dhcp_init_packet(struct dhcp_packet *pkt, uint xid, uchar *mac)
{
  memset(pkt, 0, sizeof(*pkt));
  pkt->op = DHCP_OP_BOOTREQUEST;
  pkt->htype = DHCP_HTYPE_ETHERNET;
  pkt->hlen = DHCP_HLEN_ETHERNET;
  pkt->xid = net_htonl(xid);
  pkt->flags = net_htons(DHCP_FLAGS_BROADCAST);
  memmove(pkt->chaddr, mac, 6);
  pkt->options[0] = DHCP_COOKIE0;
  pkt->options[1] = DHCP_COOKIE1;
  pkt->options[2] = DHCP_COOKIE2;
  pkt->options[3] = DHCP_COOKIE3;
}

static int
dhcp_add_option(uchar *opts, int off, uchar code, uchar len, const void *data)
{
  opts[off++] = code;
  opts[off++] = len;
  if(len > 0)
    memmove(opts + off, data, len);
  return off + len;
}

static int
dhcp_build_discover(struct dhcp_packet *pkt, uint xid, uchar *mac)
{
  uchar client_id[7];
  uchar req_list[4];
  uchar type;
  int off;

  dhcp_init_packet(pkt, xid, mac);
  off = 4;

  type = DHCPDISCOVER;
  off = dhcp_add_option(pkt->options, off, DHCP_OPTION_MSG_TYPE, 1, &type);

  client_id[0] = DHCP_HTYPE_ETHERNET;
  memmove(client_id + 1, mac, 6);
  off = dhcp_add_option(pkt->options, off, DHCP_OPTION_CLIENT_ID, sizeof(client_id), client_id);

  req_list[0] = DHCP_OPTION_SUBNET_MASK;
  req_list[1] = DHCP_OPTION_ROUTER;
  req_list[2] = DHCP_OPTION_SERVER_ID;
  req_list[3] = DHCP_OPTION_LEASE;
  off = dhcp_add_option(pkt->options, off, DHCP_OPTION_PARAM_REQ, sizeof(req_list), req_list);

  pkt->options[off++] = DHCP_OPTION_END;
  return (int)((uchar*)pkt->options - (uchar*)pkt) + off;
}

static int
dhcp_build_request(struct dhcp_packet *pkt, uint xid, uchar *mac,
                   uint req_ip, uint server_id, uint ciaddr)
{
  uchar client_id[7];
  uchar req_list[4];
  uchar type;
  uint be;
  int off;

  dhcp_init_packet(pkt, xid, mac);
  pkt->ciaddr = net_htonl(ciaddr);
  off = 4;

  type = DHCPREQUEST;
  off = dhcp_add_option(pkt->options, off, DHCP_OPTION_MSG_TYPE, 1, &type);

  if(req_ip != 0) {
    be = net_htonl(req_ip);
    off = dhcp_add_option(pkt->options, off, DHCP_OPTION_REQ_IP, 4, &be);
  }

  if(server_id != 0) {
    be = net_htonl(server_id);
    off = dhcp_add_option(pkt->options, off, DHCP_OPTION_SERVER_ID, 4, &be);
  }

  client_id[0] = DHCP_HTYPE_ETHERNET;
  memmove(client_id + 1, mac, 6);
  off = dhcp_add_option(pkt->options, off, DHCP_OPTION_CLIENT_ID, sizeof(client_id), client_id);

  req_list[0] = DHCP_OPTION_SUBNET_MASK;
  req_list[1] = DHCP_OPTION_ROUTER;
  req_list[2] = DHCP_OPTION_SERVER_ID;
  req_list[3] = DHCP_OPTION_LEASE;
  off = dhcp_add_option(pkt->options, off, DHCP_OPTION_PARAM_REQ, sizeof(req_list), req_list);

  pkt->options[off++] = DHCP_OPTION_END;
  return (int)((uchar*)pkt->options - (uchar*)pkt) + off;
}

static int
dhcp_build_release(struct dhcp_packet *pkt, uint xid, uchar *mac,
                   uint ciaddr, uint server_id)
{
  uchar client_id[7];
  uchar type;
  uint be;
  int off;

  dhcp_init_packet(pkt, xid, mac);
  pkt->ciaddr = net_htonl(ciaddr);
  pkt->flags = 0;
  off = 4;

  type = DHCPRELEASE;
  off = dhcp_add_option(pkt->options, off, DHCP_OPTION_MSG_TYPE, 1, &type);

  if(server_id != 0) {
    be = net_htonl(server_id);
    off = dhcp_add_option(pkt->options, off, DHCP_OPTION_SERVER_ID, 4, &be);
  }

  client_id[0] = DHCP_HTYPE_ETHERNET;
  memmove(client_id + 1, mac, 6);
  off = dhcp_add_option(pkt->options, off, DHCP_OPTION_CLIENT_ID, sizeof(client_id), client_id);

  pkt->options[off++] = DHCP_OPTION_END;
  return (int)((uchar*)pkt->options - (uchar*)pkt) + off;
}

static int
dhcp_parse_offer(char *buf, int len, uint xid, uchar *mac, struct dhcp_offer *offer)
{
  struct dhcp_packet *pkt;
  uchar *opt;
  int remain;
  int off;
  int optlen;

  if(buf == 0 || offer == 0)
    return -1;
  if(len < DHCP_MIN_PACKET_LEN)
    return -1;

  pkt = (struct dhcp_packet*)buf;
  if(pkt->op != DHCP_OP_BOOTREPLY || pkt->htype != DHCP_HTYPE_ETHERNET || pkt->hlen != 6)
    return -1;
  if(net_ntohl(pkt->xid) != xid)
    return -1;
  if(!bytes_equal(pkt->chaddr, mac, 6))
    return -1;
  if(pkt->options[0] != DHCP_COOKIE0 || pkt->options[1] != DHCP_COOKIE1 ||
     pkt->options[2] != DHCP_COOKIE2 || pkt->options[3] != DHCP_COOKIE3)
    return -1;

  memset(offer, 0, sizeof(*offer));
  offer->yiaddr = net_ntohl(pkt->yiaddr);

  opt = pkt->options;
  remain = len - DHCP_MIN_PACKET_LEN + 4;
  off = 4;
  while(off < remain) {
    if(opt[off] == DHCP_OPTION_END)
      break;
    if(opt[off] == DHCP_OPTION_PAD) {
      off++;
      continue;
    }
    if(off + 1 >= remain)
      break;
    optlen = opt[off + 1];
    if(off + 2 + optlen > remain)
      break;

    switch(opt[off]) {
    case DHCP_OPTION_MSG_TYPE:
      if(optlen >= 1)
        offer->msg_type = opt[off + 2];
      break;
    case DHCP_OPTION_SUBNET_MASK:
      if(optlen == 4)
        offer->subnet_mask = dhcp_opt_u32(opt + off + 2);
      break;
    case DHCP_OPTION_ROUTER:
      if(optlen >= 4)
        offer->router = dhcp_opt_u32(opt + off + 2);
      break;
    case DHCP_OPTION_SERVER_ID:
      if(optlen == 4)
        offer->server_id = dhcp_opt_u32(opt + off + 2);
      break;
    case DHCP_OPTION_LEASE:
      if(optlen == 4)
        offer->lease = dhcp_opt_u32(opt + off + 2);
      break;
    }

    off += 2 + optlen;
  }

  if(offer->msg_type == DHCPNAK)
    return -2;
  if(offer->yiaddr == 0)
    return -1;
  return 0;
}

static int
dhcp_prepare_socket(void)
{
  struct sockaddr_in src;
  int fd;

  fd = socket(AF_INET, SOCK_DGRAM, 0);
  if(fd < 0)
    return -1;

  memset(&src, 0, sizeof(src));
  src.sin_family = AF_INET;
  src.sin_port = DHCP_CLIENT_PORT;
  src.sin_addr = INADDR_ANY;
  if(bind(fd, &src, sizeof(src)) < 0) {
    close(fd);
    return -1;
  }

  return fd;
}

static int
dhcp_connect_peer(int fd, struct netifinfo *ifp, uint dst_ip)
{
  struct sockaddr_in dst;

  if(dst_ip == DHCP_BROADCAST_IP) {
    if(routeadd(DHCP_BROADCAST_IP, 0xffffffffU, 0, ifp->if_addr, ifp->if_index) < 0)
      return -1;
  }

  memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_port = DHCP_SERVER_PORT;
  dst.sin_addr = dst_ip;
  return connect(fd, &dst, sizeof(dst));
}

static int
dhcp_exchange(int fd, struct netifinfo *ifp, struct dhcp_packet *pkt, int pktlen,
              uint xid, uchar *mac, uchar expected, uint dst_ip,
              struct dhcp_offer *out)
{
  char buf[1024];
  struct dhcp_offer offer;
  int attempt;
  int n;
  int rc;

  for(attempt = 0; attempt < DHCP_MAX_TRIES; attempt++) {
    if(dhcp_connect_peer(fd, ifp, dst_ip) < 0)
      return -1;
    if(send(fd, pkt, pktlen) < 0)
      return -1;

    n = recvtimeout(fd, buf, sizeof(buf), DHCP_TIMEOUT_TICKS);
    if(n <= 0)
      continue;
    rc = dhcp_parse_offer(buf, n, xid, mac, &offer);
    if(rc == -2)
      return -2;
    if(rc < 0)
      continue;
    if(offer.msg_type != expected)
      continue;
    memmove(out, &offer, sizeof(*out));
    return 0;
  }

  return -1;
}

static int
dhcp_send_release(int fd, struct netifinfo *ifp, struct dhcp_packet *pkt, int pktlen,
                  uint dst_ip)
{
  if(dst_ip == 0)
    return -1;
  if(dhcp_connect_peer(fd, ifp, dst_ip) < 0)
    return -1;
  return send(fd, pkt, pktlen) < 0 ? -1 : 0;
}

static void
lease_from_offer(struct v6dhcpd_lease *lease, const struct v6dhcpd_lease *prior,
                 struct dhcp_offer *offer, struct dhcp_offer *ack)
{
  memset(lease, 0, sizeof(*lease));
  lease->addr = ack->yiaddr ? ack->yiaddr : offer->yiaddr;
  lease->mask = ack->subnet_mask ? ack->subnet_mask : offer->subnet_mask;
  lease->router = ack->router ? ack->router : offer->router;
  lease->server_id = ack->server_id ? ack->server_id : offer->server_id;
  lease->lease = ack->lease ? ack->lease : offer->lease;
  if(lease->mask == 0 && prior && prior->mask)
    lease->mask = prior->mask;
  if(lease->mask == 0)
    lease->mask = 0xffffff00U;
}

static int
clear_config(struct netifinfo *ifp)
{
  routedel(0, 0, ifp->if_index);
  routedel(DHCP_BROADCAST_IP, 0xffffffffU, ifp->if_index);
  return netifsetaddr(ifp->if_index, 0, 0);
}

static int
apply_lease(struct netifinfo *ifp, struct v6dhcpd_lease *lease)
{
  if(netifsetaddr(ifp->if_index, lease->addr, lease->mask) < 0)
    return -1;
  if(routeadd(DHCP_BROADCAST_IP, 0xffffffffU, 0, lease->addr, ifp->if_index) < 0)
    return -1;
  if(lease->router != 0) {
    if(routeadd(0, 0, lease->router, lease->addr, ifp->if_index) < 0)
      return -1;
  } else {
    routedel(0, 0, ifp->if_index);
  }
  return save_lease(ifp->if_name, lease);
}

static void
print_lease(struct netifinfo *ifp, struct v6dhcpd_lease *lease, const char *tag)
{
  dprintf(1, "%s: %s ", ifp->if_name, tag);
  net_print_ipv4(lease->addr);
  dprintf(1, " netmask ");
  net_print_ipv4(lease->mask);
  if(lease->router) {
    dprintf(1, " router ");
    net_print_ipv4(lease->router);
  }
  if(lease->server_id) {
    dprintf(1, " server ");
    net_print_ipv4(lease->server_id);
  }
  dprintf(1, "\n");
}

static int
acquire_lease(struct netifinfo *ifp)
{
  struct dhcp_packet pkt;
  struct dhcp_offer offer;
  struct dhcp_offer ack;
  struct v6dhcpd_lease lease;
  int fd;
  int pktlen;
  uint xid;

  if(clear_config(ifp) < 0)
    return -1;
  ifp->if_addr = 0;
  ifp->if_netmask = 0;

  fd = dhcp_prepare_socket();
  if(fd < 0)
    return -1;

  xid = ((uint)uptime() << 16) ^ ((uint)getpid() << 4) ^ ifp->if_index;
  pktlen = dhcp_build_discover(&pkt, xid, ifp->if_hwaddr);
  if(dhcp_exchange(fd, ifp, &pkt, pktlen, xid, ifp->if_hwaddr,
                   DHCPOFFER, DHCP_BROADCAST_IP, &offer) < 0) {
    close(fd);
    return -1;
  }

  pktlen = dhcp_build_request(&pkt, xid, ifp->if_hwaddr, offer.yiaddr, offer.server_id, 0);
  if(dhcp_exchange(fd, ifp, &pkt, pktlen, xid, ifp->if_hwaddr,
                   DHCPACK, DHCP_BROADCAST_IP, &ack) < 0) {
    close(fd);
    return -1;
  }

  close(fd);
  lease_from_offer(&lease, 0, &offer, &ack);
  if(apply_lease(ifp, &lease) < 0)
    return -1;
  print_lease(ifp, &lease, "leased");
  return 0;
}

static int
renew_lease(struct netifinfo *ifp, struct v6dhcpd_lease *prior)
{
  struct dhcp_packet pkt;
  struct dhcp_offer ack;
  struct dhcp_offer offer;
  struct v6dhcpd_lease lease;
  int fd;
  int pktlen;
  int rc;
  uint xid;
  uint addr;

  if(prior == 0)
    return -1;
  addr = ifp->if_addr ? ifp->if_addr : prior->addr;
  if(addr == 0)
    return -1;

  fd = dhcp_prepare_socket();
  if(fd < 0)
    return -1;

  xid = ((uint)uptime() << 16) ^ ((uint)getpid() << 4) ^ ifp->if_index ^ 0x524eU;
  memset(&offer, 0, sizeof(offer));
  memset(&ack, 0, sizeof(ack));

  pktlen = dhcp_build_request(&pkt, xid, ifp->if_hwaddr,
                              prior->addr ? prior->addr : addr,
                              prior->server_id, addr);
  rc = -1;
  if(prior->server_id != 0)
    rc = dhcp_exchange(fd, ifp, &pkt, pktlen, xid, ifp->if_hwaddr,
                       DHCPACK, prior->server_id, &ack);
  if(rc < 0)
    rc = dhcp_exchange(fd, ifp, &pkt, pktlen, xid, ifp->if_hwaddr,
                       DHCPACK, DHCP_BROADCAST_IP, &ack);

  close(fd);
  if(rc < 0)
    return acquire_lease(ifp);

  offer.yiaddr = prior->addr;
  offer.server_id = prior->server_id;
  offer.subnet_mask = prior->mask;
  offer.router = prior->router;
  offer.lease = prior->lease;
  lease_from_offer(&lease, prior, &offer, &ack);
  if(apply_lease(ifp, &lease) < 0)
    return -1;
  print_lease(ifp, &lease, "renewed");
  return 0;
}

static int
release_lease(struct netifinfo *ifp, struct v6dhcpd_lease *lease)
{
  struct dhcp_packet pkt;
  int fd;
  int pktlen;
  uint xid;
  uint addr;

  addr = ifp->if_addr;
  if(addr == 0 && lease)
    addr = lease->addr;

  if(lease && lease->server_id != 0 && addr != 0) {
    fd = dhcp_prepare_socket();
    if(fd >= 0) {
      xid = ((uint)uptime() << 16) ^ ((uint)getpid() << 4) ^ ifp->if_index ^ 0x524cU;
      pktlen = dhcp_build_release(&pkt, xid, ifp->if_hwaddr, addr, lease->server_id);
      dhcp_send_release(fd, ifp, &pkt, pktlen, lease->server_id);
      close(fd);
    }
  }

  clear_config(ifp);
  delete_lease(ifp->if_name);
  dprintf(1, "%s: released\n", ifp->if_name);
  return 0;
}

int
main(int argc, char *argv[])
{
  struct netifinfo ifs[NETIFINFO_MAX];
  struct netifinfo *ifp;
  struct v6dhcpd_lease lease;
  const char *ifname;
  int mode;
  int n;

  mode = V6DHCPD_MODE_AUTO;
  ifname = 0;

  if(argc == 2) {
    if(strcmp(argv[1], "acquire") == 0)
      mode = V6DHCPD_MODE_ACQUIRE;
    else if(strcmp(argv[1], "renew") == 0)
      mode = V6DHCPD_MODE_RENEW;
    else if(strcmp(argv[1], "release") == 0)
      mode = V6DHCPD_MODE_RELEASE;
    else
      ifname = argv[1];
  } else if(argc == 3) {
    if(strcmp(argv[1], "acquire") == 0)
      mode = V6DHCPD_MODE_ACQUIRE;
    else if(strcmp(argv[1], "renew") == 0)
      mode = V6DHCPD_MODE_RENEW;
    else if(strcmp(argv[1], "release") == 0)
      mode = V6DHCPD_MODE_RELEASE;
    else
      usage();
    ifname = argv[2];
  } else if(argc > 3) {
    usage();
  }

  n = net_load_ifs(ifs, NETIFINFO_MAX);
  if(n < 0)
    exit(0);

  if(ifname)
    ifp = net_find_if(ifs, n, ifname);
  else
    ifp = find_default_if(ifs, n);

  if(ifp == 0) {
    dprintf(2, "v6dhcpd: no suitable interface found\n");
    exit(0);
  }
  if(!mac_present(ifp->if_hwaddr)) {
    dprintf(2, "v6dhcpd: interface %s has no ethernet address\n", ifp->if_name);
    exit(0);
  }

  memset(&lease, 0, sizeof(lease));
  if(load_lease(ifp->if_name, &lease) < 0)
    memset(&lease, 0, sizeof(lease));

  if(mode == V6DHCPD_MODE_RELEASE) {
    if(release_lease(ifp, lease.magic == V6DHCPD_LEASE_MAGIC ? &lease : 0) < 0) {
      dprintf(2, "v6dhcpd: release failed on %s\n", ifp->if_name);
      exit(0);
    }
    exit(0);
  }

  if(mode == V6DHCPD_MODE_RENEW) {
    if(renew_lease(ifp, lease.magic == V6DHCPD_LEASE_MAGIC ? &lease : 0) < 0 &&
       acquire_lease(ifp) < 0) {
      dprintf(2, "v6dhcpd: renew failed on %s\n", ifp->if_name);
      exit(0);
    }
    exit(0);
  }

  if(mode == V6DHCPD_MODE_AUTO && lease.magic == V6DHCPD_LEASE_MAGIC) {
    if(renew_lease(ifp, &lease) == 0)
      exit(0);
  }

  if(acquire_lease(ifp) < 0) {
    dprintf(2, "v6dhcpd: acquire failed on %s\n", ifp->if_name);
    exit(0);
  }
  exit(0);
}