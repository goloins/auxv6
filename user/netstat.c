#include "types.h"
#include "fcntl.h"
#include "auxv6/user.h"
#include "netcommon.h"

#define PROC_NET_TCP "/proc/net_tcp"
#define PROC_NET_UDP "/proc/net_udp"
#define PROC_NET_DEV "/proc/net_dev"

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
    exit(1);
  nrt = routeinfo(routes, ROUTEINFO_MAX);
  if(nrt < 0) {
    dprintf(2, "netstat: routeinfo failed\n");
    exit(1);
  }

  dprintf(1, "Kernel IP routing table\n");
  dprintf(1, "Destination     Gateway         Genmask         Flags Iface\n");
  for(i = 0; i < nrt; i++) {
    ifname = "?";
    for(j = 0; j < nif; j++) {
      if(ifs[j].if_index == routes[i].if_index) {
        ifname = ifs[j].if_name;
        break;
      }
    }
    net_print_ipv4(routes[i].rt_dst);
    dprintf(1, " ");
    net_print_ipv4(routes[i].rt_gateway);
    dprintf(1, " ");
    net_print_ipv4(routes[i].rt_mask);
    dprintf(1, " 0x%x %s\n", routes[i].rt_flags, ifname);
  }
}

/*
 * Dump a /proc/net_* file to stdout, reading in chunks.
 * Returns 0 on success, -1 if the file cannot be opened.
 */
static int
show_proc_net(const char *path)
{
  int fd;
  int n;
  char buf[512];

  fd = open(path, O_RDONLY);
  if(fd < 0){
    dprintf(2, "netstat: cannot open %s\n", path);
    return -1;
  }
  while((n = read(fd, buf, sizeof(buf))) > 0)
    write(1, buf, n);
  close(fd);
  return 0;
}

int
main(int argc, char *argv[])
{
  struct netifinfo ifs[NETIFINFO_MAX];
  int nif;

  if(argc == 2 && strcmp(argv[1], "-t") == 0){
    show_proc_net(PROC_NET_TCP);
    exit(0);
  }
  if(argc == 2 && strcmp(argv[1], "-u") == 0){
    show_proc_net(PROC_NET_UDP);
    exit(0);
  }
  if(argc == 2 && strcmp(argv[1], "-s") == 0){
    show_proc_net(PROC_NET_DEV);
    exit(0);
  }

  nif = net_load_ifs(ifs, NETIFINFO_MAX);
  if(nif < 0)
    exit(1);

  if(argc == 1) {
    net_show_interfaces(ifs, nif);
    dprintf(1, "\n");
    show_routes();
    exit(0);
  }

  if(argc == 2 && strcmp(argv[1], "-i") == 0) {
    net_show_interfaces(ifs, nif);
    exit(0);
  }
  if(argc == 2 && (strcmp(argv[1], "-r") == 0 || strcmp(argv[1], "-rn") == 0)) {
    show_routes();
    exit(0);
  }

  dprintf(2, "usage: netstat [-i|-r|-rn|-t|-u|-s]\n");
  exit(1);
}