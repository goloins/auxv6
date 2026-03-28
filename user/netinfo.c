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
  int n;

  n = netifinfo(ifs, NETIFINFO_MAX);
  if(n < 0) {
    printf(2, "netinfo: netifinfo failed\n");
    return;
  }

  printf(1, "Interfaces (%d):\n", n);
  for(i = 0; i < n; i++) {
      printf(1, "  %s (if%d) mtu=%d flags=0x%x\n",
        ifs[i].if_name, ifs[i].if_index, ifs[i].if_mtu, ifs[i].if_flags);
  }

  n = routeinfo(rts, ROUTEINFO_MAX);
  if(n < 0) {
    printf(2, "netinfo: routeinfo failed\n");
    return;
  }

  printf(1, "Routes (%d):\n", n);
  for(i = 0; i < n; i++) {
    ifname = "?";
    for(j = 0; j < n; j++) {
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
  uint dst;
  uint mask;
  uint gw;
  uint src;
  int ifindex;

  if(argc == 1) {
    dump_state();
    exit();
  }

  if(argc != 7 || strcmp(argv[1], "add") != 0) {
    printf(2, "usage: netinfo\n");
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
