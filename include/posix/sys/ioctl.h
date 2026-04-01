/*
 * <sys/ioctl.h> - device I/O control
 */

#ifndef _SYS_IOCTL_H
#define _SYS_IOCTL_H

#include "sys/types.h"
#include "../../termios.h"   /* struct winsize is defined there */

/* Terminal window size ioctl codes (Linux-compatible) */
#define TIOCGWINSZ  0x5413  /* get window size → struct winsize * */
#define TIOCSWINSZ  0x5414  /* set window size ← struct winsize * */

/* Terminal session/foreground pgroup ioctls */
#define TIOCSCTTY   0x540E  /* make fd the controlling terminal */
#define TIOCGPGRP   0x540F  /* get foreground process group → pid_t * */
#define TIOCSPGRP   0x5410  /* set foreground process group ← pid_t * */

/* Query whether fd is a tty */
#define TIOCISATTY  0x5411  /* returns 1 if fd is a tty */

/* auxv6 virtual terminal controls */
#define TIOCGACTTTY 0x54A0  /* get active tty index -> int * */
#define TIOCSACTTTY 0x54A1  /* set active tty index <- int */
#define TIOCGNTTY   0x54A2  /* get number of virtual ttys -> int * */

/* Flush/drain */
#define TCFLSH      0x540B  /* flush pending input/output */

int ioctl(int fd, int request, ...);

#endif /* _SYS_IOCTL_H */
