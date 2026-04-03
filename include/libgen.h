/*
 * <libgen.h> - Definitions for Pattern Matching Functions
 *
 * POSIX.1-2017 compatible definitions.
 *
 * Note: basename() and dirname() modify the string passed to them;
 * callers should pass a writable copy.
 */

#ifndef _LIBGEN_H
#define _LIBGEN_H

char *basename(char *path);
char *dirname(char *path);

#endif /* _LIBGEN_H */
