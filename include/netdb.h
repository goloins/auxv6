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

struct hostent *gethostbyname(const char *name);
struct hostent *gethostbyaddr(const void *addr, socklen_t len, int type);
const char *hstrerror(int err);

#endif /* _NETDB_H */