/*
 * <sys/socket.h> - POSIX socket interface shim for auxv6
 *
 * This header provides the canonical POSIX socket prototypes and generic
 * sockaddr type while reusing the existing IPv4 socket core from socket.h.
 */

#ifndef _SYS_SOCKET_H
#define _SYS_SOCKET_H

#include "sys/types.h"
#include "../socket.h"

#ifndef PF_UNSPEC
#define PF_UNSPEC AF_UNSPEC
#endif
#ifndef PF_UNIX
#define PF_UNIX AF_UNIX
#endif
#ifndef PF_INET
#define PF_INET AF_INET
#endif

#ifndef AF_INET6
#define AF_INET6 10
#endif
#ifndef PF_INET6
#define PF_INET6 AF_INET6
#endif

#ifndef SOMAXCONN
#define SOMAXCONN 128
#endif

struct sockaddr_storage {
  sa_family_t ss_family;
  char __ss_pad[126];
};

/* Generic POSIX socket address type. */
#ifndef _NETDB_SOCKADDR_DECL
#define _NETDB_SOCKADDR_DECL
struct sockaddr {
  ushort sa_family;
  char   sa_data[14];
};
#endif

int socket(int domain, int type, int protocol);
int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int listen(int sockfd, int backlog);
int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

ssize_t send(int sockfd, const void *buf, size_t len, int flags);
ssize_t recv(int sockfd, void *buf, size_t len, int flags);

ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen);

int shutdown(int sockfd, int how);
int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

#endif /* _SYS_SOCKET_H */