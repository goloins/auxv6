#include "../include/types.h"
#include "../include/user.h"

static void
print_ipv4(uint ip)
{
  printf(1, "%d.%d.%d.%d",
         (ip >> 24) & 0xff,
         (ip >> 16) & 0xff,
         (ip >> 8) & 0xff,
         ip & 0xff);
}

static void
print_mac(uchar *mac)
{
  int i;
  int nonzero;

  nonzero = 0;
  for(i = 0; i < 6; i++) {
    if(mac[i] != 0)
      nonzero = 1;
  }
  if(!nonzero) {
    printf(1, "-\n");
    return;
  }

  for(i = 0; i < 6; i++) {
    if(i > 0)
      printf(1, ":");
    printf(1, "%02x", mac[i]);
  }
  printf(1, "\n");
}

static int
parse_ipv4(const char *s, uint *out)
{
  int i;
  int part;
  uint ip;

  if(s == 0 || out == 0)
    return -1;
  if(strcmp(s, "-") == 0 || strcmp(s, "0") == 0) {
    *out = 0;
    return 0;
  }

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
dump_state(void)
{
  struct netifinfo ifs[NETIFINFO_MAX];
  struct routeinfo rts[ROUTEINFO_MAX];
  char *ifname;
  int i;
  int j;
  int nif;
  int nrt;

  nif = netifinfo(ifs, NETIFINFO_MAX);
  if(nif < 0) {
    printf(2, "netinfo: netifinfo failed\n");
    return;
  }

  printf(1, "Interfaces (%d):\n", nif);
  for(i = 0; i < nif; i++) {
    printf(1, "  %s (if%d) mtu=%d flags=0x%x addr=",
      ifs[i].if_name, ifs[i].if_index, ifs[i].if_mtu, ifs[i].if_flags);
    if(ifs[i].if_addr)
      print_ipv4(ifs[i].if_addr);
    else
      printf(1, "-");
    printf(1, " mask=");
    if(ifs[i].if_netmask)
      print_ipv4(ifs[i].if_netmask);
    else
      printf(1, "-");
    printf(1, " mac=");
    print_mac(ifs[i].if_hwaddr);
  }

  nrt = routeinfo(rts, ROUTEINFO_MAX);
  if(nrt < 0) {
    printf(2, "netinfo: routeinfo failed\n");
    return;
  }

  printf(1, "Routes (%d):\n", nrt);
  for(i = 0; i < nrt; i++) {
    ifname = "?";
    for(j = 0; j < nif; j++) {
      if(ifs[j].if_index == rts[i].if_index) {
        ifname = ifs[j].if_name;
        break;
      }
    }

    printf(1, "  %s (if%d) dst=", ifname, rts[i].if_index);
    print_ipv4(rts[i].rt_dst);
    printf(1, " mask=");
    print_ipv4(rts[i].rt_mask);
    printf(1, " gw=");
    print_ipv4(rts[i].rt_gateway);
    printf(1, " src=");
    print_ipv4(rts[i].rt_src);
    printf(1, " flags=0x%x\n", rts[i].rt_flags);
  }
}

int
main(int argc, char *argv[])
{
  uint addr;
  uint dst;
  uint mask;
  uint gw;
  uint src;
  int ifindex;

  if(argc == 1) {
    dump_state();
    exit();
  }

  if(argc == 5 && strcmp(argv[1], "addr") == 0) {
    if(parse_ipv4(argv[3], &addr) < 0 ||
       parse_ipv4(argv[4], &mask) < 0) {
      printf(2, "netinfo: invalid IPv4 value\n");
      exit();
    }

    ifindex = atoi(argv[2]);
    if(ifindex <= 0) {
      printf(2, "netinfo: invalid ifindex\n");
      exit();
    }

    if(netifsetaddr(ifindex, addr, mask) < 0) {
      printf(2, "netinfo: netifsetaddr failed\n");
      exit();
    }

    dump_state();
    exit();
  }

  if(argc != 7 || strcmp(argv[1], "add") != 0) {
    printf(2, "usage: netinfo\n");
    printf(2, "       netinfo addr <ifindex> <addr> <mask>\n");
    printf(2, "       netinfo add <dst> <mask> <gw|-> <src|-> <ifindex>\n");
    exit();
  }

  if(parse_ipv4(argv[2], &dst) < 0 ||
     parse_ipv4(argv[3], &mask) < 0 ||
     parse_ipv4(argv[4], &gw) < 0 ||
     parse_ipv4(argv[5], &src) < 0) {
    printf(2, "netinfo: invalid IPv4 value\n");
    exit();
  }

  ifindex = atoi(argv[6]);
  if(ifindex <= 0) {
    printf(2, "netinfo: invalid ifindex\n");
    exit();
  }

  if(routeadd(dst, mask, gw, src, ifindex) < 0) {
    printf(2, "netinfo: routeadd failed\n");
    exit();
  }

  dump_state();
  exit();
}
