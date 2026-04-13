#include "types.h"
#include "errno.h"
#include "netdb.h"
#include "socket.h"
#include "arpa/inet.h"
#include "string.h"
#include "auxv6/user.h"

static int
parse_service_port(const char *service, int flags, int *out_port)
{
  int v;
  int i;

  if(out_port == 0)
    return EAI_SYSTEM;

  if(service == 0 || *service == 0) {
    *out_port = 0;
    return 0;
  }

  v = 0;
  for(i = 0; service[i] != 0; i++) {
    if(service[i] < '0' || service[i] > '9')
      break;
    v = v * 10 + (service[i] - '0');
    if(v > 65535)
      return EAI_SERVICE;
  }
  if(service[i] == 0) {
    *out_port = v;
    return 0;
  }

  if(flags & AI_NUMERICSERV)
    return EAI_SERVICE;

  if(strcmp(service, "http") == 0) {
    *out_port = 80;
    return 0;
  }
  if(strcmp(service, "https") == 0) {
    *out_port = 443;
    return 0;
  }
  if(strcmp(service, "domain") == 0) {
    *out_port = 53;
    return 0;
  }

  return EAI_SERVICE;
}

static int
is_numeric_host(const char *hostname, uint *addr_host)
{
  struct in_addr in;

  if(hostname == 0 || addr_host == 0)
    return 0;
  if(inet_aton(hostname, &in) == 0)
    return 0;

  *addr_host = ntohl(in.s_addr);
  return 1;
}

int h_errno;

static struct hostent he;
static char *aliases[1];
static char *addr_list[2];
static struct in_addr addr_slot;
static char name_slot[256];

struct service_ent {
  const char *name;
  int port_host;
  const char *proto;
};

static const struct service_ent g_services[] = {
  { "http", 80, "tcp" },
  { "https", 443, "tcp" },
  { "domain", 53, "tcp" },
  { "domain", 53, "udp" },
  { "ftp", 21, "tcp" },
  { "ssh", 22, "tcp" },
  { "telnet", 23, "tcp" },
  { "smtp", 25, "tcp" },
  { "pop3", 110, "tcp" },
  { "imap", 143, "tcp" },
};

static struct servent se;
static char *service_aliases[1];

static int
service_proto_match(const char *a, const char *b)
{
  if(a == 0 || *a == 0 || b == 0 || *b == 0)
    return 1;
  return strcmp(a, b) == 0;
}

static struct servent *
set_servent_common(const struct service_ent *src)
{
  se.s_name = (char*)src->name;
  service_aliases[0] = 0;
  se.s_aliases = service_aliases;
  se.s_port = htons((ushort)src->port_host);
  se.s_proto = (char*)src->proto;
  return &se;
}

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

struct servent *
getservbyname(const char *name, const char *proto)
{
  int i;

  if(name == 0 || *name == 0) {
    errno = EINVAL;
    return 0;
  }

  for(i = 0; i < (int)(sizeof(g_services) / sizeof(g_services[0])); i++) {
    if(strcmp(g_services[i].name, name) != 0)
      continue;
    if(!service_proto_match(proto, g_services[i].proto))
      continue;
    return set_servent_common(&g_services[i]);
  }

  errno = ENOENT;
  return 0;
}

