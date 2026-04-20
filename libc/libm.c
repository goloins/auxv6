/*
 * libc/libm.c - private math helpers shared by future libm implementations
 */

#include "libm.h"

int __signgam = 0;
extern int signgam __attribute__((alias("__signgam")));

int
__fpclassifyf(float x)
{
  uint32_t ix;

  ix = asuint(x) & 0x7fffffffU;
  if (ix == 0)
    return FP_ZERO;
  if (ix < 0x00800000U)
    return FP_SUBNORMAL;
  if (ix >= 0x7f800000U)
    return ix == 0x7f800000U ? FP_INFINITE : FP_NAN;
  return FP_NORMAL;
}

int
__fpclassify(double x)
{
  uint64_t ix;

  ix = asuint64(x) & 0x7fffffffffffffffULL;
  if (ix == 0)
    return FP_ZERO;
  if (ix < 0x0010000000000000ULL)
    return FP_SUBNORMAL;
  if (ix >= 0x7ff0000000000000ULL)
    return ix == 0x7ff0000000000000ULL ? FP_INFINITE : FP_NAN;
  return FP_NORMAL;
}

int
__fpclassifyl(long double x)
{
#if LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384
  ldshape u = { x };
  uint16_t se = u.i.se & 0x7fffU;

  if (se == 0)
    return u.i.m ? FP_SUBNORMAL : FP_ZERO;
  if (se == 0x7fffU)
    return u.i.m ? FP_NAN : FP_INFINITE;
  return FP_NORMAL;
#else
  return __fpclassify((double)x);
#endif
}

int
__signbitf(float x)
{
  return (int)(asuint(x) >> 31);
}

int
__signbit(double x)
{
  return (int)(asuint64(x) >> 63);
}

int
__signbitl(long double x)
{
#if LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384
  ldshape u = { x };
  return (int)(u.i.se >> 15);
#else
  return __signbit((double)x);
#endif
}

double
__math_xflow(uint32_t sign, double y)
{
  return eval_as_double(fp_barrier(sign ? -y : y) * y);
}

float
__math_xflowf(uint32_t sign, float y)
{
  return eval_as_float(fp_barrierf(sign ? -y : y) * y);
}

double
__math_uflow(uint32_t sign)
{
  return __math_xflow(sign, 0x1p-767);
}

float
__math_uflowf(uint32_t sign)
{
  return __math_xflowf(sign, 0x1p-95f);
}

double
__math_oflow(uint32_t sign)
{
  return __math_xflow(sign, 0x1p769);
}

float
__math_oflowf(uint32_t sign)
{
  return __math_xflowf(sign, 0x1p97f);
}

double
__math_divzero(uint32_t sign)
{
  return eval_as_double(sign ? -1.0 / 0.0 : 1.0 / 0.0);
}

float
__math_divzerof(uint32_t sign)
{
  return eval_as_float(sign ? -1.0f / 0.0f : 1.0f / 0.0f);
}

double
__math_invalid(double x)
{
  return (x - x) / (x - x);
}

float
__math_invalidf(float x)
{
  return (x - x) / (x - x);
}

long double
__math_invalidl(long double x)
{
  return (x - x) / (x - x);
}