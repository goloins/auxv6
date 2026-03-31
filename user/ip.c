#include "../include/types.h"
#include "../include/user.h"
#include "netcommon.h"

static void
show_routes(void)
{
  struct netifinfo ifs[NETIFINFO_MAX];
  struct routeinfo routes[ROUTEINFO_MAX];
  char *ifname;
  int nif;
  int nrt;
  int i;
  int j;
  int prefix;
  uint mask;

  nif = net_load_ifs(ifs, NETIFINFO_MAX);
  if(nif < 0)
    exit();
  nrt = routeinfo(routes, ROUTEINFO_MAX);
  if(nrt < 0) {
    printf(2, "ip: routeinfo failed\n");
    exit();
  }

  for(i = 0; i < nrt; i++) {
    ifname = "?";
    for(j = 0; j < nif; j++) {
      if(ifs[j].if_index == routes[i].if_index) {
        ifname = ifs[j].if_name;
        break;
      }
    }
    if(routes[i].rt_dst == 0 && routes[i].rt_mask == 0) {
      printf(1, "default");
    } else {
      net_print_ipv4(routes[i].rt_dst);
      prefix = 0;
      mask = routes[i].rt_mask;
      while(mask & 0x80000000U) {
        prefix++;
        mask <<= 1;
      }
      printf(1, "/%d", prefix);
    }
    if(routes[i].rt_gateway) {
      printf(1, " via ");
      net_print_ipv4(routes[i].rt_gateway);
    }
    printf(1, " dev %s", ifname);
    if(routes[i].rt_src) {
      printf(1, " src ");
      net_print_ipv4(routes[i].rt_src);
    }
    printf(1, "\n");
  }
}

static void
usage(void)
{
  printf(2, "usage: ip addr show\n");
  printf(2, "       ip addr add <addr>/<prefix> dev <ifname>\n");
  printf(2, "       ip route show\n");
  printf(2, "       ip route add default via <gateway> dev <ifname>\n");
  printf(2, "       ip route add <dst>/<prefix> via <gateway|-> dev <ifname>\n");
  exit();
}

int
main(int argc, char *argv[])
{
  struct netifinfo ifs[NETIFINFO_MAX];
  struct netifinfo *ifp;
  int nif;
  uint addr;
  uint mask;
  uint dst;
  uint gw;

  if(argc == 3 && strcmp(argv[1], "addr") == 0 && strcmp(argv[2], "show") == 0) {
    nif = net_load_ifs(ifs, NETIFINFO_MAX);
    if(nif < 0)
      exit();
    net_show_interfaces(ifs, nif);
    exit();
  }

  if(argc == 6 && strcmp(argv[1], "addr") == 0 && strcmp(argv[2], "add") == 0 && strcmp(argv[4], "dev") == 0) {
    nif = net_load_ifs(ifs, NETIFINFO_MAX);
    if(nif < 0)
      exit();
    if(net_parse_cidr(argv[3], &addr, &mask) < 0)
      usage();
    ifp = net_find_if(ifs, nif, argv[5]);
    if(ifp == 0) {
      printf(2, "ip: unknown interface %s\n", argv[5]);
      exit();
    }
    if(netifsetaddr(ifp->if_index, addr, mask) < 0) {
      printf(2, "ip: netifsetaddr failed\n");
      exit();
    }
    nif = net_load_ifs(ifs, NETIFINFO_MAX);
    if(nif >= 0)
      net_show_interfaces(ifs, nif);
    exit();
  }

  if(argc == 3 && strcmp(argv[1], "route") == 0 && strcmp(argv[2], "show") == 0) {
    show_routes();
    exit();
  }

  if(argc == 8 && strcmp(argv[1], "route") == 0 && strcmp(argv[2], "add") == 0 &&
     strcmp(argv[3], "default") == 0 && strcmp(argv[4], "via") == 0 &&
     strcmp(argv[6], "dev") == 0) {
    nif = net_load_ifs(ifs, NETIFINFO_MAX);
    if(nif < 0)
      exit();
    if(net_parse_ipv4(argv[5], &gw) < 0)
      usage();
    ifp = net_find_if(ifs, nif, argv[7]);
    if(ifp == 0) {
      printf(2, "ip: unknown interface %s\n", argv[7]);
      exit();
    }
    if(routeadd(0, 0, gw, ifp->if_addr, ifp->if_index) < 0) {
      printf(2, "ip: routeadd failed\n");
      exit();
    }
    show_routes();
    exit();
  }

  if(argc == 8 && strcmp(argv[1], "route") == 0 && strcmp(argv[2], "add") == 0 &&
     strcmp(argv[4], "via") == 0 && strcmp(argv[6], "dev") == 0) {
    nif = net_load_ifs(ifs, NETIFINFO_MAX);
    if(nif < 0)
      exit();
    if(net_parse_cidr(argv[3], &dst, &mask) < 0 || net_parse_ipv4(argv[5], &gw) < 0)
      usage();
    ifp = net_find_if(ifs, nif, argv[7]);
    if(ifp == 0) {
      printf(2, "ip: unknown interface %s\n", argv[7]);
      exit();
    }
    if(routeadd(dst & mask, mask, gw, ifp->if_addr, ifp->if_index) < 0) {
      printf(2, "ip: routeadd failed\n");
      exit();
    }
    show_routes();
    exit();
  }

  usage();
  return 0;
}