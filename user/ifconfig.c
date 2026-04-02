#include "types.h"
#include "auxv6/user.h"
#include "netcommon.h"

static void
usage(void)
{
  printf(2, "usage: ifconfig\n");
  printf(2, "       ifconfig <ifname>\n");
  printf(2, "       ifconfig <ifname> <addr> netmask <mask>\n");
  printf(2, "       ifconfig <ifname> inet <addr> netmask <mask>\n");
  exit();
}

int
main(int argc, char *argv[])
{
  struct netifinfo ifs[NETIFINFO_MAX];
  struct netifinfo *ifp;
  int n;
  uint addr;
  uint mask;

  n = net_load_ifs(ifs, NETIFINFO_MAX);
  if(n < 0)
    exit();

  if(argc == 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0))
    usage();

  if(argc == 1) {
    net_show_interfaces(ifs, n);
    exit();
  }

  ifp = net_find_if(ifs, n, argv[1]);
  if(ifp == 0) {
    printf(2, "ifconfig: unknown interface %s\n", argv[1]);
    exit();
  }

  if(argc == 2) {
    net_show_interfaces(ifp, 1);
    exit();
  }

  if(argc == 4) {
    if(net_parse_ipv4(argv[2], &addr) < 0 || net_parse_ipv4(argv[3], &mask) < 0)
      usage();
  } else if(argc == 5 && strcmp(argv[3], "netmask") == 0) {
    if(net_parse_ipv4(argv[2], &addr) < 0 || net_parse_ipv4(argv[4], &mask) < 0)
      usage();
  } else if(argc == 6 && strcmp(argv[2], "inet") == 0 && strcmp(argv[4], "netmask") == 0) {
    if(net_parse_ipv4(argv[3], &addr) < 0 || net_parse_ipv4(argv[5], &mask) < 0)
      usage();
  } else {
    usage();
  }

  if(netifsetaddr(ifp->if_index, addr, mask) < 0) {
    printf(2, "ifconfig: netifsetaddr failed\n");
    exit();
  }

  n = net_load_ifs(ifs, NETIFINFO_MAX);
  if(n < 0)
    exit();
  ifp = net_find_if(ifs, n, argv[1]);
  if(ifp)
    net_show_interfaces(ifp, 1);
  exit();
}