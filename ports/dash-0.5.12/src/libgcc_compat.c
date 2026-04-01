#include <stdint.h>

/*
 * Minimal compiler-rt style helpers for 64-bit division/modulus on i386.
 * Some host toolchain configurations used for auxv6 do not provide these
 * symbols from the selected libgcc during the dash link step.
 */
static unsigned long long udivmod64(unsigned long long n,
                                    unsigned long long d,
                                    unsigned long long *r)
{
    unsigned long long q = 0;
    int shift = 0;

    if (d == 0) {
        if (r)
            *r = 0;
        return 0;
    }

    if (d > n) {
        if (r)
            *r = n;
        return 0;
    }

    while (d <= (n >> 1) && shift < 63) {
        d <<= 1;
        shift++;
    }

    while (shift >= 0) {
        if (n >= d) {
            n -= d;
            q |= (1ULL << shift);
        }
        d >>= 1;
        shift--;
    }

    if (r)
        *r = n;
    return q;
}

unsigned long long __udivdi3(unsigned long long a, unsigned long long b)
{
    return udivmod64(a, b, 0);
}

unsigned long long __umoddi3(unsigned long long a, unsigned long long b)
{
    unsigned long long rem;
    (void)udivmod64(a, b, &rem);
    return rem;
}

long long __divdi3(long long a, long long b)
{
    unsigned long long ua = (unsigned long long)a;
    unsigned long long ub = (unsigned long long)b;
    unsigned long long q;
    int neg;

    neg = ((a < 0) ^ (b < 0));
    if (a < 0)
        ua = (~ua) + 1;
    if (b < 0)
        ub = (~ub) + 1;

    q = udivmod64(ua, ub, 0);
    if (neg)
        q = (~q) + 1;

    return (long long)q;
}

long long __moddi3(long long a, long long b)
{
    unsigned long long ua = (unsigned long long)a;
    unsigned long long ub = (unsigned long long)b;
    unsigned long long rem;

    if (a < 0)
        ua = (~ua) + 1;
    if (b < 0)
        ub = (~ub) + 1;

    (void)udivmod64(ua, ub, &rem);
    if (a < 0)
        rem = (~rem) + 1;

    return (long long)rem;
}
