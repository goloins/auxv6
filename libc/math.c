/*
 * libc/math.c - core math functions needed by POSIX ports
 */

#include "types.h"
#include "errno.h"
#include "libm.h"
#include "math.h"

static inline float
fabsf_local(float x)
{
  return asfloat(asuint(x) & 0x7fffffffU);
}

static inline double
fabs_local(double x)
{
  return asdouble(asuint64(x) & 0x7fffffffffffffffULL);
}

static inline long double
fabsl_local(long double x)
{
#if LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384
  ldshape u = { x };
  u.i.se &= 0x7fffU;
  return u.f;
#else
  return (long double)fabs_local((double)x);
#endif
}

static inline double
fmax_local(double x, double y)
{
  if (__fpclassify(x) == FP_NAN)
    return y;
  if (__fpclassify(y) == FP_NAN)
    return x;
  if (x > y)
    return x;
  if (y > x)
    return y;
  return __signbit(x) ? y : x;
}

static inline float
fmaxf_local(float x, float y)
{
  if (__fpclassifyf(x) == FP_NAN)
    return y;
  if (__fpclassifyf(y) == FP_NAN)
    return x;
  if (x > y)
    return x;
  if (y > x)
    return y;
  return __signbitf(x) ? y : x;
}

static inline long double
fmaxl_local(long double x, long double y)
{
#if LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384
  if (__fpclassifyl(x) == FP_NAN)
    return y;
  if (__fpclassifyl(y) == FP_NAN)
    return x;
  if (x > y)
    return x;
  if (y > x)
    return y;
  return __signbitl(x) ? y : x;
#else
  return (long double)fmax_local((double)x, (double)y);
#endif
}

static inline double
copysign_local(double x, double y)
{
  return __builtin_copysign(x, y);
}

static inline float
copysignf_local(float x, float y)
{
  return __builtin_copysignf(x, y);
}

static inline long double
copysignl_local(long double x, long double y)
{
  return __builtin_copysignl(x, y);
}

static inline float
fminf_local(float x, float y)
{
  if (__fpclassifyf(x) == FP_NAN)
    return y;
  if (__fpclassifyf(y) == FP_NAN)
    return x;
  if (x < y)
    return x;
  if (y < x)
    return y;
  return __signbitf(x) ? x : y;
}

static inline double
fmin_local(double x, double y)
{
  if (__fpclassify(x) == FP_NAN)
    return y;
  if (__fpclassify(y) == FP_NAN)
    return x;
  if (x < y)
    return x;
  if (y < x)
    return y;
  return __signbit(x) ? x : y;
}

static inline long double
fminl_local(long double x, long double y)
{
#if LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384
  if (__fpclassifyl(x) == FP_NAN)
    return y;
  if (__fpclassifyl(y) == FP_NAN)
    return x;
  if (x < y)
    return x;
  if (y < x)
    return y;
  return __signbitl(x) ? x : y;
#else
  return (long double)fmin_local((double)x, (double)y);
#endif
}

static inline double
pow2_double(int n)
{
  return asdouble((uint64_t)(n + 1023) << 52);
}

static inline float
pow2_float(int n)
{
  return asfloat((uint32_t)(n + 127) << 23);
}

double
frexp(double x, int *exp)
{
  uint64_t ix;
  uint64_t sign;
  int e;

  ix = asuint64(x);
  e = (int)((ix >> 52) & 0x7ff);
  if (e == 0) {
    if ((ix << 1) == 0 || __fpclassify(x) != FP_SUBNORMAL) {
      if (exp)
        *exp = 0;
      return x;
    }
    x *= 0x1p54;
    ix = asuint64(x);
    e = (int)((ix >> 52) & 0x7ff) - 54;
  } else if (e == 0x7ff) {
    if (exp)
      *exp = 0;
    return x;
  }

  if (exp)
    *exp = e - 1022;
  sign = ix & 0x8000000000000000ULL;
  ix = (ix & 0x000fffffffffffffULL) | 0x3fe0000000000000ULL | sign;
  return asdouble(ix);
}

