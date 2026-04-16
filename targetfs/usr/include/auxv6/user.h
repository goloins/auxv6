/*
 * auxv6/user.h - native auxv6 userland ABI surface
 *
 * This is the legacy catch-all header split out of include/user.h so the
 * standard header surface can be cleaned up incrementally. New code should
 * prefer standard headers first and include this header only for auxv6-native
 * extensions and direct syscall wrappers.
 */

#ifndef AUXV6_USER_API_H
#define AUXV6_USER_API_H

#include "types.h"
#include "stddef.h"
#include "sys/types.h"
#include "signal.h"
#include "sys/resource.h"
#include "wait.h"
#include "termios.h"
#include "sys/select.h"
#include "poll.h"
#include "limits.h"
#include "net.h"
#include "sys/socket.h"

struct stat;
struct dirent;
struct rtcdate;
struct timespec;
struct pollfd;

typedef struct {
	int si_signo;
	int si_code;
	int si_status;
	int si_pid;
} siginfo_t;

#define MOUNTINFO_NAME_MAX (NAME_MAX + 1)
#define MOUNTINFO_PATH_MAX PATH_MAX
#define MOUNTINFO_MAX MOUNT_MAX
#define NETIFINFO_NAME_MAX 16
#define NETIFINFO_MAX 16
#define ROUTEINFO_MAX NET_ROUTE_TABLE_MAX
#define ARPINFO_MAX NET_ARP_CACHE_MAX

#define ARP_FLAG_PENDING  0x1
#define ARP_FLAG_RESOLVED 0x2

#define LOOP_STATUS_MOUNTED 0x1

struct mountinfo {
  int dev;
  int flags;
  char fstype[MOUNTINFO_NAME_MAX];
  char path[MOUNTINFO_PATH_MAX];
};

struct netifinfo {
	uint if_index;
	char if_name[NETIFINFO_NAME_MAX];
	uint if_link_state;
	uint if_addr;
	uint if_netmask;
	uchar if_hwaddr[6];
	uint if_mtu;
	uint if_flags;
};

struct routeinfo {
	uint rt_dst;
	uint rt_mask;
	uint rt_gateway;
	uint rt_src;
	uint rt_flags;
	uint if_index;
};

struct arpinfo {
	uint ai_ip;
	uchar ai_mac[6];
	uint ai_flags;
	uint ai_expires;
	uint if_index;
};

pid_t fork(void);
void __auxv6_sys_exit(int status) __attribute__((noreturn));
void exit(int status) __attribute__((noreturn));
pid_t wait(int *status);
pid_t waitpid(pid_t pid, int *status, int options);
pid_t wait4(pid_t pid, int *status, int options, void *rusage);
int waitid(id_t idtype, id_t id, siginfo_t *info, int options);
int pipe(int pipefd[2]);
ssize_t write(int, const void*, size_t);
ssize_t read(int, void*, size_t);
int close(int);
int fsync(int fd);
int fdatasync(int fd);
void sync(void);
int kill(pid_t pid, int sig);
int exec(char*, char**);
int open(const char*, int, ...);
int mknod(const char*, int, short, short);
int unlink(const char*);
int __auxv6_sys_rmdir(const char*);
int rmdir(const char*);
int fstat(int fd, struct stat*);
int link(const char*, const char*);
int rename(const char*, const char*);
int ext2fail(int, int);
int fsfault(int, int, int);
int mkdir(const char*, mode_t);
int chmod(const char*, mode_t);
int chown(const char*, uid_t, gid_t);
int mountinfo(struct mountinfo *out, int max);
int netifinfo(struct netifinfo *out, int max);
int routeinfo(struct routeinfo *out, int max);
int arpinfo(struct arpinfo *out, int max);
int routeadd(uint dst, uint mask, uint gateway, uint src, int ifindex);
int routedel(uint dst, uint mask, int ifindex);
int netifsetaddr(int ifindex, uint addr, uint mask);
int mount(const char *path, const char *fstype, int flags,
		  const void *data, int datalen);
int umount(const char *path);
int devblocks(int dev);
int getdents(int fd, struct dirent *ents, int max);
int uname(char *buf, size_t size);
int chdir(const char*);
int dup(int);
int dup2(int oldfd, int newfd);
off_t lseek(int fd, off_t offset, int whence);
int   _llseek(int fd, uint offset_hi, uint offset_lo, loff_t *result, uint whence);
int truncate(const char *path, off_t length);
int ftruncate(int fd, off_t length);
int fcntl(int fd, int cmd, ...);
int symlink(const char *target, const char *linkpath);
ssize_t readlink(const char *path, char *buf, size_t bufsiz);
int lstat(const char *path, struct stat *st);
int loopsetup(int loopnum, const char *path, int offset, int nblocks);
int loopteardown(int loopnum);
int loopstatus(int loopnum, uint *backing_inum, uint *offset,
               uint *nblocks, uint *flags);
int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
		   struct timeval *timeout);
