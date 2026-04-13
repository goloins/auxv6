#include "syslog.h"
#include "stdio.h"
#include "string.h"
#include "stdarg.h"
#include "unistd.h"

/*
 * syslog.c — POSIX syslog implementation for auxv6.
 *
 * There is no syslog daemon; messages are formatted and written to
 * stderr (fd 2). Both the standard POSIX interface and the BSD
 * reentrant _r variants are implemented.
 */

#define STDERR_FD   2
#define LOG_PRIMASK 0x07        /* mask to extract priority from facility|priority */
#define BUFSIZE     512

/* Global state for the non-reentrant interface. */
static const char *g_tag  = (const char *)0;
static int         g_fac  = LOG_USER;
static int         g_stat = 0;
static int         g_mask = 0xff;   /* all priorities enabled by default */

void
openlog(const char *ident, int option, int facility)
{
    g_tag  = ident;
    g_stat = option;
    g_fac  = facility;
}

void
closelog(void)
{
    g_tag = (const char *)0;
}

int
setlogmask(int maskpri)
{
    int old = g_mask;
    if (maskpri != 0)
        g_mask = maskpri;
    return old;
}

/*
 * vsyslog_r — core formatting/emit routine used by all variants.
 *
 * data == NULL → use global state (called from the non-_r interface).
 */
void
vsyslog_r(int priority, struct syslog_data *data, const char *fmt, va_list ap)
{
    char buf[BUFSIZE];
    int  pri  = priority & LOG_PRIMASK;
    int  mask = data ? data->log_mask : g_mask;
    const char *tag  = data ? data->log_tag  : g_tag;
    int  stat = data ? data->log_stat : g_stat;
    int  n = 0;

    if (!(mask & LOG_MASK(pri)))
        return;

    if (tag && *tag)
        n += snprintf(buf + n, BUFSIZE - n, "%s", tag);
    if (stat & LOG_PID)
        n += snprintf(buf + n, BUFSIZE - n, "[%d]", getpid());
    if (n > 0 && n < BUFSIZE - 2) {
        buf[n++] = ':';
        buf[n++] = ' ';
    }
    if (n < BUFSIZE - 1)
        n += vsnprintf(buf + n, BUFSIZE - n, fmt, ap);
    /* ensure the message ends with a newline */
    if (n > 0 && buf[n - 1] != '\n' && n < BUFSIZE) {
        buf[n++] = '\n';
    }
    write(STDERR_FD, buf, n);
}

void
syslog_r(int priority, struct syslog_data *data, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsyslog_r(priority, data, fmt, ap);
    va_end(ap);
}

void
vsyslog(int priority, const char *fmt, va_list ap)
{
    vsyslog_r(priority, (struct syslog_data *)0, fmt, ap);
}

void
syslog(int priority, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsyslog_r(priority, (struct syslog_data *)0, fmt, ap);
    va_end(ap);
}
