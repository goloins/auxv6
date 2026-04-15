/*
 * <utmp.h> - legacy user accounting interface
 */

#ifndef _UTMP_H
#define _UTMP_H

#include "utmpx.h"

struct utmp {
  short ut_type;
  pid_t ut_pid;
  char  ut_line[UT_LINESIZE];
  char  ut_id[4];
  char  ut_user[UT_NAMESIZE];
  char  ut_host[UT_HOSTSIZE];
  struct timeval ut_tv;
};

void setutent(void);
void endutent(void);
struct utmp *getutent(void);
struct utmp *getutid(const struct utmp *id);
struct utmp *getutline(const struct utmp *line);
struct utmp *pututline(const struct utmp *ut);

void updwtmp(const char *wtmp_file, const struct utmp *ut);
void logwtmp(const char *line, const char *name, const char *host);

#endif /* _UTMP_H */