float
frexpf(float x, int *exp)
{
  uint32_t ix;
  uint32_t sign;
  int e;

  ix = asuint(x);
  e = (int)((ix >> 23) & 0xff);
  if (e == 0) {
    if ((ix << 1) == 0 || __fpclassifyf(x) != FP_SUBNORMAL) {
      if (exp)
        *exp = 0;
      return x;
    }
    x *= 0x1p25f;
    ix = asuint(x);
    e = (int)((ix >> 23) & 0xff) - 25;
  } else if (e == 0xff) {
    if (exp)
      *exp = 0;
    return x;
  }

  if (exp)
    *exp = e - 126;
  sign = ix & 0x80000000U;
  ix = (ix & 0x007fffffU) | 0x3f000000U | sign;
  return asfloat(ix);
}

long double
frexpl(long double x, int *exp)
{
#if LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384
  ldshape u;
  uint16_t se;

  u.f = x;
  se = u.i.se & 0x7fffU;
  if (se == 0) {
    if (u.i.m == 0 || __fpclassifyl(x) != FP_SUBNORMAL) {
      if (exp)
        *exp = 0;
      return x;
    }
    x *= 0x1p64L;
    u.f = x;
    se = u.i.se & 0x7fffU;
    if (exp)
      *exp = (int)se - 16382 - 64;
  } else if (se == 0x7fffU) {
    if (exp)
      *exp = 0;
    return x;
  } else {
    if (exp)
      *exp = (int)se - 16382;
  }

  u.i.se = (u.i.se & 0x8000U) | 0x3ffeU;
  return u.f;
#else
  if (exp)
    *exp = 0;
  return (long double)frexp((double)x, exp);
#endif
}

int
ilogb(double x)
{
  uint64_t ix;
  int e;

  if (__fpclassify(x) == FP_ZERO || __fpclassify(x) == FP_NAN)
    return FP_ILOGBNAN;
  if (!finite(x))
    return 0x7fffffff;

  ix = asuint64(fabs_local(x));
  e = (int)((ix >> 52) & 0x7ff);
  if (e == 0) {
    x *= 0x1p54;
    ix = asuint64(fabs_local(x));
    e = (int)((ix >> 52) & 0x7ff) - 54;
  }
  return e - 1023;
}

int
ilogbf(float x)
{
  uint32_t ix;
  int e;

  if (__fpclassifyf(x) == FP_ZERO || __fpclassifyf(x) == FP_NAN)
    return FP_ILOGBNAN;
  if (!finitef(x))
    return 0x7fffffff;

  ix = asuint(fabsf_local(x));
  e = (int)((ix >> 23) & 0xff);
  if (e == 0) {
    x *= 0x1p25f;
    ix = asuint(fabsf_local(x));
    e = (int)((ix >> 23) & 0xff) - 25;
  }
  return e - 127;
}

int
ilogbl(long double x)
{
#if LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384
  ldshape u;
  int e;

  if (__fpclassifyl(x) == FP_ZERO || __fpclassifyl(x) == FP_NAN)
    return FP_ILOGBNAN;
  if (__fpclassifyl(x) == FP_INFINITE)
    return 0x7fffffff;

  u.f = fabsl_local(x);
  e = (int)(u.i.se & 0x7fffU);
  if (e == 0) {
    x *= 0x1p64L;
    u.f = fabsl_local(x);
    e = (int)(u.i.se & 0x7fffU) - 64;
  }
  return e - 16383;
#else
  return ilogb((double)x);
#endif
}

double
logb(double x)
{
  int e;

  if (__fpclassify(x) == FP_ZERO)
    return -HUGE_VAL;
  if (__fpclassify(x) == FP_NAN)
    return x;
  if (!finite(x))
    return x;
  e = ilogb(x);
  return (double)e;
}

