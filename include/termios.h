#ifndef XV6_TERMIOS_H
#define XV6_TERMIOS_H

#include "types.h"

/* Number of control characters */
#define NCCS 19

struct termios {
  uint  c_iflag;       /* input mode flags */
  uint  c_oflag;       /* output mode flags */
  uint  c_cflag;       /* control mode flags */
  uint  c_lflag;       /* local mode flags */
  uchar c_cc[NCCS];    /* control characters */
};

/* Terminal window size (also visible to kernel via this header) */
struct winsize {
  unsigned short ws_row;
  unsigned short ws_col;
  unsigned short ws_xpixel;
  unsigned short ws_ypixel;
};

/* tcsetattr optional_actions */
#define TCSANOW   0   /* change immediately */
#define TCSADRAIN 1   /* change after draining output */
#define TCSAFLUSH 2   /* change after draining; discard pending input */

/* tcflush queue selectors */
#define TCIFLUSH  0   /* flush pending input */
#define TCOFLUSH  1   /* flush pending output */
#define TCIOFLUSH 2   /* flush both input and output */

/* c_cc array indices */
#define VINTR    0    /* interrupt character (^C) */
#define VQUIT    1    /* quit character (^\) */
#define VERASE   2    /* erase character (DEL) */
#define VKILL    3    /* kill-line character (^U) */
#define VEOF     4    /* end-of-file character (^D) */
#define VTIME    5    /* timeout in tenths of a second (non-canonical) */
#define VMIN     6    /* minimum bytes for non-canonical read */
#define VSWTC    7    /* switch character (unused) */
#define VSTART   8    /* start output character (^Q) */
#define VSTOP    9    /* stop output character (^S) */
#define VSUSP   10    /* suspend character (^Z) */
#define VEOL    11    /* additional end-of-line character */
#define VREPRINT 12   /* reprint unread characters */
#define VDISCARD 13   /* discard pending output */
#define VWERASE 14    /* word-erase character */
#define VLNEXT  15    /* literal-next character */
#define VEOL2   16    /* second end-of-line character */
/* [17], [18] reserved */

/* c_iflag bits */
#define IGNBRK  0x00000001U  /* ignore break condition */
#define BRKINT  0x00000002U  /* signal interrupt on break */
#define IGNPAR  0x00000004U  /* ignore parity errors */
#define PARMRK  0x00000008U  /* mark parity errors */
#define INPCK   0x00000010U  /* enable input parity check */
#define ISTRIP  0x00000020U  /* strip 8th bit */
#define INLCR   0x00000040U  /* map NL to CR on input */
#define IGNCR   0x00000080U  /* ignore CR */
#define ICRNL   0x00000100U  /* map CR to NL on input */
#define IUCLC   0x00000200U  /* map upper to lower on input */
#define IXON    0x00000400U  /* enable XON/XOFF flow control on output */
#define IXANY   0x00000800U  /* allow any char to restart output */
#define IXOFF   0x00001000U  /* enable XON/XOFF flow control on input */
#define IMAXBEL 0x00002000U  /* ring bell on input queue full */
#define IUTF8   0x00004000U  /* input is UTF-8 */

/* c_oflag bits */
#define OPOST   0x00000001U  /* enable output processing */
#define OLCUC   0x00000002U  /* map lower to upper on output */
#define ONLCR   0x00000004U  /* map NL to CR+NL on output */
#define OCRNL   0x00000008U  /* map CR to NL on output */
#define ONOCR   0x00000010U  /* no CR at column 0 */
#define ONLRET  0x00000020U  /* NL performs CR function */
#define OFILL   0x00000040U  /* use fill characters for delay */
#define OFDEL   0x00000080U  /* fill char is DEL */

/* c_cflag bits */
#define CS5     0x00000000U  /* character size: 5 bits */
#define CS6     0x00000010U  /* character size: 6 bits */
#define CS7     0x00000020U  /* character size: 7 bits */
#define CS8     0x00000030U  /* character size: 8 bits */
#define CSIZE   0x00000030U  /* character size mask */
#define CSTOPB  0x00000040U  /* 2 stop bits */
#define CREAD   0x00000080U  /* receiver enable */
#define PARENB  0x00000100U  /* parity enable */
#define PARODD  0x00000200U  /* odd parity */
#define HUPCL   0x00000400U  /* hang up on last close */
#define CLOCAL  0x00000800U  /* ignore modem status lines */

/* Baud rate constants (stored in c_cflag low nibble and extended) */
#define B0      0000000U
#define B50     0000001U
#define B75     0000002U
#define B110    0000003U
#define B134    0000004U
#define B150    0000005U
#define B200    0000006U
#define B300    0000007U
#define B600    0000010U
#define B1200   0000011U
#define B1800   0000012U
#define B2400   0000013U
#define B4800   0000014U
#define B9600   0000015U
#define B19200  0000016U
#define B38400  0000017U
#define B57600  0010001U
#define B115200 0010002U
#define B230400 0010003U

/* c_lflag bits */
#define ISIG    0x00000001U  /* enable signals */
#define ICANON  0x00000002U  /* canonical input (line editing) */
#define XCASE   0x00000004U  /* canonical upper/lower case */
#define ECHO    0x00000008U  /* enable echo */
#define ECHOE   0x00000010U  /* echo erase as backspace */
#define ECHOK   0x00000020U  /* echo NL after kill character */
#define ECHONL  0x00000040U  /* echo NL even when ECHO is off */
#define NOFLSH  0x00000080U  /* disable flush after interrupt or quit */
#define TOSTOP  0x00000100U  /* send SIGTTOU for background output */
#define ECHOCTL 0x00000200U  /* echo control chars as ^X */
#define ECHOPRT 0x00000400U  /* echo erased chars */
#define ECHOKE  0x00000800U  /* visual erase for kill */
#define FLUSHO  0x00001000U  /* output being flushed */
#define PENDIN  0x00004000U  /* retype pending input on next read */
#define IEXTEN  0x00008000U  /* enable implementation-defined input processing */

#endif /* XV6_TERMIOS_H */