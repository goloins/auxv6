#ifndef XV6_WAIT_H
#define XV6_WAIT_H

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

#endif