int poll(struct pollfd *fds, nfds_t nfds, int timeout);
int ioctl(int fd, int request, ...);
int kmsgread(void *buf, int max);
int date(struct rtcdate *r);
int halt(void);
pid_t getpid(void);
pid_t getppid(void);
pid_t getpgrp(void);
pid_t getsid(pid_t pid);
uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);
int __auxv6_sys_getcwd(char *buf, size_t size);
char* getcwd(char*, size_t);
int setpgid(pid_t pid, pid_t pgid);
pid_t setsid(void);
int setuid(uid_t uid);
int setgid(gid_t gid);
int setreuid(uid_t ruid, uid_t euid);
int setregid(gid_t rgid, gid_t egid);
int setresuid(uid_t ruid, uid_t euid, uid_t suid);
int setresgid(gid_t rgid, gid_t egid, gid_t sgid);
int getgroups(int gidsetsize, gid_t grouplist[]);
int setgroups(size_t size, const gid_t *list);
int getpeereid(int s, uid_t *euid, gid_t *egid);
int sigsend(int pid, int signo);
int sigaction(int signo, const struct sigaction *act, struct sigaction *oldact);
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int sigreturn(void);
unsigned int alarm(unsigned int seconds);
int __auxv6_sys_clock_gettime(int clock_id, struct timespec *tp);
int __auxv6_sys_clock_settime(int clock_id, const struct timespec *tp);
int __auxv6_sys_getrlimit(int resource, struct rlimit *rlp);
int __auxv6_sys_setrlimit(int resource, const struct rlimit *rlp);
int __auxv6_sys_utimensat(int dirfd, const char *path,
						  const struct timespec *times, int flags);
#ifndef _UNISTD_H
int tcsetpgrp(pid_t pgid);
pid_t tcgetpgrp(void);
#endif
int tcgetattr(int fd, struct termios *termios_p);
int tcsetattr(int fd, int optional_actions, const struct termios *termios_p);
void* sbrk(intptr_t);
void* vmreserve(int nbytes);
ssize_t getrandom(void *buf, size_t buflen, unsigned int flags);
unsigned int sleep(unsigned int);
int uptime(void);
int socket(int family, int type, int protocol);
int socketpair(int domain, int type, int protocol, int sv[2]);
int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
ssize_t send(int sockfd, const void *buf, size_t len, int flags);
ssize_t recv(int sockfd, void *buf, size_t len, int flags);
ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags);
ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags);
ssize_t recvtimeout(int sockfd, void *buf, size_t len, int timeout_ticks);
ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
					 const struct sockaddr *dst, socklen_t dstlen);
ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
						 struct sockaddr *src, socklen_t *srclen);
int listen(int sockfd, int backlog);
int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
int shutdown(int sockfd, int how);
int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

/*
 * Legacy auxv6 socket-call convenience forms.
 * Keep existing callers building while the public ABI is POSIX-compliant.
 */
#ifndef AUXV6_DISABLE_LEGACY_SOCKET_MACROS
#define bind(sockfd, addr, addrlen) \
	bind((sockfd), (const struct sockaddr *)(addr), (socklen_t)(addrlen))
#define connect(sockfd, addr, addrlen) \
	connect((sockfd), (const struct sockaddr *)(addr), (socklen_t)(addrlen))
#define send(sockfd, buf, len) \
	send((sockfd), (buf), (len), 0)
#define recv(sockfd, buf, len) \
	recv((sockfd), (buf), (len), 0)
#define accept(sockfd) \
	accept((sockfd), 0, 0)
#define sendto(sockfd, buf, len, flags, dst, dstlen) \
	sendto((sockfd), (buf), (len), (flags), (const struct sockaddr *)(dst), (socklen_t)(dstlen))
#define recvfrom(sockfd, buf, len, flags, src, srclen) \
	recvfrom((sockfd), (buf), (len), (flags), (struct sockaddr *)(src), (socklen_t *)(srclen))
#define getsockname(sockfd, addr, addrlen) \
	getsockname((sockfd), (struct sockaddr *)(addr), (socklen_t *)(addrlen))
#define getpeername(sockfd, addr, addrlen) \
	getpeername((sockfd), (struct sockaddr *)(addr), (socklen_t *)(addrlen))
#endif

int stat(const char*, struct stat*);
char* strcpy(char*, const char*);
void *memmove(void*, const void*, size_t);
char* strchr(const char*, int c);
int strcmp(const char*, const char*);
int strncmp(const char*, const char*, size_t);
int dprintf(int fd, const char *fmt, ...);
char* gets(char*, int max);
size_t strlen(const char*);
void* memset(void*, int, size_t);
void* malloc(size_t);
void free(void*);
int atoi(const char*);
char* readpass(char*, int);
int isatty(int fd);
char* ttyname(int fd);
int ttyname_r(int fd, char *buf, size_t buflen);
int resolve_ipv4(const char *name, uint *out);
int dns_nameservers(uint *servers, int max);
int dns_lookup_ipv4(const char *name, uint server, uint *out);
int openpty(int *amaster, int *aslave, char *name,
			const struct termios *termp,
			const struct winsize *winp);
char* ptsname(int fd);
int ptsname_r(int fd, char *buf, size_t buflen);

/*
 * Compatibility shim: legacy auxv6 code often called mkdir(path) while
 * POSIX code uses mkdir(path, mode). Support both call forms.
 */
static inline int __auxv6_mkdir_compat1(const char *path)
{
	return (mkdir)(path, 0777);
}

static inline int __auxv6_mkdir_compat2(const char *path, mode_t mode)
{
	return (mkdir)(path, mode);
}

#define __AUXV6_MKDIR_SELECT(_1, _2, NAME, ...) NAME
#define mkdir(...) __AUXV6_MKDIR_SELECT(__VA_ARGS__, __auxv6_mkdir_compat2, __auxv6_mkdir_compat1)(__VA_ARGS__)

#endif /* AUXV6_USER_API_H */