float
logbf(float x)
{
  int e;

  if (__fpclassifyf(x) == FP_ZERO)
    return -HUGE_VALF;
  if (__fpclassifyf(x) == FP_NAN)
    return x;
  if (!finitef(x))
    return x;
  e = ilogbf(x);
  return (float)e;
}

long double
logbl(long double x)
{
  int e;

  if (__fpclassifyl(x) == FP_ZERO)
    return -HUGE_VALL;
  if (__fpclassifyl(x) == FP_NAN)
    return x;
  if (__fpclassifyl(x) == FP_INFINITE)
    return x;
  e = ilogbl(x);
  return (long double)e;
}

double
scalbn(double x, int n)
{
  uint64_t ix;
  uint64_t sign;
  uint64_t mant;
  int e;

  if (!finite(x) || x == 0.0)
    return x;

  ix = asuint64(x);
  sign = ix & 0x8000000000000000ULL;
  e = (int)((ix >> 52) & 0x7ff);
  if (e == 0) {
    x *= 0x1p54;
    ix = asuint64(x);
    e = (int)((ix >> 52) & 0x7ff) - 54;
  }

  e += n;
  if (e >= 0x7ff)
    return __math_oflow((unsigned int)(sign >> 63));
  if (e > 0) {
    ix = sign | ((uint64_t)e << 52) | (ix & 0x000fffffffffffffULL);
    return asdouble(ix);
  }
  if (e <= -52)
    return __math_uflow((unsigned int)(sign >> 63));

  mant = (ix & 0x000fffffffffffffULL) | 0x0010000000000000ULL;
  ix = sign | (mant >> (1 - e));
  return asdouble(ix);
}

float
scalbnf(float x, int n)
{
  uint32_t ix;
  uint32_t sign;
  uint32_t mant;
  int e;

  if (!finitef(x) || x == 0.0f)
    return x;

  ix = asuint(x);
  sign = ix & 0x80000000U;
  e = (int)((ix >> 23) & 0xff);
  if (e == 0) {
    x *= 0x1p25f;
    ix = asuint(x);
    e = (int)((ix >> 23) & 0xff) - 25;
  }

  e += n;
  if (e >= 0xff)
    return __math_oflowf(sign >> 31);
  if (e > 0) {
    ix = sign | ((uint32_t)e << 23) | (ix & 0x007fffffU);
    return asfloat(ix);
  }
  if (e <= -23)
    return __math_uflowf(sign >> 31);

  mant = (ix & 0x007fffffU) | 0x00800000U;
  ix = sign | (mant >> (1 - e));
  return asfloat(ix);
}

long double
scalbnl(long double x, int n)
{
#if LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384
  ldshape u;
  uint64_t mant;
  int e;

  if (__fpclassifyl(x) != FP_NORMAL && __fpclassifyl(x) != FP_SUBNORMAL)
    return x;

  u.f = x;
  e = (int)(u.i.se & 0x7fffU);
  if (e == 0) {
    x *= 0x1p64L;
    u.f = x;
    e = (int)(u.i.se & 0x7fffU) - 64;
  }

  e += n;
  if (e >= 0x7fff)
    return __math_oflow((unsigned int)(u.i.se >> 15));
  if (e > 0) {
    u.i.se = (u.i.se & 0x8000U) | (uint16_t)e;
    return u.f;
  }
  if (e <= -63)
    return __math_uflow((unsigned int)(u.i.se >> 15));

  mant = u.i.m | (1ULL << 63);
  u.i.m = mant >> (1 - e);
  u.i.se = u.i.se & 0x8000U;
  return u.f;
#else
  return (long double)scalbn((double)x, n);
#endif
}

double
ldexp(double x, int n)
{
  return scalbn(x, n);
}

float
ldexpf(float x, int n)
{
  return scalbnf(x, n);
}

long double
ldexpl(long double x, int n)
{
  return scalbnl(x, n);
}

double
scalbln(double x, long n)
{
  return scalbn(x, (int)n);
}

