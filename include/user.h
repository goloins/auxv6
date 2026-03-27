struct stat;
struct rtcdate;
struct sockaddr_in;

#define SIGHUP   1
#define SIGINT   2
#define SIGQUIT  3
#define SIGCHLD  17
#define SIGTERM  15
#define SIGCONT  18
#define SIGSTOP  19
#define SIGTSTP  20
#define SIGKILL  9

#define SIG_DFL ((void(*)(int))0)
#define SIG_IGN ((void(*)(int))1)

#define WNOHANG    0x0001
#define WUNTRACED  0x0002
#define WCONTINUED 0x0004

#define WIFEXITED(s)    (((s) & 0xff) == 0)
#define WEXITSTATUS(s)  (((s) >> 8) & 0xff)
#define WIFSIGNALED(s)  (((s) & 0x7f) != 0 && ((s) & 0x7f) != 0x7f)
#define WTERMSIG(s)     ((s) & 0x7f)
#define WIFSTOPPED(s)   (((s) & 0xff) == 0x7f)
#define WSTOPSIG(s)     (((s) >> 8) & 0xff)
#define WIFCONTINUED(s) ((s) == 0xffff)

#define P_PID  1
#define P_PGID 2
#define P_ALL  3

#define CLD_EXITED    1
#define CLD_KILLED    2
#define CLD_STOPPED   3
#define CLD_CONTINUED 4

struct sigaction {
	void (*sa_handler)(int);
	uint sa_mask;
	int sa_flags;
};

typedef struct {
	int si_signo;
	int si_code;
	int si_status;
	int si_pid;
} siginfo_t;

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
int kill(int);
int exec(char*, char**);
int open(const char*, int);
int mknod(const char*, short, short);
int unlink(const char*);
int fstat(int fd, struct stat*);
int link(const char*, const char*);
int mkdir(const char*);
int chdir(const char*);
int dup(int);
int getpid(void);
int getppid(void);
int getpgrp(void);
int setpgid(int pid, int pgid);
int setsid(void);
int sigsend(int pid, int signo);
int sigaction(int signo, const struct sigaction *act, struct sigaction *oldact);
int tcsetpgrp(int pgid);
int tcgetpgrp(void);
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