struct servent *
getservbyport(int port, const char *proto)
{
  int i;
  int host_port;

  host_port = (int)ntohs((ushort)port);
  for(i = 0; i < (int)(sizeof(g_services) / sizeof(g_services[0])); i++) {
    if(g_services[i].port_host != host_port)
      continue;
    if(!service_proto_match(proto, g_services[i].proto))
      continue;
    return set_servent_common(&g_services[i]);
  }

  errno = ENOENT;
  return 0;
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

int
getaddrinfo(const char *hostname, const char *service,
            const struct addrinfo *hints, struct addrinfo **res)
{
  struct addrinfo *ai;
  struct sockaddr_in *sin;
  uint addr_host;
  int port;
  int rc;
  int flags;
  int family;
  int socktype;
  int protocol;

  if(res == 0)
    return EAI_SYSTEM;
  *res = 0;

  flags = 0;
  family = AF_UNSPEC;
  socktype = 0;
  protocol = 0;

  if(hints) {
    flags = hints->ai_flags;
    family = hints->ai_family;
    socktype = hints->ai_socktype;
    protocol = hints->ai_protocol;

    if(flags & ~(AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST | AI_NUMERICSERV | AI_ADDRCONFIG))
      return EAI_BADFLAGS;
    if(family != AF_UNSPEC && family != AF_INET)
      return EAI_FAMILY;
    if(socktype != 0 && socktype != SOCK_STREAM && socktype != SOCK_DGRAM && socktype != SOCK_RAW)
      return EAI_SOCKTYPE;
  }

  rc = parse_service_port(service, flags, &port);
  if(rc != 0)
    return rc;

  if(hostname == 0 || *hostname == 0) {
    if(flags & AI_PASSIVE)
      addr_host = INADDR_ANY;
    else
      addr_host = INADDR_LOOPBACK;
  } else if(is_numeric_host(hostname, &addr_host)) {
    /* numeric host parsed */
  } else {
    if(flags & AI_NUMERICHOST)
      return EAI_NONAME;
    if(resolve_ipv4(hostname, &addr_host) < 0)
      return EAI_NONAME;
  }

  ai = (struct addrinfo*)malloc(sizeof(*ai));
  if(ai == 0)
    return EAI_MEMORY;
  memset(ai, 0, sizeof(*ai));

  sin = (struct sockaddr_in*)malloc(sizeof(*sin));
  if(sin == 0) {
    free(ai);
    return EAI_MEMORY;
  }
  memset(sin, 0, sizeof(*sin));

  sin->sin_family = AF_INET;
  sin->sin_port = htons((ushort)port);
  sin->sin_addr.s_addr = htonl(addr_host);

  ai->ai_flags = flags;
  ai->ai_family = AF_INET;
  ai->ai_socktype = socktype;
  ai->ai_protocol = protocol;
  if(ai->ai_socktype == SOCK_STREAM && ai->ai_protocol == 0)
    ai->ai_protocol = IPPROTO_TCP;
  if(ai->ai_socktype == SOCK_DGRAM && ai->ai_protocol == 0)
    ai->ai_protocol = IPPROTO_UDP;
  ai->ai_addrlen = sizeof(*sin);
  ai->ai_addr = (struct sockaddr*)sin;
  ai->ai_next = 0;

  if((flags & AI_CANONNAME) && hostname != 0) {
    int n;
    char *canon;
    n = strlen(hostname);
    canon = (char*)malloc((size_t)n + 1);
    if(canon == 0) {
      free(sin);
      free(ai);
      return EAI_MEMORY;
    }
    memmove(canon, hostname, n + 1);
    ai->ai_canonname = canon;
  }

  *res = ai;
  return 0;
}

void
freeaddrinfo(struct addrinfo *res)
{
  struct addrinfo *next;

  while(res) {
    next = res->ai_next;
    if(res->ai_addr)
      free(res->ai_addr);
    if(res->ai_canonname)
      free(res->ai_canonname);
    free(res);
    res = next;
  }
}

const char *
gai_strerror(int errcode)
{
  switch(errcode) {
  case 0:
    return "Success";
  case EAI_AGAIN:
    return "Temporary failure in name resolution";
  case EAI_BADFLAGS:
    return "Invalid value for ai_flags";
  case EAI_FAIL:
    return "Non-recoverable failure in name resolution";
  case EAI_FAMILY:
    return "Address family not supported";
  case EAI_MEMORY:
    return "Memory allocation failure";
  case EAI_NONAME:
    return "Name or service not known";
  case EAI_SERVICE:
    return "Service not supported";
  case EAI_SOCKTYPE:
    return "Socket type not supported";
  case EAI_SYSTEM:
    return "System error";
  default:
    return "Resolver error";
  }
}

int
getnameinfo(const struct sockaddr *sa, socklen_t salen,
            char *host, socklen_t hostlen,
            char *serv, socklen_t servlen, int flags)
{
  const struct sockaddr_in *sin;
  struct in_addr in;
  char *name;
  int port;

  if(sa == 0)
    return EAI_SYSTEM;
  if(sa->sa_family != AF_INET)
    return EAI_FAMILY;
  if(salen < (socklen_t)sizeof(struct sockaddr_in))
    return EAI_FAIL;

  sin = (const struct sockaddr_in*)sa;

  if(host && hostlen > 0) {
    if((flags & NI_NUMERICHOST) == 0) {
      /* auxv6 currently only guarantees numeric reverse mapping here. */
    }
    in.s_addr = (uint)sin->sin_addr.s_addr;
    name = inet_ntoa(in);
    if(name == 0)
      return EAI_FAIL;
    if((int)strlen(name) + 1 > (int)hostlen)
      return EAI_FAIL;
    strcpy(host, name);
  }

  if(serv && servlen > 0) {
    char tmp[8];
    int n;
    port = (int)ntohs((ushort)sin->sin_port);
    n = 0;
    if(port == 0) {
      tmp[n++] = '0';
    } else {
      int v = port;
      int i;
      char rev[8];
      i = 0;
      while(v > 0 && i < (int)sizeof(rev)) {
        rev[i++] = (char)('0' + (v % 10));
        v /= 10;
      }
      while(i > 0)
        tmp[n++] = rev[--i];
    }
    tmp[n] = 0;
    if(n + 1 > (int)servlen)
      return EAI_FAIL;
    strcpy(serv, tmp);
  }

  return 0;
}