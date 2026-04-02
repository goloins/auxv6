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

  nif = net_load_ifs(ifs, NETIFINFO_MAX);
  if(nif < 0)
    exit();
  nrt = routeinfo(routes, ROUTEINFO_MAX);
  if(nrt < 0) {
    printf(2, "route: routeinfo failed\n");
    exit();
  }

  printf(1, "Destination     Gateway         Genmask         Flags Iface\n");
  for(i = 0; i < nrt; i++) {
    ifname = "?";
    for(j = 0; j < nif; j++) {
      if(ifs[j].if_index == routes[i].if_index) {
        ifname = ifs[j].if_name;
        break;
      }
    }
    net_print_ipv4(routes[i].rt_dst);
    printf(1, " ");
    net_print_ipv4(routes[i].rt_gateway);
    printf(1, " ");
    net_print_ipv4(routes[i].rt_mask);
    printf(1, " 0x%x %s\n", routes[i].rt_flags, ifname);
  }
}

static void
usage(void)
{
  printf(2, "usage: route\n");
  printf(2, "       route add default <gateway> <ifname>\n");
  printf(2, "       route add <dst> <mask> <gateway|-> <ifname> [src]\n");
  printf(2, "       route del default <ifname>\n");
  printf(2, "       route del <dst> <mask> <ifname>\n");
  printf(2, "       route delete default <ifname>\n");
  printf(2, "       route delete <dst> <mask> <ifname>\n");
  exit();
}

int
main(int argc, char *argv[])
{
  struct netifinfo ifs[NETIFINFO_MAX];
  struct netifinfo *ifp;
  char *cmd;
  int nif;
  uint dst;
  uint mask;
  uint gw;
  uint src;

  if(argc == 1) {
    show_routes();
    exit();
  }
  cmd = argv[1];
  if(strcmp(cmd, "add") != 0 && strcmp(cmd, "del") != 0 && strcmp(cmd, "delete") != 0)
    usage();

  nif = net_load_ifs(ifs, NETIFINFO_MAX);
  if(nif < 0)
    exit();

  if(strcmp(cmd, "add") == 0) {
    if(strcmp(argv[2], "default") == 0) {
      if(argc != 5)
        usage();
      if(net_parse_ipv4(argv[3], &gw) < 0)
        usage();
      ifp = net_find_if(ifs, nif, argv[4]);
      if(ifp == 0) {
        printf(2, "route: unknown interface %s\n", argv[4]);
        exit();
      }
      src = ifp->if_addr;
      if(routeadd(0, 0, gw, src, ifp->if_index) < 0) {
        printf(2, "route: routeadd failed\n");
        exit();
      }
      show_routes();
      exit();
    }

    if(argc != 6 && argc != 7)
      usage();
    if(net_parse_ipv4(argv[2], &dst) < 0 ||
       net_parse_ipv4(argv[3], &mask) < 0 ||
       net_parse_ipv4(argv[4], &gw) < 0)
      usage();
    ifp = net_find_if(ifs, nif, argv[5]);
    if(ifp == 0) {
      printf(2, "route: unknown interface %s\n", argv[5]);
      exit();
    }
    src = ifp->if_addr;
    if(argc == 7 && net_parse_ipv4(argv[6], &src) < 0)
      usage();
    if(routeadd(dst, mask, gw, src, ifp->if_index) < 0) {
      printf(2, "route: routeadd failed\n");
      exit();
    }
    show_routes();
    exit();
  }

  if(strcmp(argv[2], "default") == 0) {
    if(argc != 4)
      usage();
    ifp = net_find_if(ifs, nif, argv[3]);
    if(ifp == 0) {
      printf(2, "route: unknown interface %s\n", argv[3]);
      exit();
    }
    if(routedel(0, 0, ifp->if_index) < 0) {
      printf(2, "route: routedel failed\n");
      exit();
    }
    show_routes();
    exit();
  }

  if(argc != 5)
    usage();
  if(net_parse_ipv4(argv[2], &dst) < 0 || net_parse_ipv4(argv[3], &mask) < 0)
    usage();
  ifp = net_find_if(ifs, nif, argv[4]);
  if(ifp == 0) {
    printf(2, "route: unknown interface %s\n", argv[4]);
    exit();
  }
  if(routedel(dst & mask, mask, ifp->if_index) < 0) {
    printf(2, "route: routedel failed\n");
    exit();
  }
  show_routes();
  exit();
}