#include "types.h"
#include "auxv6/user.h"
#include "netcommon.h"

static int
parse_ipv4(const char *s, uint *out)
{
  int i;
  int part;
  uint ip;

  if(s == 0 || out == 0)
    return -1;

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
usage(void)
{
  dprintf(2, "usage: nslookup host [server]\n");
  exit(1);
}

int
main(int argc, char *argv[])
{
  uint server;
  uint answer;
  uint servers[3];
  int nservers;

  if(argc != 2 && argc != 3)
    usage();

  if(argc == 3) {
    if(parse_ipv4(argv[2], &server) < 0) {
      dprintf(2, "nslookup: invalid server %s\n", argv[2]);
      exit(1);
    }
  } else {
    nservers = dns_nameservers(servers, 3);
    if(nservers <= 0) {
      dprintf(2, "nslookup: no nameserver configured\n");
      exit(1);
    }
    server = servers[0];
  }

  if(dns_lookup_ipv4(argv[1], server, &answer) < 0) {
    dprintf(2, "Server: ");
    net_print_ipv4(server);
    dprintf(2, "\n");
    dprintf(2, "nslookup: lookup failed for %s\n", argv[1]);
    exit(1);
  }

  dprintf(1, "Server: ");
  net_print_ipv4(server);
  dprintf(1, "\n");
  dprintf(1, "Name: %s\n", argv[1]);
  dprintf(1, "Address: ");
  net_print_ipv4(answer);
  dprintf(1, "\n");
  exit(0);
}