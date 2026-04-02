/*
 * <sys/ioctl.h> - device I/O control
 */

#ifndef AUXV6_SYS_IOCTL_H
#define AUXV6_SYS_IOCTL_H

#include "sys/types.h"
#include "termios.h"

#define TCGETS      0x5401
#define TCSETS      0x5402
#define TCSETSW     0x5403
#define TCSETSF     0x5404

#define TIOCGWINSZ  0x5413
#define TIOCSWINSZ  0x5414

#define TIOCSCTTY   0x540E
#define TIOCGPGRP   0x540F
#define TIOCSPGRP   0x5410

#define TIOCOUTQ    0x5411
#define FIONREAD    0x541B
#define TIOCINQ     FIONREAD

#define TIOCGACTTTY 0x54A0
#define TIOCSACTTTY 0x54A1
#define TIOCGNTTY   0x54A2

#define TIOCISATTY  0x54A3
#define TIOCGPTN    0x80045430

#define TCFLSH      0x540B

int ioctl(int fd, int request, ...);

#endif /* AUXV6_SYS_IOCTL_H */