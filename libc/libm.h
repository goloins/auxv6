#ifndef _AUXV6_LIBM_H
#define _AUXV6_LIBM_H

#include "float.h"
#include "stdint.h"

#define WANT_ROUNDING 1
#define WANT_SNAN 0

#ifndef FP_NAN
#define FP_NAN 0
#define FP_INFINITE 1
#define FP_ZERO 2
#define FP_SUBNORMAL 3
#define FP_NORMAL 4
#endif

#ifndef FP_ILOGBNAN
#define FP_ILOGBNAN (-1 - 0x7fffffff)
#define FP_ILOGB0 FP_ILOGBNAN
#endif

static inline uint32_t
asuint(float x)
{
  union {
    float f;
    uint32_t i;
  } u = { x };
  return u.i;
}

static inline float
asfloat(uint32_t x)
{
  union {
    float f;
    uint32_t i;
  } u = { .i = x };
  return u.f;
}

static inline uint64_t
asuint64(double x)
{
  union {
    double f;
    uint64_t i;
  } u = { x };
  return u.i;
}

static inline double
asdouble(uint64_t x)
{
  union {
    double f;
    uint64_t i;
  } u = { .i = x };
  return u.f;
}

static inline float
fp_barrierf(float x)
{
  volatile float y = x;
  return y;
}

static inline double
fp_barrier(double x)
{
  volatile double y = x;
  return y;
}

static inline long double
fp_barrierl(long double x)
{
  volatile long double y = x;
  return y;
}

static inline float
eval_as_float(float x)
{
  volatile float y = x;
  return y;
}

static inline double
eval_as_double(double x)
{
  volatile double y = x;
  return y;
}

static inline long double
eval_as_long_double(long double x)
{
  volatile long double y = x;
  return y;
}

#define GET_FLOAT_WORD(i, d) do { (i) = asuint(d); } while (0)
#define SET_FLOAT_WORD(d, i) do { (d) = asfloat(i); } while (0)

#define EXTRACT_WORDS(hi, lo, d) do { \
  uint64_t __u = asuint64(d); \
  (hi) = (uint32_t)(__u >> 32); \
  (lo) = (uint32_t)__u; \
} while (0)

#define GET_HIGH_WORD(hi, d) do { \
  (hi) = (uint32_t)(asuint64(d) >> 32); \
} while (0)

#define GET_LOW_WORD(lo, d) do { \
  (lo) = (uint32_t)asuint64(d); \
} while (0)

#define INSERT_WORDS(d, hi, lo) do { \
  (d) = asdouble(((uint64_t)(hi) << 32) | (uint32_t)(lo)); \
} while (0)

#define SET_HIGH_WORD(d, hi) do { \
  uint64_t __u = asuint64(d); \
  __u = ((uint64_t)(hi) << 32) | (uint32_t)__u; \
  (d) = asdouble(__u); \
} while (0)

#define SET_LOW_WORD(d, lo) do { \
  uint64_t __u = asuint64(d); \
  __u = (__u & 0xffffffff00000000ULL) | (uint32_t)(lo); \
  (d) = asdouble(__u); \
} while (0)

#if LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384
typedef union {
  long double f;
  struct {
    uint64_t m;
    uint16_t se;
  } i;
} ldshape;
#endif

int __fpclassifyf(float x);
int __fpclassify(double x);
int __fpclassifyl(long double x);

int __signbitf(float x);
int __signbit(double x);
int __signbitl(long double x);

double __math_xflow(uint32_t sign, double y);
float __math_xflowf(uint32_t sign, float y);
double __math_uflow(uint32_t sign);
float __math_uflowf(uint32_t sign);
double __math_oflow(uint32_t sign);
float __math_oflowf(uint32_t sign);
double __math_divzero(uint32_t sign);
float __math_divzerof(uint32_t sign);
double __math_invalid(double x);
float __math_invalidf(float x);
long double __math_invalidl(long double x);

extern int __signgam;

#endif