float
scalblnf(float x, long n)
{
  return scalbnf(x, (int)n);
}

long double
scalblnl(long double x, long n)
{
  return scalbnl(x, (int)n);
}

double
modf(double x, double *iptr)
{
  uint64_t ix;
  int e;

  if (__fpclassify(x) == FP_NAN) {
    *iptr = x;
    return x;
  }
  if (!finite(x)) {
    *iptr = x;
    return copysign_local(0.0, x);
  }

  ix = asuint64(x);
  e = (int)((ix >> 52) & 0x7ff) - 1023;
  if (e < 0) {
    *iptr = copysign_local(0.0, x);
    return x;
  }
  if (e >= 52) {
    *iptr = x;
    return copysign_local(0.0, x);
  }

  ix &= (1ULL << (52 - e)) - 1;
  if (ix == 0) {
    *iptr = x;
    return copysign_local(0.0, x);
  }

  *iptr = asdouble((asuint64(x) & ~((1ULL << (52 - e)) - 1)));
  return x - *iptr;
}

float
modff(float x, float *iptr)
{
  uint32_t ix;
  int e;

  if (__fpclassifyf(x) == FP_NAN) {
    *iptr = x;
    return x;
  }
  if (!finitef(x)) {
    *iptr = x;
    return copysignf_local(0.0f, x);
  }

  ix = asuint(x);
  e = (int)((ix >> 23) & 0xff) - 127;
  if (e < 0) {
    *iptr = copysignf_local(0.0f, x);
    return x;
  }
  if (e >= 23) {
    *iptr = x;
    return copysignf_local(0.0f, x);
  }

  if ((ix & ((1U << (23 - e)) - 1)) == 0) {
    *iptr = x;
    return copysignf_local(0.0f, x);
  }

  *iptr = asfloat(ix & ~((1U << (23 - e)) - 1));
  return x - *iptr;
}

long double
modfl(long double x, long double *iptr)
{
#if LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384
  ldshape u;
  int e;

  if (__fpclassifyl(x) == FP_NAN) {
    *iptr = x;
    return x;
  }
  if (__fpclassifyl(x) == FP_INFINITE) {
    *iptr = x;
    return copysignl_local(0.0L, x);
  }
  if (__fpclassifyl(x) == FP_ZERO) {
    *iptr = x;
    return x;
  }

  u.f = x;
  e = (int)(u.i.se & 0x7fffU) - 16383;
  if (e < 0) {
    *iptr = copysignl_local(0.0L, x);
    return x;
  }
  if (e >= 63) {
    *iptr = x;
    return copysignl_local(0.0L, x);
  }

  if ((u.i.m & ((1ULL << (63 - e)) - 1)) == 0) {
    *iptr = x;
    return copysignl_local(0.0L, x);
  }

  u.i.m &= ~((1ULL << (63 - e)) - 1);
  *iptr = u.f;
  return x - *iptr;
#else
  return (long double)modf((double)x, (double *)iptr);
#endif
}

static inline float
fdimf_local(float x, float y)
{
  if (__fpclassifyf(x) == FP_NAN)
    return x;
  if (__fpclassifyf(y) == FP_NAN)
    return y;
  return x > y ? x - y : 0.0f;
}

static inline double
fdim_local(double x, double y)
{
  if (__fpclassify(x) == FP_NAN)
    return x;
  if (__fpclassify(y) == FP_NAN)
    return y;
  return x > y ? x - y : 0.0;
}

static inline long double
fdiml_local(long double x, long double y)
{
#if LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384
  if (__fpclassifyl(x) == FP_NAN)
    return x;
  if (__fpclassifyl(y) == FP_NAN)
    return y;
  return x > y ? x - y : 0.0L;
#else
  return (long double)fdim_local((double)x, (double)y);
#endif
}

int
finite(double x)
{
  return __fpclassify(x) != FP_NAN && __fpclassify(x) != FP_INFINITE;
}

int
finitef(float x)
{
  return __fpclassifyf(x) != FP_NAN && __fpclassifyf(x) != FP_INFINITE;
}

