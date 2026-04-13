/*
 * <netdb.h> - network database operations (minimal truthful subset)
 *
 * auxv6 currently provides a small IPv4-only host database surface backed by
 * resolve_ipv4()/hosts/DNS helpers. getaddrinfo-style APIs are intentionally
 * not declared yet.
 */

#ifndef _NETDB_H
#define _NETDB_H

#include "sys/types.h"

/* Keep netdb.h compatible with legacy auxv6/user.h socket declarations. */
#ifndef AF_UNSPEC
#define AF_UNSPEC 0
#endif

#ifndef _NETDB_SOCKADDR_DECL
#define _NETDB_SOCKADDR_DECL
struct sockaddr {
    ushort sa_family;
    char   sa_data[14];
};
#endif

struct hostent {
    char  *h_name;      /* Official name of host. */
    char **h_aliases;   /* Alias list (NULL-terminated). */
    int    h_addrtype;  /* Host address type. */
    int    h_length;    /* Length of address in bytes. */
    char **h_addr_list; /* List of addresses (NULL-terminated). */
};

#define h_addr h_addr_list[0]

/* h_errno values */
#define HOST_NOT_FOUND 1
#define TRY_AGAIN      2
#define NO_RECOVERY    3
#define NO_DATA        4

extern int h_errno;

struct addrinfo {
    int              ai_flags;
    int              ai_family;
    int              ai_socktype;
    int              ai_protocol;
    socklen_t        ai_addrlen;
    struct sockaddr *ai_addr;
    char            *ai_canonname;
    struct addrinfo *ai_next;
};

/* getaddrinfo flags */
#define AI_PASSIVE      0x0001
#define AI_CANONNAME    0x0002
#define AI_NUMERICHOST  0x0004
#define AI_NUMERICSERV  0x0008
#define AI_ADDRCONFIG   0x0020

/* getnameinfo flags */
#define NI_NUMERICHOST  0x0001
#define NI_NUMERICSERV  0x0002
#define NI_NOFQDN       0x0004
#define NI_NAMEREQD     0x0008
#define NI_DGRAM        0x0010

#ifndef NI_MAXHOST
#define NI_MAXHOST 1025
#endif
#ifndef NI_MAXSERV
#define NI_MAXSERV 32
#endif

/* getaddrinfo error codes */
#define EAI_AGAIN       2
#define EAI_BADFLAGS    3
#define EAI_FAIL        4
#define EAI_FAMILY      5
#define EAI_MEMORY      6
#define EAI_NONAME      8
#define EAI_SERVICE     9
#define EAI_SOCKTYPE    10
#define EAI_SYSTEM      11

struct hostent *gethostbyname(const char *name);
struct hostent *gethostbyaddr(const void *addr, socklen_t len, int type);
const char *hstrerror(int err);

int getaddrinfo(const char *hostname, const char *service,
                const struct addrinfo *hints, struct addrinfo **res);
void freeaddrinfo(struct addrinfo *res);
const char *gai_strerror(int errcode);
int getnameinfo(const struct sockaddr *sa, socklen_t salen,
                char *host, socklen_t hostlen,
                char *serv, socklen_t servlen, int flags);

#endif /* _NETDB_H */