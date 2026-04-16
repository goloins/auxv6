/*
 * <string.h> - String Operations
 *
 * POSIX.1-2017 / C11 compatible definitions
 */

#ifndef _STRING_H
#define _STRING_H

#include "stddef.h"

/*
 * Copying functions
 */
void       *memcpy(void *dest, const void *src, size_t n);
void       *memmove(void *dest, const void *src, size_t n);
char       *strcpy(char *dest, const char *src);
char       *strncpy(char *dest, const char *src, size_t n);
/* POSIX extensions */
char       *stpcpy(char *dest, const char *src);
char       *stpncpy(char *dest, const char *src, size_t n);

/*
 * Concatenation functions
 */
char       *strcat(char *dest, const char *src);
char       *strncat(char *dest, const char *src, size_t n);

/*
 * Comparison functions
 */
int         memcmp(const void *s1, const void *s2, size_t n);
int         strcmp(const char *s1, const char *s2);
int         strncmp(const char *s1, const char *s2, size_t n);
int         strcoll(const char *s1, const char *s2);
size_t      strxfrm(char *dest, const char *src, size_t n);
/* POSIX locale extensions */
int         strcoll_l(const char *s1, const char *s2, void *locale);
size_t      strxfrm_l(char *dest, const char *src, size_t n, void *locale);

/*
 * Search functions
 */
void       *memchr(const void *s, int c, size_t n);
char       *strchr(const char *s, int c);
size_t      strcspn(const char *s, const char *reject);
char       *strpbrk(const char *s, const char *accept);
char       *strrchr(const char *s, int c);
size_t      strspn(const char *s, const char *accept);
char       *strstr(const char *haystack, const char *needle);
char       *strtok(char *str, const char *delim);
char       *strtok_r(char *str, const char *delim, char **saveptr);
/* BSD extension */
void       *memrchr(const void *s, int c, size_t n);

/*
 * Miscellaneous functions
 */
void       *memset(void *s, int c, size_t n);
char       *strerror(int errnum);
char       *strerror_r(int errnum, char *buf, size_t buflen);
size_t      strlen(const char *s);
/* GNU/POSIX extensions */
size_t      strnlen(const char *s, size_t maxlen);
char       *strdup(const char *s);
char       *strndup(const char *s, size_t n);
/* GNU extension */
void       *mempcpy(void *dest, const void *src, size_t n);

/*
 * Signal description (POSIX)
 */
char       *strsignal(int sig);

/*
 * BSD string functions
 */
void        bzero(void *s, size_t n);
void        bcopy(const void *src, void *dest, size_t n);
int         bcmp(const void *s1, const void *s2, size_t n);
char       *index(const char *s, int c);
char       *rindex(const char *s, int c);
int         strcasecmp(const char *s1, const char *s2);
int         strncasecmp(const char *s1, const char *s2, size_t n);
size_t      strlcpy(char *dst, const char *src, size_t size);
size_t      strlcat(char *dst, const char *src, size_t size);

/*
 * Explicit memory operations (C11)
 */
void       *memset_explicit(void *s, int c, size_t n);

/*
 * Tokenization
 */
char       *strsep(char **stringp, const char *delim);

#endif /* _STRING_H */
