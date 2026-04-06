/*
 * <sys/ioctl.h> - device I/O control
 */

#ifndef AUXV6_SYS_IOCTL_H
#define AUXV6_SYS_IOCTL_H

#include "sys/types.h"
#include "termios.h"
#include "audio_ioctl.h"

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

#define TIOCMGET    0x5415
#define TIOCMBIS    0x5416
#define TIOCMBIC    0x5417
#define TIOCMSET    0x5418

#define TIOCM_LE    0x001
#define TIOCM_DTR   0x002
#define TIOCM_RTS   0x004
#define TIOCM_ST    0x008
#define TIOCM_SR    0x010
#define TIOCM_CTS   0x020
#define TIOCM_CAR   0x040
#define TIOCM_CD    TIOCM_CAR
#define TIOCM_RNG   0x080
#define TIOCM_RI    TIOCM_RNG
#define TIOCM_DSR   0x100

#define TIOCGACTTTY 0x54A0
#define TIOCSACTTTY 0x54A1
#define TIOCGNTTY   0x54A2

#define TIOCISATTY  0x54A3
#define TIOCGPTN    0x80045430

#define TCFLSH      0x540B

/* Linux-compatible /dev/net/tun baseline ioctl subset */
#define TUNSETIFF      0x400454ca
#define TUNSETPERSIST  0x400454cb
#define TUNSETOWNER    0x400454cc
#define TUNSETGROUP    0x400454ce
#define TUNGETIFF      0x800454d2

/* Linux-compatible ifreq flags used with TUNSETIFF/TUNGETIFF */
#define IFF_TUN        0x0001
#define IFF_TAP        0x0002
#define IFF_NO_PI      0x1000

#ifndef IFNAMSIZ
#define IFNAMSIZ 16
#endif

struct ifreq {
	char ifr_name[IFNAMSIZ];
	short ifr_flags;
	char ifr_pad[14];
};

int ioctl(int fd, int request, ...);

#endif /* AUXV6_SYS_IOCTL_H */