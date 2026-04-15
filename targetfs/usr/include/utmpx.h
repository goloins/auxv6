/*
 * <utmpx.h> - user accounting database entries
 */

#ifndef _UTMPX_H
#define _UTMPX_H

#include "sys/types.h"
#include "sys/time.h"

#ifndef UT_LINESIZE
#define UT_LINESIZE 32
#endif

#ifndef UT_NAMESIZE
#define UT_NAMESIZE 32
#endif

#ifndef UT_HOSTSIZE
#define UT_HOSTSIZE 64
#endif

#define EMPTY         0
#define RUN_LVL       1
#define BOOT_TIME     2
#define NEW_TIME      3
#define OLD_TIME      4
#define INIT_PROCESS  5
#define LOGIN_PROCESS 6
#define USER_PROCESS  7
#define DEAD_PROCESS  8

struct utmpx {
  short ut_type;
  pid_t ut_pid;
  char  ut_line[UT_LINESIZE];
  char  ut_id[4];
  char  ut_user[UT_NAMESIZE];
  char  ut_host[UT_HOSTSIZE];
  struct timeval ut_tv;
};

void setutxent(void);
void endutxent(void);
struct utmpx *getutxent(void);
struct utmpx *getutxid(const struct utmpx *id);
struct utmpx *getutxline(const struct utmpx *line);
struct utmpx *pututxline(const struct utmpx *ut);

void updwtmpx(const char *wtmpx_file, const struct utmpx *utx);

#endif /* _UTMPX_H */