double
fabs(double x)
{
  return fabs_local(x);
}

float
fabsf(float x)
{
  return fabsf_local(x);
}

long double
fabsl(long double x)
{
  return fabsl_local(x);
}

double
copysign(double x, double y)
{
  return copysign_local(x, y);
}

float
copysignf(float x, float y)
{
  return copysignf_local(x, y);
}

long double
copysignl(long double x, long double y)
{
  return copysignl_local(x, y);
}

double
fmax(double x, double y)
{
  return fmax_local(x, y);
}

float
fmaxf(float x, float y)
{
  return fmaxf_local(x, y);
}

long double
fmaxl(long double x, long double y)
{
  return fmaxl_local(x, y);
}

double
fmin(double x, double y)
{
  return fmin_local(x, y);
}

float
fminf(float x, float y)
{
  return fminf_local(x, y);
}

long double
fminl(long double x, long double y)
{
  return fminl_local(x, y);
}

double
fdim(double x, double y)
{
  return fdim_local(x, y);
}

float
fdimf(float x, float y)
{
  return fdimf_local(x, y);
}

long double
fdiml(long double x, long double y)
{
  return fdiml_local(x, y);
}

static inline double
trunc_local(double x)
{
  double iptr;

  (void)modf(x, &iptr);
  return iptr;
}

static inline float
truncf_local(float x)
{
  float iptr;

  (void)modff(x, &iptr);
  return iptr;
}

static inline long double
truncl_local(long double x)
{
  long double iptr;

  (void)modfl(x, &iptr);
  return iptr;
}

static inline double
round_even_local(double x)
{
  double iptr;
  double frac;
  uint64_t bits;

  frac = modf(x, &iptr);
  if (frac > 0.5)
    return iptr + 1.0;
  if (frac < -0.5)
    return iptr - 1.0;
  if (frac == 0.5 || frac == -0.5) {
    bits = asuint64(iptr);
    if (bits & 1ULL)
      return iptr + (frac > 0.0 ? 1.0 : -1.0);
  }
  return iptr;
}

static inline float
roundf_even_local(float x)
{
  float iptr;
  float frac;
  uint32_t bits;

  frac = modff(x, &iptr);
  if (frac > 0.5f)
    return iptr + 1.0f;
  if (frac < -0.5f)
    return iptr - 1.0f;
  if (frac == 0.5f || frac == -0.5f) {
    bits = asuint(iptr);
    if (bits & 1U)
      return iptr + (frac > 0.0f ? 1.0f : -1.0f);
  }
  return iptr;
}

static inline long double
roundl_even_local(long double x)
{
  long double iptr;
  long double frac;

  frac = modfl(x, &iptr);
  if (frac > 0.5L)
    return iptr + 1.0L;
  if (frac < -0.5L)
    return iptr - 1.0L;
  if (frac == 0.5L || frac == -0.5L) {
#if LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384
    ldshape u;

    u.f = iptr;
    if (u.i.m & 1ULL)
      return iptr + (frac > 0.0L ? 1.0L : -1.0L);
#endif
  }
  return iptr;
}

double
trunc(double x)
{
  return trunc_local(x);
}

float
truncf(float x)
{
  return truncf_local(x);
}

long double
truncl(long double x)
{
  return truncl_local(x);
}

double
rint(double x)
{
  return round_even_local(x);
}

float
rintf(float x)
{
  return roundf_even_local(x);
}

long double
rintl(long double x)
{
  return roundl_even_local(x);
}

double
nearbyint(double x)
{
  return round_even_local(x);
}

float
nearbyintf(float x)
{
  return roundf_even_local(x);
}

long double
nearbyintl(long double x)
{
  return roundl_even_local(x);
}

double
round(double x)
{
  return x >= 0.0 ? trunc_local(x + 0.5) : -trunc_local(-x + 0.5);
}

