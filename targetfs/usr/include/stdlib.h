/*
 * <stdlib.h> - Standard Library Definitions
 *
 * POSIX.1-2017 / C11 compatible definitions
 *
 * Tranche 1 note:
 * - the standard helper surface is being filled in around the post-ABI libc
 *   split; the remaining large gaps are time, locale, and broader
 *   wide-character support rather than the basic process/path/tempfile layer.
 */

#ifndef _STDLIB_H
#define _STDLIB_H

#include "stddef.h"
#include "stdint.h"

/* Exit codes */
#define EXIT_SUCCESS    0
#define EXIT_FAILURE    1

/* Limits */
#define RAND_MAX        2147483647
#define MB_CUR_MAX      1           /* Single-byte encoding only */

/* Division structures */
typedef struct {
    int quot;       /* Quotient */
    int rem;        /* Remainder */
} div_t;

typedef struct {
    long quot;
    long rem;
} ldiv_t;

typedef struct {
    long long quot;
    long long rem;
} lldiv_t;

/*
 * String conversion functions
 */
double      atof(const char *nptr);
int         atoi(const char *nptr);
long        atol(const char *nptr);
long long   atoll(const char *nptr);
double      strtod(const char *nptr, char **endptr);
float       strtof(const char *nptr, char **endptr);
long double strtold(const char *nptr, char **endptr);
long        strtol(const char *nptr, char **endptr, int base);
long long   strtoll(const char *nptr, char **endptr, int base);
unsigned long       strtoul(const char *nptr, char **endptr, int base);
unsigned long long  strtoull(const char *nptr, char **endptr, int base);

/*
 * Random number generation
 */
int         rand(void);
int         rand_r(unsigned int *seedp);
void        srand(unsigned int seed);
/* BSD/SVID */
long        random(void);
void        srandom(unsigned int seed);
char       *initstate(unsigned int seed, char *state, size_t n);
char       *setstate(char *state);

/*
 * Memory allocation
 */
void       *malloc(size_t size);
void       *calloc(size_t nmemb, size_t size);
void       *realloc(void *ptr, size_t size);
void        free(void *ptr);
void       *aligned_alloc(size_t alignment, size_t size);
/* BSD extension */
void       *reallocarray(void *ptr, size_t nmemb, size_t size);

/*
 * Program termination
 */
void        abort(void) __attribute__((noreturn));
#ifdef exit
#undef exit
#endif
void        exit(int status) __attribute__((noreturn));
void        _Exit(int status) __attribute__((noreturn));
int         atexit(void (*func)(void));
int         at_quick_exit(void (*func)(void));
void        quick_exit(int status) __attribute__((noreturn));

/*
 * Environment
 */
char       *getenv(const char *name);
int         setenv(const char *name, const char *value, int overwrite);
int         unsetenv(const char *name);
int         putenv(char *string);
int         clearenv(void);

/*
 * Searching and sorting
 */
void       *bsearch(const void *key, const void *base, size_t nmemb,
                    size_t size, int (*compar)(const void *, const void *));
void        qsort(void *base, size_t nmemb, size_t size,
                  int (*compar)(const void *, const void *));

/*
 * Integer arithmetic
 */
int         abs(int j);
long        labs(long j);
long long   llabs(long long j);
div_t       div(int numer, int denom);
ldiv_t      ldiv(long numer, long denom);
lldiv_t     lldiv(long long numer, long long denom);

/*
 * Multibyte/wide character conversion (minimal stub)
 */
int         mblen(const char *s, size_t n);
int         mbtowc(wchar_t *pwc, const char *s, size_t n);
int         wctomb(char *s, wchar_t wchar);
size_t      mbstowcs(wchar_t *dest, const char *src, size_t n);
size_t      wcstombs(char *dest, const wchar_t *src, size_t n);

/*
 * Pseudo-terminal functions (POSIX)
 */
int         posix_openpt(int flags);
int         grantpt(int fd);
int         unlockpt(int fd);
char       *ptsname(int fd);
int         ptsname_r(int fd, char *buf, size_t buflen);

/*
 * System functions
 */
int         system(const char *command);

/*
 * Temporary files
 */
char       *mktemp(char *template);
int         mkstemp(char *template);
char       *mkdtemp(char *template);
int         mkostemp(char *template, int flags);

/*
 * Path resolution
 */
char       *realpath(const char *path, char *resolved_path);

/* Global environment variable array (initialised in user/posix.c) */
extern char **environ;

/*
 * Cryptographically secure random number generation (Tranche 2)
 * ChaCha20-based CSPRNG with automatic kernel entropy seeding.
 */
uint32_t    arc4random(void);
void        arc4random_buf(void *buf, size_t n);
uint32_t    arc4random_uniform(uint32_t upper_bound);
void        arc4random_stir(void);

#endif /* _STDLIB_H */
