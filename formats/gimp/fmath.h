#ifndef BITPLANE_GIMP_FMATH_H
#define BITPLANE_GIMP_FMATH_H
/* The little floating point maths the compositor needs, so the codec needs
   no maths library. Accurate to about 1e-15 relative, far below what 8-bit
   output can show. Arguments are finite. */
#include <stdint.h>
#include <string.h>

static inline double fm_abs(double x)
{
    return x < 0.0 ? -x : x;
}

/* Natural log of x > 0. */
static inline double fm_log(double x)
{
    const double ln2 = 0.69314718055994530942;
    uint64_t bits;
    int e;
    double m, s, s2, term, sum;
    int k;

    memcpy(&bits, &x, sizeof bits);
    e = (int)((bits >> 52) & 0x7ff);
    if (e == 0) {
        /* Subnormal: scale into the normal range first. */
        x *= 4503599627370496.0;  /* 2^52 */
        memcpy(&bits, &x, sizeof bits);
        e = (int)((bits >> 52) & 0x7ff) - 52;
    }
    e -= 1023;
    bits = (bits & 0x000fffffffffffffull) | 0x3ff0000000000000ull;
    memcpy(&m, &bits, sizeof m);
    /* m in [1, 2); move to [sqrt(1/2), sqrt(2)). */
    if (m > 1.41421356237309504880) {
        m *= 0.5;
        e++;
    }
    /* log m = 2 atanh(s), s = (m - 1) / (m + 1), |s| < 0.172. */
    s = (m - 1.0) / (m + 1.0);
    s2 = s * s;
    term = s;
    sum = 0.0;
    for (k = 1; k < 30; k += 2) {
        sum += term / k;
        term *= s2;
    }
    return 2.0 * sum + e * ln2;
}

static inline double fm_exp(double y)
{
    const double ln2 = 0.69314718055994530942;
    double k, r, term, sum;
    uint64_t bits;
    long n;
    int i;

    if (y > 700.0)
        y = 700.0;
    if (y < -700.0)
        return 0.0;
    k = y / ln2;
    n = (long)(k < 0.0 ? k - 0.5 : k + 0.5);
    r = y - (double)n * ln2;
    term = 1.0;
    sum = 1.0;
    for (i = 1; i < 22; i++) {
        term *= r / i;
        sum += term;
    }
    bits = (uint64_t)(n + 1023) << 52;
    memcpy(&k, &bits, sizeof k);
    return sum * k;
}

/* x^a for x > 0. */
static inline double fm_pow(double x, double a)
{
    return fm_exp(a * fm_log(x));
}

static inline double fm_cbrt(double x)
{
    double y;
    if (x == 0.0)
        return 0.0;
    if (x < 0.0)
        return -fm_cbrt(-x);
    y = fm_exp(fm_log(x) / 3.0);
    return y - (y * y * y - x) / (3.0 * y * y);
}

static inline double fm_sqrt(double x)
{
    double y;
    if (x <= 0.0)
        return 0.0;
    y = fm_exp(fm_log(x) * 0.5);
    return 0.5 * (y + x / y);
}

static inline double fm_hypot(double a, double b)
{
    return fm_sqrt(a * a + b * b);
}
#endif
