#ifndef XV6_TERMIOS_H
#define XV6_TERMIOS_H

#include "types.h"

#define NCCS 8

struct termios {
  uint c_iflag;
  uint c_oflag;
  uint c_cflag;
  uint c_lflag;
  uchar c_cc[NCCS];
};

#define TCSANOW 0

// Minimal local-mode flags for current console behavior.
#define ECHO    0x00000008U
#define ICANON  0x00000002U

#endif