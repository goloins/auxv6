/*
 * <sys/ioctl.h> - device I/O control
 */

#ifndef _SYS_IOCTL_H
#define _SYS_IOCTL_H

#include "sys/types.h"
#include "../../termios.h"   /* struct winsize is defined there */

/* termios ioctl compatibility (Linux-compatible request values) */
#define TCGETS     0x5401  /* get terminal attributes -> struct termios * */
#define TCSETS     0x5402  /* set terminal attributes immediately */
#define TCSETSW    0x5403  /* set terminal attributes after drain */
#define TCSETSF    0x5404  /* set terminal attributes after drain + flush */

/* Terminal window size ioctl codes (Linux-compatible) */
#define TIOCGWINSZ  0x5413  /* get window size → struct winsize * */
#define TIOCSWINSZ  0x5414  /* set window size ← struct winsize * */

/* Terminal session/foreground pgroup ioctls */
#define TIOCSCTTY   0x540E  /* make fd the controlling terminal */
#define TIOCGPGRP   0x540F  /* get foreground process group → pid_t * */
#define TIOCSPGRP   0x5410  /* set foreground process group ← pid_t * */

/* Queue state (Linux-compatible aliases) */
#define TIOCOUTQ    0x5411  /* output queue size -> int * */
#define FIONREAD    0x541B  /* bytes available to read -> int * */
#define TIOCINQ     FIONREAD

/* auxv6 virtual terminal controls */
#define TIOCGACTTTY 0x54A0  /* get active tty index -> int * */
#define TIOCSACTTTY 0x54A1  /* set active tty index <- int */
#define TIOCGNTTY   0x54A2  /* get number of virtual ttys -> int * */

/* auxv6-specific tty query (kept out of Linux-occupied request space) */
#define TIOCISATTY  0x54A3  /* returns 1 if fd is a tty */
#define TIOCGPTN    0x80045430 /* get PTY number from ptmx master -> int * */

/* Flush/drain */
#define TCFLSH      0x540B  /* flush pending input/output */

int ioctl(int fd, int request, ...);

#endif /* _SYS_IOCTL_H */