float
roundf(float x)
{
  return x >= 0.0f ? truncf_local(x + 0.5f) : -truncf_local(-x + 0.5f);
}

long double
roundl(long double x)
{
  return x >= 0.0L ? truncl_local(x + 0.5L) : -truncl_local(-x + 0.5L);
}

long
lrint(double x)
{
  double y = round_even_local(x);
  if (y > (double)0x7fffffffL || y < (double)-0x80000000L) {
    errno = ERANGE;
    return y < 0.0 ? (long)-0x80000000L : (long)0x7fffffffL;
  }
  return (long)y;
}

long
lrintf(float x)
{
  return lrint((double)x);
}

long
lrintl(long double x)
{
  return lrint((double)x);
}

long long
llrint(double x)
{
  double y = round_even_local(x);

  if (y > (double)9223372036854775807.0 || y < (double)-9223372036854775808.0) {
    errno = ERANGE;
    return y < 0.0 ? (-9223372036854775807LL - 1) : 9223372036854775807LL;
  }
  return (long long)y;
}

long long
llrintf(float x)
{
  return llrint((double)x);
}

long long
llrintl(long double x)
{
  return llrint((double)x);
}

long
lround(double x)
{
  double y = round(x);

  if (y > (double)0x7fffffffL || y < (double)-0x80000000L) {
    errno = ERANGE;
    return y < 0.0 ? (long)-0x80000000L : (long)0x7fffffffL;
  }
  return (long)y;
}

long
lroundf(float x)
{
  return lround((double)x);
}

long
lroundl(long double x)
{
  return lround((double)x);
}

long long
llround(double x)
{
  return llrint(x);
}

long long
llroundf(float x)
{
  return llrint((double)x);
}

long long
llroundl(long double x)
{
  return llrint((double)x);
}

double
exp(double x)
{
  return __builtin_exp(x);
}

float
expf(float x)
{
  return __builtin_expf(x);
}

long double
expl(long double x)
{
  return __builtin_expl(x);
}

double
exp2(double x)
{
  return __builtin_exp2(x);
}

float
exp2f(float x)
{
  return __builtin_exp2f(x);
}

long double
exp2l(long double x)
{
  return __builtin_exp2l(x);
}

double
sin(double x)
{
  return __builtin_sin(x);
}

float
sinf(float x)
{
  return __builtin_sinf(x);
}

long double
sinl(long double x)
{
  return __builtin_sinl(x);
}

double
cos(double x)
{
  return __builtin_cos(x);
}

float
cosf(float x)
{
  return __builtin_cosf(x);
}

long double
cosl(long double x)
{
  return __builtin_cosl(x);
}

double
tan(double x)
{
  return __builtin_tan(x);
}

float
tanf(float x)
{
  return __builtin_tanf(x);
}

long double
tanl(long double x)
{
  return __builtin_tanl(x);
}

double
atan(double x)
{
  return __builtin_atan(x);
}

float
atanf(float x)
{
  return __builtin_atanf(x);
}

long double
atanl(long double x)
{
  return __builtin_atanl(x);
}

double
atan2(double x, double y)
{
  return __builtin_atan2(x, y);
}

float
atan2f(float x, float y)
{
  return __builtin_atan2f(x, y);
}

long double
atan2l(long double x, long double y)
{
  return __builtin_atan2l(x, y);
}

double
asin(double x)
{
  return __builtin_asin(x);
}

float
asinf(float x)
{
  return __builtin_asinf(x);
}

long double
asinl(long double x)
{
  return __builtin_asinl(x);
}

double
acos(double x)
{
  return __builtin_acos(x);
}

float
acosf(float x)
{
  return __builtin_acosf(x);
}

long double
acosl(long double x)
{
  return __builtin_acosl(x);
}

double
sinh(double x)
{
  return __builtin_sinh(x);
}

float
sinhf(float x)
{
  return __builtin_sinhf(x);
}

long double
sinhl(long double x)
{
  return __builtin_sinhl(x);
}

