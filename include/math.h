#ifndef _AUXV6_MATH_H
#define _AUXV6_MATH_H

/* Minimal math.h stub for auxv6 */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ceil-like function for floats */
static inline float ceilf(float x) {
  int i = (int)x;
  return (x > (float)i) ? (float)(i + 1) : (float)i;
}

static inline double ceil(double x) {
  int i = (int)x;
  return (x > (double)i) ? (double)(i + 1) : (double)i;
}

static inline float floorf(float x) {
  return (float)(int)x;
}

static inline double floor(double x) {
  return (double)(int)x;
}

#endif  /* _AUXV6_MATH_H */
