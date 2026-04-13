#include "types.h"
#include "auxv6/user.h"
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
    exit(0);
  nrt = routeinfo(routes, ROUTEINFO_MAX);
  if(nrt < 0) {
    dprintf(2, "ip: routeinfo failed\n");
    exit(0);
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
      dprintf(1, "default");
    } else {
      net_print_ipv4(routes[i].rt_dst);
      prefix = 0;
      mask = routes[i].rt_mask;
      while(mask & 0x80000000U) {
        prefix++;
        mask <<= 1;
      }
      dprintf(1, "/%d", prefix);
    }
    if(routes[i].rt_gateway) {
      dprintf(1, " via ");
      net_print_ipv4(routes[i].rt_gateway);
    }
    dprintf(1, " dev %s", ifname);
    if(routes[i].rt_src) {
      dprintf(1, " src ");
      net_print_ipv4(routes[i].rt_src);
    }
    dprintf(1, "\n");
  }
}

static void
usage(void)
{
  dprintf(2, "usage: ip addr show\n");
  dprintf(2, "       ip addr add <addr>/<prefix> dev <ifname>\n");
  dprintf(2, "       ip route show\n");
  dprintf(2, "       ip route add default via <gateway> dev <ifname>\n");
  dprintf(2, "       ip route add <dst>/<prefix> via <gateway|-> dev <ifname>\n");
  dprintf(2, "       ip route del default dev <ifname>\n");
  dprintf(2, "       ip route del <dst>/<prefix> dev <ifname>\n");
  exit(0);
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
      exit(0);
    net_show_interfaces(ifs, nif);
    exit(0);
  }

  if(argc == 6 && strcmp(argv[1], "addr") == 0 && strcmp(argv[2], "add") == 0 && strcmp(argv[4], "dev") == 0) {
    nif = net_load_ifs(ifs, NETIFINFO_MAX);
    if(nif < 0)
      exit(0);
    if(net_parse_cidr(argv[3], &addr, &mask) < 0)
      usage();
    ifp = net_find_if(ifs, nif, argv[5]);
    if(ifp == 0) {
      dprintf(2, "ip: unknown interface %s\n", argv[5]);
      exit(0);
    }
    if(netifsetaddr(ifp->if_index, addr, mask) < 0) {
      dprintf(2, "ip: netifsetaddr failed\n");
      exit(0);
    }
    nif = net_load_ifs(ifs, NETIFINFO_MAX);
    if(nif >= 0)
      net_show_interfaces(ifs, nif);
    exit(0);
  }

  if(argc == 3 && strcmp(argv[1], "route") == 0 && strcmp(argv[2], "show") == 0) {
    show_routes();
    exit(0);
  }

  if(argc == 8 && strcmp(argv[1], "route") == 0 && strcmp(argv[2], "add") == 0 &&
     strcmp(argv[3], "default") == 0 && strcmp(argv[4], "via") == 0 &&
     strcmp(argv[6], "dev") == 0) {
    nif = net_load_ifs(ifs, NETIFINFO_MAX);
    if(nif < 0)
      exit(0);
    if(net_parse_ipv4(argv[5], &gw) < 0)
      usage();
    ifp = net_find_if(ifs, nif, argv[7]);
    if(ifp == 0) {
      dprintf(2, "ip: unknown interface %s\n", argv[7]);
      exit(0);
    }
    if(routeadd(0, 0, gw, ifp->if_addr, ifp->if_index) < 0) {
      dprintf(2, "ip: routeadd failed\n");
      exit(0);
    }
    show_routes();
    exit(0);
  }

  if(argc == 8 && strcmp(argv[1], "route") == 0 && strcmp(argv[2], "add") == 0 &&
     strcmp(argv[4], "via") == 0 && strcmp(argv[6], "dev") == 0) {
    nif = net_load_ifs(ifs, NETIFINFO_MAX);
    if(nif < 0)
      exit(0);
    if(net_parse_cidr(argv[3], &dst, &mask) < 0 || net_parse_ipv4(argv[5], &gw) < 0)
      usage();
    ifp = net_find_if(ifs, nif, argv[7]);
    if(ifp == 0) {
      dprintf(2, "ip: unknown interface %s\n", argv[7]);
      exit(0);
    }
    if(routeadd(dst & mask, mask, gw, ifp->if_addr, ifp->if_index) < 0) {
      dprintf(2, "ip: routeadd failed\n");
      exit(0);
    }
    show_routes();
    exit(0);
  }

  if(argc == 6 && strcmp(argv[1], "route") == 0 && strcmp(argv[2], "del") == 0 &&
     strcmp(argv[3], "default") == 0 && strcmp(argv[4], "dev") == 0) {
    nif = net_load_ifs(ifs, NETIFINFO_MAX);
    if(nif < 0)
      exit(0);
    ifp = net_find_if(ifs, nif, argv[5]);
    if(ifp == 0) {
      dprintf(2, "ip: unknown interface %s\n", argv[5]);
      exit(0);
    }
    if(routedel(0, 0, ifp->if_index) < 0) {
      dprintf(2, "ip: routedel failed\n");
      exit(0);
    }
    show_routes();
    exit(0);
  }

  if(argc == 6 && strcmp(argv[1], "route") == 0 && strcmp(argv[2], "del") == 0 &&
     strcmp(argv[4], "dev") == 0) {
    nif = net_load_ifs(ifs, NETIFINFO_MAX);
    if(nif < 0)
      exit(0);
    if(net_parse_cidr(argv[3], &dst, &mask) < 0)
      usage();
    ifp = net_find_if(ifs, nif, argv[5]);
    if(ifp == 0) {
      dprintf(2, "ip: unknown interface %s\n", argv[5]);
      exit(0);
    }
    if(routedel(dst & mask, mask, ifp->if_index) < 0) {
      dprintf(2, "ip: routedel failed\n");
      exit(0);
    }
    show_routes();
    exit(0);
  }

  usage();
  return 0;
}