double
cosh(double x)
{
  return __builtin_cosh(x);
}

float
coshf(float x)
{
  return __builtin_coshf(x);
}

long double
coshl(long double x)
{
  return __builtin_coshl(x);
}

double
tanh(double x)
{
  return __builtin_tanh(x);
}

float
tanhf(float x)
{
  return __builtin_tanhf(x);
}

long double
tanhl(long double x)
{
  return __builtin_tanhl(x);
}

double
asinh(double x)
{
  return __builtin_asinh(x);
}

float
asinhf(float x)
{
  return __builtin_asinhf(x);
}

long double
asinhl(long double x)
{
  return __builtin_asinhl(x);
}

double
acosh(double x)
{
  return __builtin_acosh(x);
}

float
acoshf(float x)
{
  return __builtin_acoshf(x);
}

long double
acoshl(long double x)
{
  return __builtin_acoshl(x);
}

double
atanh(double x)
{
  return __builtin_atanh(x);
}

float
atanhf(float x)
{
  return __builtin_atanhf(x);
}

long double
atanhl(long double x)
{
  return __builtin_atanhl(x);
}

double
erf(double x)
{
  return __builtin_erf(x);
}

float
erff(float x)
{
  return __builtin_erff(x);
}

long double
erfl(long double x)
{
  return __builtin_erfl(x);
}

double
erfc(double x)
{
  return __builtin_erfc(x);
}

float
erfcf(float x)
{
  return __builtin_erfcf(x);
}

long double
erfcl(long double x)
{
  return __builtin_erfcl(x);
}

double
tgamma(double x)
{
  return __builtin_tgamma(x);
}

float
tgammaf(float x)
{
  return __builtin_tgammaf(x);
}

long double
tgammal(long double x)
{
  return __builtin_tgammal(x);
}

double
lgamma(double x)
{
  double y;

  y = __builtin_lgamma(x);
  __signgam = __builtin_tgamma(x) < 0.0 ? -1 : 1;
  return y;
}

float
lgammaf(float x)
{
  float y;

  y = __builtin_lgammaf(x);
  __signgam = __builtin_tgammaf(x) < 0.0f ? -1 : 1;
  return y;
}

long double
lgammal(long double x)
{
  long double y;

  y = __builtin_lgammal(x);
  __signgam = __builtin_tgammal(x) < 0.0L ? -1 : 1;
  return y;
}

double
remainder(double x, double y)
{
  return __builtin_remainder(x, y);
}

float
remainderf(float x, float y)
{
  return __builtin_remainderf(x, y);
}

long double
remainderl(long double x, long double y)
{
  return __builtin_remainderl(x, y);
}

double
remquo(double x, double y, int *quo)
{
  return __builtin_remquo(x, y, quo);
}

float
remquof(float x, float y, int *quo)
{
  return __builtin_remquof(x, y, quo);
}

long double
remquol(long double x, long double y, int *quo)
{
  return __builtin_remquol(x, y, quo);
}

double
drem(double x, double y)
{
  return remainder(x, y);
}

float
dremf(float x, float y)
{
  return remainderf(x, y);
}

double
scalb(double x, double y)
{
  return scalbn(x, (int)y);
}

float
scalbf(float x, float y)
{
  return scalbnf(x, (int)y);
}

double
significand(double x)
{
  int e;

  if (!finite(x) || x == 0.0)
    return x;
  e = ilogb(x);
  return scalbn(x, -e);
}

float
significandf(float x)
{
  int e;

  if (!finitef(x) || x == 0.0f)
    return x;
  e = ilogbf(x);
  return scalbnf(x, -e);
}

double
j0(double x)
{
  return __builtin_j0(x);
}

double
j1(double x)
{
  return __builtin_j1(x);
}

double
jn(int n, double x)
{
  return __builtin_jn(n, x);
}

double
y0(double x)
{
  return __builtin_y0(x);
}

double
y1(double x)
{
  return __builtin_y1(x);
}

