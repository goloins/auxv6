#include "types.h"
#include "auxv6/user.h"
#include "netcommon.h"

int
main(void)
{
  struct arpinfo entries[ARPINFO_MAX];
  struct netifinfo ifs[NETIFINFO_MAX];
  char *ifname;
  int nent;
  int nif;
  int i;
  int j;

  nif = net_load_ifs(ifs, NETIFINFO_MAX);
  if(nif < 0)
    exit(1);
  nent = arpinfo(entries, ARPINFO_MAX);
  if(nent < 0) {
    dprintf(2, "rarp: arpinfo failed\n");
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
    net_print_mac(entries[i].ai_mac);
    dprintf(1, " -> ");
    net_print_ipv4(entries[i].ai_ip);
    dprintf(1, " on %s\n", ifname);
  }

  exit(0);
}