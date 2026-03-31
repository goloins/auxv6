#ifndef _SYS_SELECT_H
#define _SYS_SELECT_H

#include "sys/types.h"
#include "sys/time.h"

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
           struct timeval *timeout);

#endif
