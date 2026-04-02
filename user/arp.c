#include "types.h"
#include "auxv6/user.h"
#include "netcommon.h"

int
main(int argc, char *argv[])
{
  struct arpinfo entries[ARPINFO_MAX];
  struct netifinfo ifs[NETIFINFO_MAX];
  char *ifname;
  int nent;
  int nif;
  int i;
  int j;

  if(argc > 2)
    goto usage;
  if(argc == 2 && strcmp(argv[1], "-a") != 0)
    goto usage;

  nif = net_load_ifs(ifs, NETIFINFO_MAX);
  if(nif < 0)
    exit(1);
  nent = arpinfo(entries, ARPINFO_MAX);
  if(nent < 0) {
    dprintf(2, "arp: arpinfo failed\n");
    exit(1);
  }

  for(i = 0; i < nent; i++) {
    ifname = "?";
    for(j = 0; j < nif; j++) {
      if(ifs[j].if_index == entries[i].if_index) {
        ifname = ifs[j].if_name;
        break;
      }
    }
    dprintf(1, "?");
    dprintf(1, " (");
    net_print_ipv4(entries[i].ai_ip);
    dprintf(1, ") at ");
    net_print_mac(entries[i].ai_mac);
    dprintf(1, " on %s", ifname);
    if(entries[i].ai_flags & ARP_FLAG_PENDING)
      dprintf(1, " [pending]");
    if(entries[i].ai_flags & ARP_FLAG_RESOLVED)
      dprintf(1, " [resolved]");
    dprintf(1, "\n");
  }
  exit(0);

usage:
  dprintf(2, "usage: arp [-a]\n");
  exit(1);
}