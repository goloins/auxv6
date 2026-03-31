/*
 * <stdio.h> - minimal stdio for ported software
 *
 * auxv6 does not have a C standard library.  This header provides:
 *  - BUFSIZ, EOF, NULL constants (safe for any file that only needs those)
 *  - vsnprintf / snprintf / sprintf declarations (implemented in user/posix.c)
 *
 * WARNING: Do NOT declare printf, fprintf, fopen, fclose, FILE, stdout,
 * stderr, or stdin here — they conflict with auxv6's native I/O model.
 * Code that uses those is gated by USE_GLIBC_STDIO / #ifdef notyet and
 * must not be compiled for this target.
 */

#ifndef _STDIO_H
#define _STDIO_H

#include "stddef.h"      /* size_t, NULL */
#include "stdarg.h"      /* va_list */

/* Buffer size used by dash's input/output layers */
#ifndef BUFSIZ
#define BUFSIZ    512
#endif

/* End-of-file sentinel */
#ifndef EOF
#define EOF       (-1)
#endif

/* Seek whence constants (also in unistd.h; harmless to repeat here) */
#ifndef SEEK_SET
#define SEEK_SET  0
#define SEEK_CUR  1
#define SEEK_END  2
#endif

/*
 * Formatted-string functions (implementations in user/posix.c).
 * These follow standard C99 semantics.
 */
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int  snprintf(char *buf, size_t size, const char *fmt, ...);
int   sprintf(char *buf,              const char *fmt, ...);
int  vsprintf(char *buf,              const char *fmt, va_list ap);

/* sscanf stub - returns 0, for code that optionally uses it */
int sscanf(const char *str, const char *fmt, ...);

#endif /* _STDIO_H */
