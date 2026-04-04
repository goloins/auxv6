#include "types.h"
#include "errno.h"
#include "netdb.h"
#include "socket.h"
#include "arpa/inet.h"
#include "string.h"
#include "auxv6/user.h"

int h_errno;

static struct hostent he;
static char *aliases[1];
static char *addr_list[2];
static struct in_addr addr_slot;
static char name_slot[256];

static void
set_hostent_common(uint addr_net, const char *name)
{
  int n;

  if(name == 0)
    name = "";

  n = strlen(name);
  if(n >= (int)sizeof(name_slot))
    n = sizeof(name_slot) - 1;
  memmove(name_slot, name, n);
  name_slot[n] = 0;

  addr_slot.s_addr = addr_net;

  aliases[0] = 0;
  addr_list[0] = (char*)&addr_slot;
  addr_list[1] = 0;

  he.h_name = name_slot;
  he.h_aliases = aliases;
  he.h_addrtype = AF_INET;
  he.h_length = 4;
  he.h_addr_list = addr_list;
}

struct hostent *
gethostbyname(const char *name)
{
  uint addr_host;

  if(name == 0 || *name == 0) {
    h_errno = HOST_NOT_FOUND;
    errno = EINVAL;
    return 0;
  }

  if(resolve_ipv4(name, &addr_host) < 0) {
    h_errno = HOST_NOT_FOUND;
    errno = ENOENT;
    return 0;
  }

  set_hostent_common(htonl(addr_host), name);
  h_errno = 0;
  return &he;
}

struct hostent *
gethostbyaddr(const void *addr, socklen_t len, int type)
{
  struct in_addr in;
  char *name;

  if(addr == 0 || len != 4 || type != AF_INET) {
    h_errno = NO_RECOVERY;
    errno = EINVAL;
    return 0;
  }

  memmove(&in.s_addr, addr, 4);
  name = inet_ntoa(in);
  if(name == 0) {
    h_errno = NO_RECOVERY;
    errno = EINVAL;
    return 0;
  }

  set_hostent_common(in.s_addr, name);
  h_errno = 0;
  return &he;
}

const char *
hstrerror(int err)
{
  switch(err) {
  case HOST_NOT_FOUND:
    return "Unknown host";
  case TRY_AGAIN:
    return "Temporary failure in name resolution";
  case NO_RECOVERY:
    return "Non-recoverable name resolution failure";
  case NO_DATA:
    return "No address associated with hostname";
  default:
    return "Resolver error";
  }
}