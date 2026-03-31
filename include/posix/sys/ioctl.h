/*
 * <sys/ioctl.h> - device I/O control
 *
 * Stub header for ported software.  Only TIOCGWINSZ is defined; the
 * actual ioctl() syscall is not yet implemented.
 */

#ifndef _SYS_IOCTL_H
#define _SYS_IOCTL_H

#include "sys/types.h"

/* Terminal window size */
struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

#define TIOCGWINSZ  0x5413  /* get window size */
#define TIOCSWINSZ  0x5414  /* set window size */

/* ioctl stub - returns -1 (unimplemented) */
int ioctl(int fd, unsigned long request, ...);

#endif /* _SYS_IOCTL_H */
