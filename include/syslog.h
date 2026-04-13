#ifndef _SYSLOG_H
#define _SYSLOG_H

#include "stdarg.h"

#define LOG_EMERG   0
#define LOG_ALERT   1
#define LOG_CRIT    2
#define LOG_ERR     3
#define LOG_WARNING 4
#define LOG_NOTICE  5
#define LOG_INFO    6
#define LOG_DEBUG   7

#define LOG_KERN    (0 << 3)
#define LOG_USER    (1 << 3)
#define LOG_LOCAL0  (16 << 3)
#define LOG_LOCAL1  (17 << 3)
#define LOG_LOCAL2  (18 << 3)
#define LOG_LOCAL3  (19 << 3)
#define LOG_LOCAL4  (20 << 3)
#define LOG_LOCAL5  (21 << 3)
#define LOG_LOCAL6  (22 << 3)
#define LOG_LOCAL7  (23 << 3)

#define LOG_PID     0x01
#define LOG_CONS    0x02
#define LOG_NDELAY  0x08
#define LOG_PERROR  0x20

#define LOG_MASK(pri) (1U << (pri))
#define LOG_UPTO(pri) ((1U << ((pri) + 1)) - 1U)

#ifndef HAVE_SYSLOG_R
#define HAVE_SYSLOG_R 1
#endif

struct syslog_data {
    int log_stat;
    const char *log_tag;
    int log_fac;
    int log_mask;
};

#define SYSLOG_DATA_INIT {0, (const char *)0, LOG_USER, 0xff}

void openlog(const char *ident, int option, int facility);
void closelog(void);
int setlogmask(int maskpri);
void syslog(int priority, const char *format, ...);
void vsyslog(int priority, const char *format, va_list ap);
void syslog_r(int priority, struct syslog_data *data, const char *format, ...);
void vsyslog_r(int priority, struct syslog_data *data, const char *format, va_list ap);

#endif
