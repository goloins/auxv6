#include "types.h"
#include "auxv6/user.h"
#include "netcommon.h"

static void
show_routes(void)
{
  struct netifinfo ifs[NETIFINFO_MAX];
  struct routeinfo routes[ROUTEINFO_MAX];
  int nif;
  int nrt;
  int i;
  int j;
  char *ifname;

  nif = net_load_ifs(ifs, NETIFINFO_MAX);
  if(nif < 0)
    exit();
  nrt = routeinfo(routes, ROUTEINFO_MAX);
  if(nrt < 0) {
    printf(2, "netstat: routeinfo failed\n");
    exit();
  }

  printf(1, "Kernel IP routing table\n");
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

int
main(int argc, char *argv[])
{
  struct netifinfo ifs[NETIFINFO_MAX];
  int nif;

  nif = net_load_ifs(ifs, NETIFINFO_MAX);
  if(nif < 0)
    exit();

  if(argc == 1) {
    net_show_interfaces(ifs, nif);
    printf(1, "\n");
    show_routes();
    exit();
  }

  if(argc == 2 && strcmp(argv[1], "-i") == 0) {
    net_show_interfaces(ifs, nif);
    exit();
  }
  if(argc == 2 && (strcmp(argv[1], "-r") == 0 || strcmp(argv[1], "-rn") == 0)) {
    show_routes();
    exit();
  }

  printf(2, "usage: netstat [-i|-r|-rn]\n");
  exit();
}