/*
 * <sys/socket.h> - POSIX socket interface shim for auxv6
 *
 * This header provides the canonical POSIX socket prototypes and generic
 * sockaddr type while reusing the existing IPv4 socket core from socket.h.
 */

#ifndef _SYS_SOCKET_H
#define _SYS_SOCKET_H

#include "sys/types.h"
#include "sys/uio.h"
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

#ifndef SCM_RIGHTS
#define SCM_RIGHTS 1
#endif

#ifndef MSG_CTRUNC
#define MSG_CTRUNC 0x08
#endif

struct cmsghdr {
  size_t cmsg_len;
  int    cmsg_level;
  int    cmsg_type;
};

struct msghdr {
  void         *msg_name;
  socklen_t     msg_namelen;
  struct iovec *msg_iov;
  int           msg_iovlen;
  void         *msg_control;
  socklen_t     msg_controllen;
  int           msg_flags;
};

struct ucred {
  pid_t pid;
  uid_t uid;
  gid_t gid;
};

#define __CMSG_ALIGN(len) (((len) + sizeof(size_t) - 1) & ~(sizeof(size_t) - 1))
#define CMSG_SPACE(len)   (__CMSG_ALIGN(sizeof(struct cmsghdr)) + __CMSG_ALIGN(len))
#define CMSG_LEN(len)     (__CMSG_ALIGN(sizeof(struct cmsghdr)) + (len))
#define CMSG_DATA(cmsg)   ((unsigned char *)(cmsg) + __CMSG_ALIGN(sizeof(struct cmsghdr)))

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
int socketpair(int domain, int type, int protocol, int sv[2]);
int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int listen(int sockfd, int backlog);
int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

ssize_t send(int sockfd, const void *buf, size_t len, int flags);
ssize_t recv(int sockfd, void *buf, size_t len, int flags);
ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags);
ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags);

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