double
yn(int n, double x)
{
  return __builtin_yn(n, x);
}

float
j0f(float x)
{
  return __builtin_j0f(x);
}

float
j1f(float x)
{
  return __builtin_j1f(x);
}

float
jnf(int n, float x)
{
  return __builtin_jnf(n, x);
}

float
y0f(float x)
{
  return __builtin_y0f(x);
}

float
y1f(float x)
{
  return __builtin_y1f(x);
}

float
ynf(int n, float x)
{
  return __builtin_ynf(n, x);
}

double
lgamma_r(double x, int *signp)
{
  double y;

  y = lgamma(x);
  if (signp)
    *signp = __signgam;
  return y;
}

float
lgammaf_r(float x, int *signp)
{
  float y;

  y = lgammaf(x);
  if (signp)
    *signp = __signgam;
  return y;
}

long double
lgammal_r(long double x, int *signp)
{
  long double y;

  y = lgammal(x);
  if (signp)
    *signp = __signgam;
  return y;
}

double
expm1(double x)
{
  return __builtin_expm1(x);
}

float
expm1f(float x)
{
  return __builtin_expm1f(x);
}

long double
expm1l(long double x)
{
  return __builtin_expm1l(x);
}

double
sqrt(double x)
{
  return __builtin_sqrt(x);
}

float
sqrtf(float x)
{
  return __builtin_sqrtf(x);
}

long double
sqrtl(long double x)
{
  return __builtin_sqrtl(x);
}

double
cbrt(double x)
{
  return __builtin_cbrt(x);
}

float
cbrtf(float x)
{
  return __builtin_cbrtf(x);
}

long double
cbrtl(long double x)
{
  return __builtin_cbrtl(x);
}

double
hypot(double x, double y)
{
  return __builtin_hypot(x, y);
}

float
hypotf(float x, float y)
{
  return __builtin_hypotf(x, y);
}

long double
hypotl(long double x, long double y)
{
  return __builtin_hypotl(x, y);
}

double
log(double x)
{
  return __builtin_log(x);
}

float
logf(float x)
{
  return __builtin_logf(x);
}

long double
logl(long double x)
{
  return __builtin_logl(x);
}

double
log10(double x)
{
  return __builtin_log10(x);
}

float
log10f(float x)
{
  return __builtin_log10f(x);
}

long double
log10l(long double x)
{
  return __builtin_log10l(x);
}

double
log1p(double x)
{
  return __builtin_log1p(x);
}

float
log1pf(float x)
{
  return __builtin_log1pf(x);
}

long double
log1pl(long double x)
{
  return __builtin_log1pl(x);
}

double
log2(double x)
{
  return __builtin_log2(x);
}

float
log2f(float x)
{
  return __builtin_log2f(x);
}

long double
log2l(long double x)
{
  return __builtin_log2l(x);
}

double
pow(double x, double y)
{
  return __builtin_pow(x, y);
}

float
powf(float x, float y)
{
  return __builtin_powf(x, y);
}

long double
powl(long double x, long double y)
{
  return __builtin_powl(x, y);
}

void
sincos(double x, double *sinp, double *cosp)
{
  if (sinp)
    *sinp = sin(x);
  if (cosp)
    *cosp = cos(x);
}

void
sincosf(float x, float *sinp, float *cosp)
{
  if (sinp)
    *sinp = sinf(x);
  if (cosp)
    *cosp = cosf(x);
}

void
sincosl(long double x, long double *sinp, long double *cosp)
{
  if (sinp)
    *sinp = sinl(x);
  if (cosp)
    *cosp = cosl(x);
}

double
exp10(double x)
{
  return pow(10.0, x);
}

float
exp10f(float x)
{
  return powf(10.0f, x);
}

long double
exp10l(long double x)
{
  return powl(10.0L, x);
}

double
pow10(double x)
{
  return exp10(x);
}

float
pow10f(float x)
{
  return exp10f(x);
}

long double
pow10l(long double x)
{
  return exp10l(x);
}
