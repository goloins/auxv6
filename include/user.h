#ifndef _USER_H_
#define _USER_H_

struct stat;
struct dirent;
struct rtcdate;
struct sockaddr_in;
struct pollfd;
#include "signal.h"
#include "wait.h"
#include "termios.h"
#include "sys/select.h"
#include "poll.h"

typedef struct {
	int si_signo;
	int si_code;
	int si_status;
	int si_pid;
} siginfo_t;

#define MOUNTINFO_NAME_MAX 8
#define MOUNTINFO_PATH_MAX 32
#define MOUNTINFO_MAX 8
#define NETIFINFO_NAME_MAX 16
#define NETIFINFO_MAX 16
#define ROUTEINFO_MAX 32
#define ARPINFO_MAX 32

#define ARP_FLAG_PENDING  0x1
#define ARP_FLAG_RESOLVED 0x2

struct mountinfo {
  int dev;
  int flags;
  char fstype[MOUNTINFO_NAME_MAX];
  char path[MOUNTINFO_PATH_MAX];
};

struct netifinfo {
	uint if_index;
	char if_name[NETIFINFO_NAME_MAX];
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

// system calls
int fork(void);
int exit(void) __attribute__((noreturn));
int wait(void);
int waitpid(int pid, int *status, int options);
int wait4(int pid, int *status, int options, void *rusage);
int waitid(int idtype, int id, siginfo_t *info, int options);
int pipe(int*);
int write(int, const void*, int);
int read(int, void*, int);
int close(int);
int kill(int pid, int sig);
int exec(char*, char**);
int open(const char*, int);
int mknod(const char*, int, short, short);
int unlink(const char*);
int fstat(int fd, struct stat*);
int link(const char*, const char*);
int rename(const char*, const char*);
int ext2fail(int, int);
int fsfault(int, int, int);
int mkdir(const char*);
int chmod(const char*, int);
int chown(const char*, int, int);
int mountinfo(struct mountinfo *out, int max);
int netifinfo(struct netifinfo *out, int max);
int routeinfo(struct routeinfo *out, int max);
int arpinfo(struct arpinfo *out, int max);
int routeadd(uint dst, uint mask, uint gateway, uint src, int ifindex);
int routedel(uint dst, uint mask, int ifindex);
int netifsetaddr(int ifindex, uint addr, uint mask);
int mount(const char *path, const char *fstype, int flags);
int umount(const char *path);
int devblocks(int dev);
int getdents(int fd, struct dirent *ents, int max);
int uname(char *buf, int size);
int chdir(const char*);
int dup(int);
int dup2(int oldfd, int newfd);
int lseek(int fd, int offset, int whence);
int fcntl(int fd, int cmd, ...);
int symlink(const char *target, const char *linkpath);
int readlink(const char *path, char *buf, int bufsiz);
int lstat(const char *path, struct stat *st);
int loopsetup(int loopnum, const char *path, int offset, int nblocks);
int loopteardown(int loopnum);
int loopstatus(int loopnum, uint *backing_inum, uint *nblocks);
int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
		   struct timeval *timeout);
int poll(struct pollfd *fds, nfds_t nfds, int timeout);
int getpid(void);
int getppid(void);
int getpgrp(void);
int getsid(int pid);
int getuid(void);
int getgid(void);
int getcwd(char*, int);
int setpgid(int pid, int pgid);
int setsid(void);
int setuid(int uid);
int setgid(int gid);
int sigsend(int pid, int signo);
int sigaction(int signo, const struct sigaction *act, struct sigaction *oldact);
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int sigreturn(void);
int alarm(int seconds);
int tcsetpgrp(int pgid);
int tcgetpgrp(void);
int tcgetattr(int fd, struct termios *termios_p);
int tcsetattr(int fd, int optional_actions, const struct termios *termios_p);
char* sbrk(int);
int sleep(int);
int uptime(void);
int socket(int family, int type, int protocol);
int bind(int sockfd, struct sockaddr_in *addr, int addrlen);
int connect(int sockfd, struct sockaddr_in *addr, int addrlen);
int send(int sockfd, const void *buf, int len);
int recv(int sockfd, void *buf, int len);
int recvtimeout(int sockfd, void *buf, int len, int timeout_ticks);
int listen(int sockfd, int backlog);
int accept(int sockfd);

// ulib.c
int stat(const char*, struct stat*);
char* strcpy(char*, const char*);
void *memmove(void*, const void*, int);
char* strchr(const char*, char c);
int strcmp(const char*, const char*);
int strncmp(const char*, const char*, uint);
void printf(int, const char*, ...);
char* gets(char*, int max);
uint strlen(const char*);
void* memset(void*, int, uint);
void* malloc(uint);
void free(void*);
int atoi(const char*);
char* readpass(char*, int);
int resolve_ipv4(const char *name, uint *out);
int dns_nameservers(uint *servers, int max);
int dns_lookup_ipv4(const char *name, uint server, uint *out);

#endif
