/*
 * <inttypes.h> - fixed-width integer format macros
 */

#ifndef _INTTYPES_H
#define _INTTYPES_H

#include <stdint.h>

/* intmax_t / uintmax_t — largest integer type available on this platform */
typedef long long          intmax_t;
typedef unsigned long long uintmax_t;

/* Printf format macros */
#define PRId8    "d"
#define PRId16   "d"
#define PRId32   "d"
#define PRId64   "lld"
#define PRIdMAX  "lld"
#define PRIdPTR  "d"

#define PRIi8    "i"
#define PRIi16   "i"
#define PRIi32   "i"
#define PRIi64   "lli"
#define PRIiMAX  "lli"
#define PRIiPTR  "i"

#define PRIu8    "u"
#define PRIu16   "u"
#define PRIu32   "u"
#define PRIu64   "llu"
#define PRIuMAX  "llu"
#define PRIuPTR  "u"

#define PRIx8    "x"
#define PRIx16   "x"
#define PRIx32   "x"
#define PRIx64   "llx"
#define PRIxMAX  "llx"
#define PRIxPTR  "x"

#define PRIX8    "X"
#define PRIX16   "X"
#define PRIX32   "X"
#define PRIX64   "llX"
#define PRIXMAX  "llX"
#define PRIXPTR  "X"

/* Scanf format macros */
#define SCNd8    "d"
#define SCNd16   "d"
#define SCNd32   "d"
#define SCNd64   "lld"
#define SCNdMAX  "lld"
#define SCNdPTR  "d"

#define SCNu8    "u"
#define SCNu16   "u"
#define SCNu32   "u"
#define SCNu64   "llu"
#define SCNuMAX  "llu"
#define SCNuPTR  "u"

#define SCNx8    "x"
#define SCNx16   "x"
#define SCNx32   "x"
#define SCNx64   "llx"
#define SCNxMAX  "llx"
#define SCNxPTR  "x"

/* Conversion functions */
static inline intmax_t  imaxabs(intmax_t j) { return j<0 ? -j : j; }

/* Map to libc long-long variants */
#define strtoimax  strtoll
#define strtoumax  strtoull
#define wcstoimax  wcstoll
#define wcstoumax  wcstoull

#endif /* _INTTYPES_H */
