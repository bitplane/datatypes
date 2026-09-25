/* Composite an XCF's layers the way GIMP 3.2 merges visible layers:
   straight alpha in float, each layer blended in its mode's colour space,
   groups rendered on their own and rounded to the image's precision. */
#include "xcf.h"
#include "gimp.h"
#include "fmath.h"
#include <stdlib.h>
#include <string.h>

#define MAX_DEPTH 32
#define EPS 1e-6f

/* Colour spaces a band of float pixels can be in. Non-linear means the
   sRGB curve: GIMP's perceptual space, and the image's own curve for images
   without a colour profile. */
enum space { SP_AUTO = 0, SP_LIN = 1, SP_NL = 2, SP_LAB = 3 };

enum composite { CM_AUTO = 0, CM_UNION, CM_CLIP_BACKDROP, CM_CLIP_LAYER, CM_INTERSECT };

enum op { OP_NORMAL, OP_DISSOLVE, OP_LEGACY, OP_BLEND, OP_ERASE, OP_MERGE,
          OP_SPLIT, OP_PASS };

/* Properties of a layer mode from GIMP's mode table. */
struct mode_info {
    unsigned char op, blend, comp, cm;
    unsigned char fixed;   /* 1 blend, 2 composite space, 4 composite mode */
    unsigned char kind;    /* 1 subtractive, 2 alpha only, 4 trivial */
    unsigned char where;   /* 1 layers, 2 groups */
};

#define B_ SP_AUTO
#define L_ SP_LIN
#define N_ SP_NL
#define A_ SP_LAB
static const struct mode_info modes[64] = {
    /* 0 normal (legacy) */       {OP_NORMAL, B_, N_, CM_UNION, 7, 4, 3},
    /* 1 dissolve */              {OP_DISSOLVE, B_, B_, CM_UNION, 3, 4, 3},
    /* 2 behind (legacy) */       {OP_NORMAL, B_, N_, CM_UNION, 7, 0, 0},
    /* 3-21: legacy modes */      {OP_LEGACY, N_, N_, CM_CLIP_BACKDROP, 7, 0, 3},
    {OP_LEGACY, N_, N_, CM_CLIP_BACKDROP, 7, 0, 3},
    {OP_LEGACY, N_, N_, CM_CLIP_BACKDROP, 7, 0, 3},
    {OP_LEGACY, N_, N_, CM_CLIP_BACKDROP, 7, 0, 3},
    {OP_LEGACY, N_, N_, CM_CLIP_BACKDROP, 7, 0, 3},
    {OP_LEGACY, N_, N_, CM_CLIP_BACKDROP, 7, 0, 3},
    {OP_LEGACY, N_, N_, CM_CLIP_BACKDROP, 7, 0, 3},
    {OP_LEGACY, N_, N_, CM_CLIP_BACKDROP, 7, 0, 3},
    {OP_LEGACY, N_, N_, CM_CLIP_BACKDROP, 7, 0, 3},
    {OP_LEGACY, N_, N_, CM_CLIP_BACKDROP, 7, 0, 3},
    {OP_LEGACY, N_, N_, CM_CLIP_BACKDROP, 7, 0, 3},
    {OP_LEGACY, N_, N_, CM_CLIP_BACKDROP, 7, 0, 3},
    {OP_LEGACY, N_, N_, CM_CLIP_BACKDROP, 7, 0, 3},
    {OP_LEGACY, N_, N_, CM_CLIP_BACKDROP, 7, 0, 3},
    {OP_LEGACY, N_, N_, CM_CLIP_BACKDROP, 7, 0, 3},
    {OP_LEGACY, N_, N_, CM_CLIP_BACKDROP, 7, 0, 3},
    {OP_LEGACY, N_, N_, CM_CLIP_BACKDROP, 7, 0, 3},
    {OP_LEGACY, N_, N_, CM_CLIP_BACKDROP, 7, 0, 3},
    {OP_LEGACY, N_, N_, CM_CLIP_BACKDROP, 7, 0, 3},
    /* 22 colour erase (legacy) */ {OP_BLEND, N_, N_, CM_CLIP_BACKDROP, 7, 1, 0},
    /* 23 overlay */              {OP_BLEND, N_, L_, CM_CLIP_BACKDROP, 0, 0, 3},
    /* 24-27 LCh */               {OP_BLEND, A_, L_, CM_CLIP_BACKDROP, 1, 0, 3},
    {OP_BLEND, A_, L_, CM_CLIP_BACKDROP, 1, 0, 3},
    {OP_BLEND, A_, L_, CM_CLIP_BACKDROP, 1, 0, 3},
    {OP_BLEND, A_, L_, CM_CLIP_BACKDROP, 1, 0, 3},
    /* 28 normal */               {OP_NORMAL, B_, L_, CM_UNION, 1, 4, 3},
    /* 29 behind */               {OP_NORMAL, B_, L_, CM_UNION, 1, 0, 0},
    /* 30 multiply */             {OP_BLEND, L_, L_, CM_CLIP_BACKDROP, 0, 0, 3},
    /* 31 screen */               {OP_BLEND, N_, L_, CM_CLIP_BACKDROP, 0, 0, 3},
    /* 32 difference */           {OP_BLEND, N_, L_, CM_CLIP_BACKDROP, 0, 0, 3},
    /* 33 addition */             {OP_BLEND, L_, L_, CM_CLIP_BACKDROP, 0, 0, 3},
    /* 34 subtract */             {OP_BLEND, L_, L_, CM_CLIP_BACKDROP, 0, 0, 3},
    /* 35 darken only */          {OP_BLEND, B_, L_, CM_CLIP_BACKDROP, 1, 0, 3},
    /* 36 lighten only */         {OP_BLEND, B_, L_, CM_CLIP_BACKDROP, 1, 0, 3},
    /* 37-40 HSV and HSL */       {OP_BLEND, N_, L_, CM_CLIP_BACKDROP, 1, 0, 3},
    {OP_BLEND, N_, L_, CM_CLIP_BACKDROP, 1, 0, 3},
    {OP_BLEND, N_, L_, CM_CLIP_BACKDROP, 1, 0, 3},
    {OP_BLEND, N_, L_, CM_CLIP_BACKDROP, 1, 0, 3},
    /* 41 divide */               {OP_BLEND, L_, L_, CM_CLIP_BACKDROP, 0, 0, 3},
    /* 42-53 */                   {OP_BLEND, N_, L_, CM_CLIP_BACKDROP, 0, 0, 3},
    {OP_BLEND, N_, L_, CM_CLIP_BACKDROP, 0, 0, 3},
    {OP_BLEND, N_, L_, CM_CLIP_BACKDROP, 0, 0, 3},
    {OP_BLEND, N_, L_, CM_CLIP_BACKDROP, 0, 0, 3},
    {OP_BLEND, N_, L_, CM_CLIP_BACKDROP, 0, 0, 3},
    {OP_BLEND, N_, L_, CM_CLIP_BACKDROP, 0, 0, 3},
    {OP_BLEND, N_, L_, CM_CLIP_BACKDROP, 0, 0, 3},
    {OP_BLEND, N_, L_, CM_CLIP_BACKDROP, 0, 0, 3},
    {OP_BLEND, N_, L_, CM_CLIP_BACKDROP, 0, 0, 3},
    {OP_BLEND, N_, L_, CM_CLIP_BACKDROP, 0, 0, 3},
    {OP_BLEND, N_, L_, CM_CLIP_BACKDROP, 0, 0, 3},
    {OP_BLEND, N_, L_, CM_CLIP_BACKDROP, 0, 0, 3},
    /* 54, 55 luma darken/lighten */ {OP_BLEND, L_, L_, CM_CLIP_BACKDROP, 0, 0, 3},
    {OP_BLEND, L_, L_, CM_CLIP_BACKDROP, 0, 0, 3},
    /* 56 luminance */            {OP_BLEND, L_, L_, CM_CLIP_BACKDROP, 1, 0, 3},
    /* 57 colour erase */         {OP_BLEND, L_, L_, CM_CLIP_BACKDROP, 0, 1, 3},
    /* 58 erase */                {OP_ERASE, B_, L_, CM_CLIP_BACKDROP, 1, 7, 3},
    /* 59 merge */                {OP_MERGE, B_, L_, CM_UNION, 1, 4, 3},
    /* 60 split */                {OP_SPLIT, B_, B_, CM_CLIP_BACKDROP, 3, 7, 3},
    /* 61 pass through */         {OP_PASS, B_, L_, CM_UNION, 5, 4, 2},
    /* 62 replace, 63 overwrite: not for layers */
    {OP_NORMAL, B_, L_, CM_UNION, 1, 4, 0},
    {OP_NORMAL, B_, L_, CM_UNION, 7, 4, 0},
};
#undef B_
#undef L_
#undef N_
#undef A_

#define MODE_NORMAL 28
#define MODE_PASS_THROUGH 61

struct rect { long x0, y0, x1, y1; };

/* What the compositor works out for each layer before rendering. */
struct node {
    int mode, op, blend, comp, cm, work;
    int last;            /* bottom of its stack: drawn as Normal */
    int use_mask;        /* the mask multiplies alpha */
    long mask_x, mask_y; /* where the mask is */
    struct rect rect;    /* the layer, or a group's children */
    long *below;         /* a group's children, bottom first */
    long children;
};

struct band {
    float *px;           /* width * rows * 4 */
    int space;
};

struct ctx {
    const struct xcf_file *xcf;
    struct node *nodes;
    long *order;         /* top-level layers, bottom first */
    long top_count;
    unsigned width, rows, y0, y1;
    struct band acc[MAX_DEPTH + 1];
    float *layer;        /* a layer's pixels for this band */
    float *floating;     /* the floating selection's pixels */
    long float_target;   /* the layer it's anchored to, or -1 */
    int float_on_mask;   /* on that layer's mask instead */
    float *mask;         /* a mask for this band */
    uint16_t *tile;
    uint8_t *scratch;
    float decode8[256];  /* sRGB curve, byte to linear */
    float *decode16;     /* sRGB curve for deeper components, or NULL */
    uint32_t seeds[4096];
    uint32_t mt[624];
    int mti;
    enum codec_result error;
};

/* ---- colour ---- */

static float to_linear(float v)
{
    return v > 0.04045f ? (float)fm_pow((v + 0.055) / 1.055, 2.4) : v / 12.92f;
}

static float from_linear(float v)
{
    return v > 0.003130804954f ? (float)(1.055 * fm_pow(v, 1.0 / 2.4) - 0.055) : v * 12.92f;
}

/* Linear sRGB to XYZ, adapted to D50 (GIMP's babl sRGB space). */
static const float to_xyz[9] = {
    0.4360348f, 0.3851167f, 0.1430514f,
    0.2224879f, 0.7169038f, 0.0606083f,
    0.0139159f, 0.0970605f, 0.7139290f
};
static const float from_xyz[9] = {
    3.1342757f, -1.6172766f, -0.4907241f,
    -0.9787936f, 1.9161605f, 0.0334524f,
    0.0719763f, -0.2289831f, 1.4057168f
};
#define WHITE_X 0.96420288f
#define WHITE_Z 0.82490540f
#define LAB_EPSILON (216.0f / 24389.0f)
#define LAB_KAPPA (24389.0f / 27.0f)

static float lab_f(float t)
{
    return t > LAB_EPSILON ? (float)fm_cbrt(t) : (LAB_KAPPA * t + 16.0f) / 116.0f;
}

static void lin_to_lab(float *p)
{
    float x = (to_xyz[0] * p[0] + to_xyz[1] * p[1] + to_xyz[2] * p[2]) / WHITE_X;
    float y = to_xyz[3] * p[0] + to_xyz[4] * p[1] + to_xyz[5] * p[2];
    float z = (to_xyz[6] * p[0] + to_xyz[7] * p[1] + to_xyz[8] * p[2]) / WHITE_Z;
    float fx = lab_f(x), fy = lab_f(y), fz = lab_f(z);
    p[0] = 116.0f * fy - 16.0f;
    p[1] = 500.0f * (fx - fy);
    p[2] = 200.0f * (fy - fz);
}

static float lab_inverse(float f)
{
    float cube = f * f * f;
    return cube > LAB_EPSILON ? cube : (116.0f * f - 16.0f) / LAB_KAPPA;
}

static void lab_to_lin(float *p)
{
    float fy = (p[0] + 16.0f) / 116.0f;
    float fx = fy + p[1] / 500.0f, fz = fy - p[2] / 200.0f;
    float y = p[0] > LAB_KAPPA * LAB_EPSILON ? fy * fy * fy : p[0] / LAB_KAPPA;
    float x = lab_inverse(fx) * WHITE_X, z = lab_inverse(fz) * WHITE_Z;
    p[0] = from_xyz[0] * x + from_xyz[1] * y + from_xyz[2] * z;
    p[1] = from_xyz[3] * x + from_xyz[4] * y + from_xyz[5] * z;
    p[2] = from_xyz[6] * x + from_xyz[7] * y + from_xyz[8] * z;
}

static float luminance(const float *p)
{
    return to_xyz[3] * p[0] + to_xyz[4] * p[1] + to_xyz[5] * p[2];
}

/* Convert one pixel's colour between spaces; alpha is untouched. */
static void convert_pixel(float *p, int from, int to)
{
    int c;
    if (from == to)
        return;
    if (from == SP_NL)
        for (c = 0; c < 3; c++)
            p[c] = to_linear(p[c]);
    else if (from == SP_LAB)
        lab_to_lin(p);
    if (to == SP_NL)
        for (c = 0; c < 3; c++)
            p[c] = from_linear(p[c]);
    else if (to == SP_LAB)
        lin_to_lab(p);
}

static void convert(float *px, size_t n, int from, int to)
{
    size_t i;
    if (from != to)
        for (i = 0; i < n; i++)
            convert_pixel(px + i * 4, from, to);
}

/* ---- the dissolve pattern: GLib's Mersenne Twister ---- */

static void mt_seed(struct ctx *c, uint32_t seed)
{
    int i;
    c->mt[0] = seed;
    for (i = 1; i < 624; i++)
        c->mt[i] = 1812433253u * (c->mt[i - 1] ^ (c->mt[i - 1] >> 30)) + (uint32_t)i;
    c->mti = 624;
}

static uint32_t mt_next(struct ctx *c)
{
    uint32_t y;
    if (c->mti >= 624) {
        int k;
        for (k = 0; k < 624; k++) {
            y = (c->mt[k] & 0x80000000u) | (c->mt[(k + 1) % 624] & 0x7fffffffu);
            c->mt[k] = c->mt[(k + 397) % 624] ^ (y >> 1) ^ ((y & 1u) ? 0x9908b0dfu : 0u);
        }
        c->mti = 0;
    }
    y = c->mt[c->mti++];
    y ^= y >> 11;
    y ^= (y << 7) & 0x9d2c5680u;
    y ^= (y << 15) & 0xefc60000u;
    y ^= y >> 18;
    return y;
}

/* g_rand_int_range (0, 255) */
static unsigned mt_255(struct ctx *c)
{
    uint32_t v;
    do
        v = mt_next(c);
    while (v == 0xffffffffu);
    return v % 255u;
}

/* ---- blend functions ---- */

static int is_negative(float x)
{
    uint32_t bits;
    memcpy(&bits, &x, sizeof bits);
    return (bits >> 31) != 0;
}

/* Division as IEEE arithmetic does it, reporting what came out instead of
   dividing by zero: 1 for +inf, -1 for -inf, 2 for NaN, 0 for a number. */
static int divide(float a, float b, float *q)
{
    if (b != 0.0f) {
        *q = a / b;
        return 0;
    }
    if (a == 0.0f || a != a)
        return 2;
    return (a > 0.0f) != is_negative(b) ? 1 : -1;
}

static float sdiv(float a, float b)
{
    float q = 0.0f;
    int special;
    if ((float)fm_abs(a) <= EPS)
        return 0.0f;
    special = divide(a, b, &q);
    if (special != 0)
        return special > 0 ? 1e6f : -1e6f;
    return q < -1e6f ? -1e6f : q > 1e6f ? 1e6f : q;
}

static float max3(const float *p)
{
    float m = p[0] > p[1] ? p[0] : p[1];
    return m > p[2] ? m : p[2];
}

static float min3(const float *p)
{
    float m = p[0] < p[1] ? p[0] : p[1];
    return m < p[2] ? m : p[2];
}

static float blend_channel(int mode, float b, float s)
{
    switch (mode) {
    case 23: return b < 0.5f ? 2.0f * b * s : 1.0f - 2.0f * (1.0f - s) * (1.0f - b);
    case 30: return b * s;
    case 31: return 1.0f - (1.0f - b) * (1.0f - s);
    case 32: return (float)fm_abs(b - s);
    case 33: return b + s;
    case 34: return b - s;
    case 35: return b < s ? b : s;
    case 36: return b > s ? b : s;
    case 41: return sdiv(b, s);
    case 42: return sdiv(b, 1.0f - s);
    case 43: return 1.0f - sdiv(1.0f - b, s);
    case 44:
        if (s > 0.5f) {
            float v = 1.0f - (1.0f - b) * (1.0f - 2.0f * (s - 0.5f));
            return v < 1.0f ? v : 1.0f;
        } else {
            float v = 2.0f * b * s;
            return v < 1.0f ? v : 1.0f;
        }
    case 45: return (1.0f - b) * b * s + b * (1.0f - (1.0f - b) * (1.0f - s));
    case 46: return b - s + 0.5f;
    case 47: return b + s - 0.5f;
    case 48:
        if (s <= 0.5f) {
            float v = 1.0f - sdiv(1.0f - b, 2.0f * s);
            return v > 0.0f ? v : 0.0f;
        } else {
            float v = sdiv(b, 2.0f * (1.0f - s));
            return v < 1.0f ? v : 1.0f;
        }
    case 49:
        if (s > 0.5f) {
            float v = 2.0f * (s - 0.5f);
            return b > v ? b : v;
        } else {
            float v = 2.0f * s;
            return b < v ? b : v;
        }
    case 50: return s <= 0.5f ? b + 2.0f * s - 1.0f : b + 2.0f * (s - 0.5f);
    case 51: return b + s < 1.0f ? 0.0f : 1.0f;
    case 52: return 0.5f - 2.0f * (b - 0.5f) * (s - 0.5f);
    case 53: return b + s - 1.0f;
    default: return s;
    }
}

/* C = f(B, S) for the whole-pixel blend modes; returns the result's alpha
   factor (the layer's alpha, or colour erase's). */
static float blend_pixel(int mode, const float *b, const float *s, float *out)
{
    int k;
    switch (mode) {
    case 54: case 55: {
        float yb = luminance(b), ys = luminance(s);
        int keep = mode == 54 ? yb <= ys : yb >= ys;
        memcpy(out, keep ? b : s, 3 * sizeof *out);
        break;
    }
    case 56: {
        float r = sdiv(luminance(s), luminance(b));
        for (k = 0; k < 3; k++)
            out[k] = b[k] * r;
        break;
    }
    case 40: {
        float vb = max3(b), vs = max3(s);
        for (k = 0; k < 3; k++)
            out[k] = (float)fm_abs(vb) > EPS ? b[k] * (vs / vb) : vs;
        break;
    }
    case 37: {
        float ds = max3(s) - min3(s);
        if (ds <= EPS) {
            memcpy(out, b, 3 * sizeof *out);
        } else {
            float mb = max3(b), db = mb - min3(b);
            float sat = mb != 0.0f ? db / mb : 0.0f;
            float r = sat * mb / ds, off = mb - max3(s) * r;
            for (k = 0; k < 3; k++)
                out[k] = s[k] * r + off;
        }
        break;
    }
    case 38: {
        float mb = max3(b), db = mb - min3(b);
        if (db <= EPS) {
            out[0] = out[1] = out[2] = mb;
        } else {
            float ms = max3(s), ss = ms != 0.0f ? (ms - min3(s)) / ms : 0.0f;
            float r = ss * mb / db;
            for (k = 0; k < 3; k++)
                out[k] = b[k] * r + (1.0f - r) * mb;
        }
        break;
    }
    case 39: {
        float lb = (min3(b) + max3(b)) / 2.0f, ls = (min3(s) + max3(s)) / 2.0f;
        if ((float)fm_abs(ls) <= EPS || (float)fm_abs(1.0f - ls) <= EPS) {
            out[0] = out[1] = out[2] = lb;
        } else {
            float fb = lb < 1.0f - lb ? lb : 1.0f - lb;
            float fs = ls < 1.0f - ls ? ls : 1.0f - ls;
            float r = fb / fs, off = 0.0f;
            if (lb > 0.5f)
                off += 1.0f - 2.0f * fb;
            if (ls > 0.5f)
                off += 2.0f * fb - r;
            for (k = 0; k < 3; k++)
                out[k] = s[k] * r + off;
        }
        break;
    }
    case 27:
        out[0] = s[0];
        out[1] = b[1];
        out[2] = b[2];
        break;
    case 26:
        out[0] = b[0];
        out[1] = s[1];
        out[2] = s[2];
        break;
    case 25: {
        float cb = (float)fm_hypot(b[1], b[2]);
        if (cb <= EPS) {
            memcpy(out, b, 3 * sizeof *out);
        } else {
            float cs = (float)fm_hypot(s[1], s[2]);
            out[0] = b[0];
            out[1] = cs * b[1] / cb;
            out[2] = cs * b[2] / cb;
        }
        break;
    }
    case 24: {
        float cs = (float)fm_hypot(s[1], s[2]);
        if (cs <= EPS) {
            memcpy(out, b, 3 * sizeof *out);
        } else {
            float cb = (float)fm_hypot(b[1], b[2]);
            out[0] = b[0];
            out[1] = cb * s[1] / cs;
            out[2] = cb * s[2] / cs;
        }
        break;
    }
    case 22: case 57: {
        float alpha = 0.0f;
        for (k = 0; k < 3; k++) {
            float x = b[k] < 0.0f ? 0.0f : b[k] > 1.0f ? 1.0f : b[k];
            float y = s[k] < 0.0f ? 0.0f : s[k] > 1.0f ? 1.0f : s[k];
            if ((float)fm_abs(x - y) > EPS) {
                float a = x > y ? (x - y) / (1.0f - y) : (y - x) / y;
                if (a > alpha)
                    alpha = a;
            }
        }
        for (k = 0; k < 3; k++)
            out[k] = alpha > EPS ? (b[k] - s[k]) * (1.0f / alpha) + s[k] : 0.0f;
        return alpha;
    }
    default:
        for (k = 0; k < 3; k++)
            out[k] = blend_channel(mode, b[k], s[k]);
        break;
    }
    return s[3];
}

/* ---- GIMP 2.8's modes, in double ---- */

static void rgb_to_hsv(const float *p, double *hsv)
{
    double r = p[0], g = p[1], b = p[2];
    double mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    double mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    double d = mx - mn, h = 0.0, s = 0.0;
    if (d > 0.0001) {
        s = d / mx;
        if (r == mx) {
            h = (g - b) / d;
            if (h < 0.0)
                h += 6.0;
        } else if (g == mx) {
            h = 2.0 + (b - r) / d;
        } else {
            h = 4.0 + (r - g) / d;
        }
        h /= 6.0;
    }
    hsv[0] = h;
    hsv[1] = s;
    hsv[2] = mx;
}

static void hsv_to_rgb(const double *hsv, float *p)
{
    double h = hsv[0], s = hsv[1], v = hsv[2], f, w, q, t;
    int i;
    if (s == 0.0) {
        p[0] = p[1] = p[2] = (float)v;
        return;
    }
    if (h == 1.0)
        h = 0.0;
    h *= 6.0;
    i = (int)h;
    f = h - i;
    w = v * (1.0 - s);
    q = v * (1.0 - s * f);
    t = v * (1.0 - s * (1.0 - f));
    switch (i) {
    case 0: p[0] = (float)v; p[1] = (float)t; p[2] = (float)w; break;
    case 1: p[0] = (float)q; p[1] = (float)v; p[2] = (float)w; break;
    case 2: p[0] = (float)w; p[1] = (float)v; p[2] = (float)t; break;
    case 3: p[0] = (float)w; p[1] = (float)q; p[2] = (float)v; break;
    case 4: p[0] = (float)t; p[1] = (float)w; p[2] = (float)v; break;
    default: p[0] = (float)v; p[1] = (float)w; p[2] = (float)q; break;
    }
}

static void rgb_to_hsl(const float *p, double *hsl)
{
    double r = p[0], g = p[1], b = p[2];
    double mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    double mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    double l = (mx + mn) / 2.0, s = 0.0, h = -1.0, d = mx - mn;
    if (mx != mn) {
        s = l <= 0.5 ? d / (mx + mn) : d / (2.0 - mx - mn);
        if (r == mx)
            h = (g - b) / d;
        else if (g == mx)
            h = 2.0 + (b - r) / d;
        else
            h = 4.0 + (r - g) / d;
        h /= 6.0;
        if (h < 0.0)
            h += 1.0;
    }
    hsl[0] = h;
    hsl[1] = s;
    hsl[2] = l;
}

static double hsl_channel(double m1, double m2, double h)
{
    while (h > 6.0)
        h -= 6.0;
    while (h < 0.0)
        h += 6.0;
    if (h < 1.0)
        return m1 + (m2 - m1) * h;
    if (h < 3.0)
        return m2;
    if (h < 4.0)
        return m1 + (m2 - m1) * (4.0 - h);
    return m1;
}

static void hsl_to_rgb(const double *hsl, float *p)
{
    double h = hsl[0], s = hsl[1], l = hsl[2], m1, m2;
    if (s == 0.0) {
        p[0] = p[1] = p[2] = (float)l;
        return;
    }
    m2 = l <= 0.5 ? l * (1.0 + s) : l + s - l * s;
    m1 = 2.0 * l - m2;
    p[0] = (float)hsl_channel(m1, m2, h * 6.0 + 2.0);
    p[1] = (float)hsl_channel(m1, m2, h * 6.0);
    p[2] = (float)hsl_channel(m1, m2, h * 6.0 - 2.0);
}

static float clamp01(float v)
{
    return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}

/* a / b as the legacy divide and dodge map it: into 0..1, with +inf as 1
   and NaN as 0. */
static float legacy_ratio(float a, float b)
{
    float q = 0.0f;
    int special = divide(a, b, &q);
    if (special != 0)
        return special == 1 ? 1.0f : 0.0f;
    if (q != q || q <= 0.0f)
        return 0.0f;
    return q < 1.0f ? q : 1.0f;
}

static void legacy_blend(int mode, const float *b, const float *s, float *out)
{
    const float half = 128.0f / 255.0f;
    double x[3], y[3];
    int k;

    switch (mode) {
    case 11: case 12: case 14:
        rgb_to_hsv(b, x);
        rgb_to_hsv(s, y);
        if (mode == 11) {
            if (y[1] != 0.0)
                x[0] = y[0];
        } else {
            x[mode == 12 ? 1 : 2] = y[mode == 12 ? 1 : 2];
        }
        hsv_to_rgb(x, out);
        return;
    case 13:
        rgb_to_hsl(b, x);
        rgb_to_hsl(s, y);
        y[2] = x[2];
        hsl_to_rgb(y, out);
        return;
    default:
        break;
    }
    for (k = 0; k < 3; k++) {
        float bb = b[k], ss = s[k], v;
        switch (mode) {
        case 3: v = ss * bb; break;
        case 4: v = 1.0f - (1.0f - bb) * (1.0f - ss); break;
        case 6: v = (float)fm_abs(bb - ss); break;
        case 7: v = clamp01(bb + ss); break;
        case 8: v = clamp01(bb - ss); break;
        case 9: v = bb < ss ? bb : ss; break;
        case 10: v = bb > ss ? bb : ss; break;
        case 15: v = legacy_ratio(bb, ss); break;
        case 16: v = legacy_ratio(bb, 1.0f - ss); break;
        case 17: {
            /* 1 - (1 - B) / S, clamped; NaN from 0 / 0 gives 1. */
            float q = 0.0f;
            int special = divide(1.0f - bb, ss, &q);
            if (special != 0)
                v = special == 1 ? 0.0f : 1.0f;
            else {
                q = 1.0f - q;
                v = q < 0.0f ? 0.0f : q < 1.0f ? q : 1.0f;
            }
            break;
        }
        case 18:
            if (ss > half) {
                double t = 1.0 - (1.0 - (double)bb) * (1.0 - 2.0 * ((double)ss - half));
                v = (float)(t < 1.0 ? t : 1.0);
            } else {
                v = 2.0f * bb * ss;
                v = v < 1.0f ? v : 1.0f;
            }
            break;
        case 20: v = clamp01(bb - ss + half); break;
        case 21: v = clamp01(bb + ss - half); break;
        default: /* 19, and 5 which loads as 19: soft light */
            v = (1.0f - bb) * bb * ss + bb * (1.0f - (1.0f - bb) * (1.0f - ss));
            break;
        }
        out[k] = v;
    }
}

/* ---- compositing one pixel ---- */

static void take(float *b, const float *x, float alpha)
{
    b[0] = x[0];
    b[1] = x[1];
    b[2] = x[2];
    b[3] = alpha;
}

/* The generic modes: C (with alpha factor ca) composited with layer s,
   whose effective alpha is al, onto b. */
static void composite(int cm, int subtractive, float *b, const float *s,
                      const float *c, float ca, float al)
{
    float ab = b[3], alpha, k, n;
    int i;

    if (!subtractive) {
        switch (cm) {
        case CM_UNION:
            alpha = al + (1.0f - al) * ab;
            if (al == 0.0f || alpha == 0.0f) {
                b[3] = alpha;
            } else if (ab == 0.0f) {
                take(b, s, alpha);
            } else {
                float r = al / alpha;
                for (i = 0; i < 3; i++)
                    b[i] = b[i] + r * (ab * (c[i] - s[i]) + s[i] - b[i]);
                b[3] = alpha;
            }
            return;
        case CM_CLIP_BACKDROP:
            if (ab != 0.0f && al != 0.0f)
                for (i = 0; i < 3; i++)
                    b[i] = c[i] * al + b[i] * (1.0f - al);
            return;
        case CM_CLIP_LAYER:
            if (al != 0.0f) {
                if (ab == 0.0f) {
                    memcpy(b, s, 3 * sizeof *b);
                } else {
                    for (i = 0; i < 3; i++)
                        b[i] = c[i] * ab + s[i] * (1.0f - ab);
                }
            }
            b[3] = al;
            return;
        default:
            alpha = ab * al;
            if (alpha != 0.0f)
                memcpy(b, c, 3 * sizeof *b);
            b[3] = alpha;
            return;
        }
    }
    switch (cm) {
    case CM_UNION:
        alpha = ab + al - (2.0f - ca) * ab * al;
        if (al == 0.0f || alpha == 0.0f) {
            b[3] = alpha;
        } else if (ab == 0.0f) {
            take(b, s, alpha);
        } else {
            for (i = 0; i < 3; i++)
                b[i] = (ab / alpha) * (al * (ca * c[i] + (1.0f / ab - 1.0f) * s[i] - b[i]) + b[i]);
            b[3] = alpha;
        }
        return;
    case CM_CLIP_BACKDROP:
        k = ca * al;
        n = 1.0f - al + k;
        if (ab != 0.0f && k != 0.0f)
            for (i = 0; i < 3; i++)
                b[i] = c[i] * (k / n) + b[i] * (1.0f - k / n);
        b[3] = n * ab;
        return;
    case CM_CLIP_LAYER:
        k = ca * ab;
        n = 1.0f - ab + k;
        if (al != 0.0f) {
            if (ab == 0.0f) {
                memcpy(b, s, 3 * sizeof *b);
            } else {
                for (i = 0; i < 3; i++)
                    b[i] = c[i] * (k / n) + s[i] * (1.0f - k / n);
            }
        }
        b[3] = n * al;
        return;
    default:
        alpha = ab * ca * al;
        if (alpha != 0.0f)
            memcpy(b, c, 3 * sizeof *b);
        b[3] = alpha;
        return;
    }
}

static void normal(int cm, float *b, const float *s, float al)
{
    float ab = b[3], alpha;
    int i;
    switch (cm) {
    case CM_UNION:
        alpha = al + ab - al * ab;
        if (alpha != 0.0f) {
            float w = al / alpha;
            for (i = 0; i < 3; i++)
                b[i] = s[i] * w + b[i] * (1.0f - w);
        }
        b[3] = alpha;
        return;
    case CM_CLIP_BACKDROP:
        if (ab != 0.0f)
            for (i = 0; i < 3; i++)
                b[i] = b[i] + (s[i] - b[i]) * al;
        return;
    case CM_CLIP_LAYER:
        if (al != 0.0f)
            memcpy(b, s, 3 * sizeof *b);
        b[3] = al;
        return;
    default:
        alpha = ab * al;
        if (alpha != 0.0f)
            memcpy(b, s, 3 * sizeof *b);
        b[3] = alpha;
        return;
    }
}

static void erase(int cm, float *b, const float *s, float al)
{
    float ab = b[3], alpha;
    switch (cm) {
    case CM_UNION:
        alpha = ab + al - 2.0f * ab * al;
        if (alpha != 0.0f) {
            float r = (1.0f - ab) * al / alpha;
            int i;
            for (i = 0; i < 3; i++)
                b[i] = r * s[i] + (1.0f - r) * b[i];
        }
        b[3] = alpha;
        return;
    case CM_CLIP_BACKDROP:
        b[3] = (1.0f - al) * ab;
        return;
    case CM_CLIP_LAYER:
        alpha = (1.0f - ab) * al;
        if (alpha != 0.0f)
            memcpy(b, s, 3 * sizeof *b);
        b[3] = alpha;
        return;
    default:
        b[3] = 0.0f;
        return;
    }
}

static void merge(int cm, float *b, const float *s, float al)
{
    float ab = b[3], alpha, t;
    int i;
    switch (cm) {
    case CM_UNION:
        t = ab < 1.0f - al ? ab : 1.0f - al;
        alpha = t + al;
        if (alpha != 0.0f)
            for (i = 0; i < 3; i++)
                b[i] = b[i] + (s[i] - b[i]) * al / alpha;
        b[3] = alpha;
        return;
    case CM_CLIP_BACKDROP:
        t = al - (1.0f - ab);
        if (t > 0.0f)
            for (i = 0; i < 3; i++)
                b[i] = b[i] + (s[i] - b[i]) * t / ab;
        return;
    case CM_CLIP_LAYER:
        if (al != 0.0f)
            memcpy(b, s, 3 * sizeof *b);
        b[3] = al;
        return;
    default:
        t = al - (1.0f - ab);
        t = t > 0.0f ? t : 0.0f;
        if (t != 0.0f)
            memcpy(b, s, 3 * sizeof *b);
        b[3] = t;
        return;
    }
}

static void split(int cm, float *b, const float *s, float al)
{
    float ab = b[3], alpha;
    switch (cm) {
    case CM_UNION:
        if (al <= ab)
            b[3] = ab - al;
        else
            take(b, s, al - ab);
        return;
    case CM_CLIP_BACKDROP:
        b[3] = ab - al > 0.0f ? ab - al : 0.0f;
        return;
    case CM_CLIP_LAYER:
        alpha = al - ab > 0.0f ? al - ab : 0.0f;
        if (alpha != 0.0f)
            memcpy(b, s, 3 * sizeof *b);
        b[3] = alpha;
        return;
    default:
        b[3] = 0.0f;
        return;
    }
}

/* Pass-through groups mix their result p back with the backdrop b. */
static void replace(int cm, float *b, const float *p, float v)
{
    float ab = b[3], alpha, r;
    int i;
    switch (cm) {
    case CM_UNION:
        alpha = (p[3] - ab) * v + ab;
        r = alpha != 0.0f ? v * p[3] / alpha : v;
        for (i = 0; i < 3; i++)
            b[i] = (p[i] - b[i]) * r + b[i];
        b[3] = alpha;
        return;
    case CM_CLIP_BACKDROP:
        b[3] = ab * (1.0f - v);
        return;
    case CM_CLIP_LAYER:
        take(b, p, p[3] * v);
        return;
    default:
        b[0] = b[1] = b[2] = b[3] = 0.0f;
        return;
    }
}

/* ---- the node for one layer ---- */

static int resolve_space(int work, int backdrop)
{
    if (work != SP_AUTO)
        return work;
    return backdrop == SP_LIN ? SP_LIN : SP_NL;
}

/* Whether the node leaves nothing outside its layer's rectangle. Legacy
   modes ignore the composite mode and keep the backdrop. */
static int clears_outside(const struct node *n)
{
    if (n->last)
        return 1;
    if (n->op == OP_LEGACY)
        return 0;
    return n->cm == CM_CLIP_LAYER || n->cm == CM_INTERSECT;
}

/* Composite the layer pixels (ctx->layer, in space `space`) onto acc.
   rect limits where the layer has pixels; mask is NULL or per pixel. */
static void apply_node(struct ctx *c, const struct node *n, struct band *acc,
                       float *layer, int space, const float *mask,
                       float opacity)
{
    size_t row_px = c->width, npx = row_px * c->rows;
    long y, x, xa, xb, ya, yb;
    int work = resolve_space(n->work, n->last ? SP_LIN : acc->space);
    int cm = n->cm;
    float tmp[4], cb[4], cs[4];

    convert(acc->px, npx, acc->space, work);
    acc->space = work;
    xa = n->rect.x0 > 0 ? n->rect.x0 : 0;
    xb = n->rect.x1 < (long)c->width ? n->rect.x1 : (long)c->width;
    ya = n->rect.y0 > (long)c->y0 ? n->rect.y0 : (long)c->y0;
    yb = n->rect.y1 < (long)c->y1 ? n->rect.y1 : (long)c->y1;
    /* Outside the layer, clip-to-layer and intersection leave nothing. */
    if (clears_outside(n)) {
        for (y = c->y0; y < (long)c->y1; y++)
            for (x = 0; x < (long)c->width; x++)
                if (y < ya || y >= yb || x < xa || x >= xb)
                    acc->px[((size_t)(y - c->y0) * row_px + (size_t)x) * 4 + 3] = 0.0f;
    }
    if (xa >= xb || ya >= yb)
        return;
    for (y = ya; y < yb; y++) {
        size_t base = (size_t)(y - c->y0) * row_px;
        convert(layer + (base + (size_t)xa) * 4, (size_t)(xb - xa), space, work);
        if (n->op == OP_DISSOLVE) {
            mt_seed(c, c->seeds[(unsigned long)y % 4096u]);
            for (x = 0; x < xa; x++)
                mt_next(c);
        }
        for (x = xa; x < xb; x++) {
            size_t i = base + (size_t)x;
            float *b = acc->px + i * 4;
            const float *s = layer + i * 4;
            float m = mask != NULL ? mask[i] : 1.0f;
            float al = s[3] * opacity * m;

            if (n->op == OP_DISSOLVE) {
                unsigned r = mt_255(c);
                int shown = (float)r < s[3] * opacity * 255.0f * m;
                int dcm = n->last ? CM_UNION : cm;
                if (shown)
                    take(b, s, dcm == CM_UNION || dcm == CM_CLIP_LAYER ? 1.0f : b[3]);
                else if (dcm == CM_CLIP_LAYER || dcm == CM_INTERSECT)
                    b[3] = 0.0f;
                continue;
            }
            if (n->last) {
                take(b, s, al);
                continue;
            }
            switch (n->op) {
            case OP_NORMAL:
                normal(cm, b, s, al);
                break;
            case OP_ERASE:
                erase(cm, b, s, al);
                break;
            case OP_MERGE:
                merge(cm, b, s, al);
                break;
            case OP_SPLIT:
                split(cm, b, s, al);
                break;
            case OP_LEGACY: {
                float cc = (b[3] < s[3] ? b[3] : s[3]) * opacity * m;
                float a2 = b[3] + (1.0f - b[3]) * cc;
                if (cc != 0.0f && a2 != 0.0f) {
                    float r = cc / a2;
                    int k;
                    legacy_blend(n->mode, b, s, tmp);
                    for (k = 0; k < 3; k++)
                        b[k] = tmp[k] * r + b[k] * (1.0f - r);
                }
                break;
            }
            default: {
                float ca = s[3];
                if (b[3] != 0.0f && s[3] != 0.0f) {
                    int bs = n->blend != SP_AUTO ? n->blend : work;
                    memcpy(cb, b, sizeof cb);
                    memcpy(cs, s, sizeof cs);
                    convert_pixel(cb, work, bs);
                    convert_pixel(cs, work, bs);
                    ca = blend_pixel(n->mode, cb, cs, tmp);
                    convert_pixel(tmp, bs, work);
                } else {
                    tmp[0] = tmp[1] = tmp[2] = 0.0f;
                    if (n->mode == 22 || n->mode == 57)
                        ca = 0.0f;
                }
                composite(cm, n->mode == 22 || n->mode == 57, b, s, tmp, ca, al);
                break;
            }
            }
        }
    }
}

/* ---- pixels from the file ---- */

/* Storage values (0..65535) in the image's curve to float. */
static float stored(const struct ctx *c, uint16_t v)
{
    return c->xcf->bpc == 1 ? (float)(v / 257u) / 255.0f : (float)v / 65535.0f;
}

/* A non-linear storage value in linear light. */
static float linear_of(const struct ctx *c, uint16_t v)
{
    return c->xcf->bpc == 1 ? c->decode8[v / 257u] : c->decode16[v];
}

/* Fill ctx->layer for this band with a layer's pixels, in linear light if
   want is SP_LIN and otherwise in the image's own curve; *space says which.
   Outside the layer, pixels are transparent. */
static enum codec_result fetch_layer(struct ctx *c, const struct xcf_layer *l,
                                     int want, float *out, int *space)
{
    const struct xcf_file *xcf = c->xcf;
    const struct xcf_buffer *b = &l->pixels;
    size_t row_px = c->width;
    long ya, yb, xa, xb, row, col;
    unsigned ch = b->channels;
    /* Decode the curve here, by table, when the mode works in linear. */
    int decode = !xcf->linear && want == SP_LIN;

    memset(out, 0, row_px * c->rows * 4 * sizeof *out);
    *space = xcf->linear || decode ? SP_LIN : SP_NL;
    ya = l->y > (long)c->y0 ? l->y : (long)c->y0;
    yb = l->y + (long)l->height < (long)c->y1 ? l->y + (long)l->height : (long)c->y1;
    xa = l->x > 0 ? l->x : 0;
    xb = l->x + (long)l->width < (long)c->width ? l->x + (long)l->width : (long)c->width;
    if (ya >= yb || xa >= xb)
        return CODEC_OK;
    for (row = (ya - l->y) / (long)XCF_TILE; row <= (yb - 1 - l->y) / (long)XCF_TILE; row++) {
        for (col = (xa - l->x) / (long)XCF_TILE; col <= (xb - 1 - l->x) / (long)XCF_TILE; col++) {
            unsigned tw, th, ty, tx;
            enum codec_result r = xcf_tile(xcf, b, (unsigned)col, (unsigned)row,
                                           c->scratch, c->tile, &tw, &th);
            if (r != CODEC_OK)
                return r;
            for (ty = 0; ty < th; ty++) {
                long y = l->y + row * (long)XCF_TILE + ty;
                if (y < ya || y >= yb)
                    continue;
                for (tx = 0; tx < tw; tx++) {
                    long x = l->x + col * (long)XCF_TILE + tx;
                    const uint16_t *t = c->tile + ((size_t)ty * tw + tx) * ch;
                    float *p;
                    int k;
                    if (x < xa || x >= xb)
                        continue;
                    p = out + ((size_t)(y - c->y0) * row_px + (size_t)x) * 4;
                    if (l->base == XCF_INDEXED) {
                        unsigned index = t[0] / 257u;
                        if (index < xcf->colours) {
                            p[0] = xcf->colourmap[index * 3] / 255.0f;
                            p[1] = xcf->colourmap[index * 3 + 1] / 255.0f;
                            p[2] = xcf->colourmap[index * 3 + 2] / 255.0f;
                        } else {
                            /* GIMP shows indexes past the map as black. */
                            p[0] = p[1] = p[2] = 0.0f;
                        }
                    } else if (l->base == XCF_GREY) {
                        p[0] = p[1] = p[2] = stored(c, t[0]);
                    } else {
                        p[0] = stored(c, t[0]);
                        p[1] = stored(c, t[1]);
                        p[2] = stored(c, t[2]);
                    }
                    if (decode) {
                        if (l->base == XCF_INDEXED)
                            for (k = 0; k < 3; k++)
                                p[k] = c->decode8[(unsigned)(p[k] * 255.0f + 0.5f)];
                        else
                            for (k = 0; k < 3; k++)
                                p[k] = linear_of(c, t[l->base == XCF_GREY ? 0 : k]);
                    }
                    p[3] = l->alpha ? stored(c, t[ch - 1]) : 1.0f;
                }
            }
        }
    }
    return CODEC_OK;
}

/* Fill ctx->mask with a mask at (x, y); zero outside it. */
static enum codec_result fetch_mask(struct ctx *c, const struct xcf_buffer *b,
                                   long ox, long oy)
{
    const struct xcf_file *xcf = c->xcf;
    size_t row_px = c->width;
    long ya, yb, xa, xb, row, col;

    memset(c->mask, 0, row_px * c->rows * sizeof *c->mask);
    ya = oy > (long)c->y0 ? oy : (long)c->y0;
    yb = oy + (long)b->height < (long)c->y1 ? oy + (long)b->height : (long)c->y1;
    xa = ox > 0 ? ox : 0;
    xb = ox + (long)b->width < (long)c->width ? ox + (long)b->width : (long)c->width;
    if (ya >= yb || xa >= xb)
        return CODEC_OK;
    for (row = (ya - oy) / (long)XCF_TILE; row <= (yb - 1 - oy) / (long)XCF_TILE; row++) {
        for (col = (xa - ox) / (long)XCF_TILE; col <= (xb - 1 - ox) / (long)XCF_TILE; col++) {
            unsigned tw, th, ty, tx;
            enum codec_result r = xcf_tile(xcf, b, (unsigned)col, (unsigned)row,
                                           c->scratch, c->tile, &tw, &th);
            if (r != CODEC_OK)
                return r;
            for (ty = 0; ty < th; ty++) {
                long y = oy + row * (long)XCF_TILE + ty;
                if (y < ya || y >= yb)
                    continue;
                for (tx = 0; tx < tw; tx++) {
                    long x = ox + col * (long)XCF_TILE + tx;
                    uint16_t v = c->tile[(size_t)ty * tw + tx];
                    if (x < xa || x >= xb)
                        continue;
                    /* Layer masks are linear at every precision. */
                    c->mask[(size_t)(y - c->y0) * row_px + (size_t)x] = stored(c, v);
                }
            }
        }
    }
    return CODEC_OK;
}

/* Round a band to the image's storage precision, as a group's projection
   is, leaving it in the image's own curve. */
static void quantize(struct ctx *c, struct band *band)
{
    const struct xcf_file *xcf = c->xcf;
    size_t i, n = (size_t)c->width * c->rows;
    int curve = xcf->linear ? SP_LIN : SP_NL;
    float top = xcf->bpc == 1 ? 255.0f : 65535.0f;
    int k;

    for (i = 0; i < n; i++) {
        float *p = band->px + i * 4;
        if (xcf->base == XCF_GREY) {
            float y;
            convert_pixel(p, band->space, SP_LIN);
            y = luminance(p);
            p[0] = p[1] = p[2] = xcf->linear ? y : from_linear(y);
        } else {
            convert_pixel(p, band->space, curve);
        }
        for (k = 0; k < 4; k++) {
            float v = p[k];
            v = v > 0.0f ? (v < 1.0f ? v : 1.0f) : 0.0f;
            p[k] = (float)(long)(v * top + 0.5f) / top;
        }
    }
    band->space = curve;
}

static int allowed_mode(int mode, int group)
{
    if (mode < 0 || mode > 63)
        return 0;
    return (modes[mode].where & (group ? 2 : 1)) != 0;
}

/* ---- the layer tree ---- */

static int rect_empty(const struct rect *r)
{
    return r->x0 >= r->x1 || r->y0 >= r->y1;
}

static struct rect rect_union(struct rect a, struct rect b)
{
    if (rect_empty(&a))
        return b;
    if (rect_empty(&b))
        return a;
    if (b.x0 < a.x0) a.x0 = b.x0;
    if (b.y0 < a.y0) a.y0 = b.y0;
    if (b.x1 > a.x1) a.x1 = b.x1;
    if (b.y1 > a.y1) a.y1 = b.y1;
    return a;
}

static struct rect rect_meet(struct rect a, struct rect b)
{
    if (b.x0 > a.x0) a.x0 = b.x0;
    if (b.y0 > a.y0) a.y0 = b.y0;
    if (b.x1 < a.x1) a.x1 = b.x1;
    if (b.y1 < a.y1) a.y1 = b.y1;
    if (rect_empty(&a))
        a.x0 = a.y0 = a.x1 = a.y1 = 0;
    return a;
}

/* List a container's children bottom first. */
static long *list_children(const struct xcf_file *xcf, long first, long *count)
{
    long n = 0, i, *list;
    for (i = first; i >= 0; i = xcf->layers[i].next_sibling)
        n++;
    list = malloc((size_t)(n ? n : 1) * sizeof *list);
    if (list == NULL)
        return NULL;
    *count = n;
    for (i = first; i >= 0; i = xcf->layers[i].next_sibling)
        list[--n] = i;
    return list;
}

/* Work out modes, spaces and rectangles. Groups' rectangles cover all their
   children, visible or not. */
static enum codec_result prepare(struct ctx *c, long index, int depth)
{
    const struct xcf_layer *l = &c->xcf->layers[index];
    struct node *n = &c->nodes[index];
    const struct mode_info *info;
    long i;

    if (depth > MAX_DEPTH)
        return CODEC_INVALID;
    n->mode = allowed_mode(l->mode, l->group) ? l->mode : MODE_NORMAL;
    info = &modes[n->mode];
    n->op = info->op;
    n->blend = info->blend;
    n->comp = info->comp;
    n->cm = info->cm;
    /* Stored settings replace the defaults the mode lets them change. The
       sign only records whether they were "auto" when saved. */
    if (!(info->fixed & 1) && abs(l->blend_space) >= 1 && abs(l->blend_space) <= 4)
        n->blend = abs(l->blend_space) == 4 ? SP_NL : abs(l->blend_space);
    if (!(info->fixed & 2) && abs(l->composite_space) >= 1 && abs(l->composite_space) <= 4)
        n->comp = abs(l->composite_space) == 4 ? SP_NL : abs(l->composite_space);
    if (!(info->fixed & 4) && abs(l->composite_mode) >= 1 && abs(l->composite_mode) <= 4)
        n->cm = abs(l->composite_mode);
    n->work = n->comp;
    if ((info->kind & 2) && n->cm != CM_UNION)
        n->work = SP_AUTO;
    else if ((info->kind & 4) && (n->cm == CM_CLIP_LAYER || n->cm == CM_INTERSECT))
        n->work = SP_AUTO;
    if (l->group) {
        struct rect r = {0, 0, 0, 0};
        n->below = list_children(c->xcf, l->first_child, &n->children);
        if (n->below == NULL)
            return CODEC_NO_MEMORY;
        for (i = 0; i < n->children; i++) {
            enum codec_result result = prepare(c, n->below[i], depth + 1);
            if (result != CODEC_OK)
                return result;
            r = rect_union(r, c->nodes[n->below[i]].rect);
        }
        n->rect = r;
    } else {
        n->rect.x0 = l->x;
        n->rect.y0 = l->y;
        n->rect.x1 = l->x + (long)l->width;
        n->rect.y1 = l->y + (long)l->height;
    }
    /* GIMP only attaches a mask the same size as its layer. */
    n->use_mask = l->has_mask && l->mask.width == (unsigned long)(n->rect.x1 - n->rect.x0) &&
                  l->mask.height == (unsigned long)(n->rect.y1 - n->rect.y0);
    n->mask_x = n->rect.x0;
    n->mask_y = n->rect.y0;
    return CODEC_OK;
}

/* Which layers are the bottom of their stack: those drawn onto an empty
   region, which GIMP draws as Normal whatever their mode. */
static struct rect mark_last(struct ctx *c, const long *list, long count,
                             struct rect below)
{
    long i;
    for (i = 0; i < count; i++) {
        const struct xcf_layer *l = &c->xcf->layers[list[i]];
        struct node *n = &c->nodes[list[i]];
        struct rect r = n->rect, out;
        int source, dest;

        if (!l->visible)
            continue;
        n->last = rect_empty(&below);
        if (l->group && n->op == OP_PASS && !l->show_mask) {
            r = mark_last(c, n->below, n->children, below);
            n->last = 0;
        } else if (l->group) {
            mark_last(c, n->below, n->children, (struct rect){0, 0, 0, 0});
        }
        source = n->last || n->cm == CM_UNION || n->cm == CM_CLIP_LAYER;
        dest = !n->last && (n->cm == CM_UNION || n->cm == CM_CLIP_BACKDROP);
        if (l->opacity == 0.0f)
            source = 0;
        out = rect_meet(r, below);
        if (source)
            out = rect_union(out, r);
        if (dest)
            out = rect_union(out, below);
        below = out;
    }
    return below;
}

static enum codec_result render(struct ctx *c, const long *list, long count, int depth);

/* Keep only pixels inside r. */
static void crop(struct ctx *c, float *px, const struct rect *r)
{
    long y, x;
    for (y = 0; y < (long)c->rows; y++)
        for (x = 0; x < (long)c->width; x++) {
            long cy = y + (long)c->y0;
            if (cy < r->y0 || cy >= r->y1 || x < r->x0 || x >= r->x1)
                px[((size_t)y * c->width + (size_t)x) * 4 + 3] = 0.0f;
        }
}

/* Show the floating selection: composite it with its own mode onto the
   target's pixels in ctx->layer and round the result to the image's
   precision. As in GIMP's projection, the target grows to cover it. */
static enum codec_result anchor(struct ctx *c, int *space)
{
    const struct xcf_layer *f = &c->xcf->layers[c->xcf->floating];
    const struct node *fn = &c->nodes[c->xcf->floating];
    struct band tb;
    int fspace;
    enum codec_result r;

    tb.px = c->layer;
    tb.space = *space;
    r = fetch_layer(c, f, resolve_space(fn->work, tb.space), c->floating, &fspace);
    if (r != CODEC_OK)
        return r;
    apply_node(c, fn, &tb, c->floating, fspace, NULL, f->opacity);
    quantize(c, &tb);
    *space = tb.space;
    return CODEC_OK;
}

/* The same onto a layer mask, as grey, whose result becomes the mask's
   luminance. */
static enum codec_result anchor_mask(struct ctx *c, const struct node *target)
{
    size_t i, n = (size_t)c->width * c->rows;
    float top = c->xcf->bpc == 1 ? 255.0f : 65535.0f;
    int space = SP_LIN;
    enum codec_result r;

    for (i = 0; i < n; i++) {
        float *p = c->layer + i * 4;
        p[0] = p[1] = p[2] = c->mask[i];
        p[3] = 1.0f;
    }
    crop(c, c->layer, &target->rect);
    {
        const struct xcf_layer *f = &c->xcf->layers[c->xcf->floating];
        const struct node *fn = &c->nodes[c->xcf->floating];
        struct band tb;
        int fspace;
        tb.px = c->layer;
        tb.space = space;
        r = fetch_layer(c, f, resolve_space(fn->work, tb.space), c->floating, &fspace);
        if (r != CODEC_OK)
            return r;
        apply_node(c, fn, &tb, c->floating, fspace, NULL, f->opacity);
        /* Masks have no alpha: the colour's luminance is the value. */
        for (i = 0; i < n; i++) {
            float *p = tb.px + i * 4, y;
            long cy = (long)(i / c->width) + (long)c->y0, x = (long)(i % c->width);
            if (cy < target->rect.y0 || cy >= target->rect.y1 ||
                x < target->rect.x0 || x >= target->rect.x1)
                continue;
            convert_pixel(p, tb.space, SP_LIN);
            y = luminance(p);
            y = y > 0.0f ? (y < 1.0f ? y : 1.0f) : 0.0f;
            c->mask[i] = (float)(long)(y * top + 0.5f) / top;
        }
    }
    return CODEC_OK;
}

/* Fetch the layer's mask into ctx->mask if it applies; *mask gets it or
   NULL. Groups call this after their children, which share the buffer. */
static enum codec_result layer_mask(struct ctx *c, long index, const float **mask)
{
    const struct xcf_layer *l = &c->xcf->layers[index];
    const struct node *n = &c->nodes[index];
    enum codec_result r;

    *mask = NULL;
    if (!n->use_mask || !(l->apply_mask || l->show_mask))
        return CODEC_OK;
    r = fetch_mask(c, &l->mask, n->mask_x, n->mask_y);
    if (r == CODEC_OK && index == c->float_target && c->float_on_mask)
        r = anchor_mask(c, n);
    if (r == CODEC_OK)
        *mask = c->mask;
    return r;
}

/* show_mask: the mask itself is drawn, as opaque grey, in Normal mode. */
static void render_shown_mask(struct ctx *c, const struct xcf_layer *l,
                              const struct node *n, struct band *acc)
{
    size_t npx = (size_t)c->width * c->rows;
    struct node shown = *n;
    long y, x;

    memset(c->layer, 0, npx * 4 * sizeof *c->layer);
    for (y = 0; y < (long)c->rows; y++)
        for (x = 0; x < (long)c->width; x++) {
            long cy = y + (long)c->y0;
            size_t k = (size_t)y * c->width + (size_t)x;
            if (cy >= n->rect.y0 && cy < n->rect.y1 && x >= n->rect.x0 && x < n->rect.x1) {
                float *p = c->layer + k * 4;
                p[0] = p[1] = p[2] = c->mask[k];
                p[3] = 1.0f;
            }
        }
    shown.op = OP_NORMAL;
    shown.cm = CM_UNION;
    shown.work = n->comp;
    apply_node(c, &shown, acc, c->layer, SP_LIN, NULL, l->opacity);
}

static enum codec_result render_layer(struct ctx *c, long index, int depth)
{
    const struct xcf_layer *l = &c->xcf->layers[index];
    const struct node *n = &c->nodes[index];
    struct band *acc = &c->acc[depth];
    size_t npx = (size_t)c->width * c->rows, i;
    enum codec_result r;
    const float *mask = NULL;
    int space;

    if (l->show_mask && n->use_mask) {
        r = layer_mask(c, index, &mask);
        if (r == CODEC_OK)
            render_shown_mask(c, l, n, acc);
        return r;
    }
    if (l->group && n->op == OP_PASS) {
        struct band *p = &c->acc[depth + 1];
        int work;
        memcpy(p->px, acc->px, npx * 4 * sizeof *p->px);
        p->space = acc->space;
        r = render(c, n->below, n->children, depth + 1);
        if (r == CODEC_OK)
            r = layer_mask(c, index, &mask);
        if (r != CODEC_OK)
            return r;
        if (l->opacity == 1.0f && mask == NULL && n->cm == CM_UNION) {
            memcpy(acc->px, p->px, npx * 4 * sizeof *p->px);
            acc->space = p->space;
            return CODEC_OK;
        }
        work = resolve_space(n->work, acc->space);
        convert(acc->px, npx, acc->space, work);
        convert(p->px, npx, p->space, work);
        acc->space = work;
        for (i = 0; i < npx; i++)
            replace(n->cm, acc->px + i * 4, p->px + i * 4,
                    l->opacity * (mask != NULL ? mask[i] : 1.0f));
        return CODEC_OK;
    }
    if (l->group) {
        struct band *g = &c->acc[depth + 1];
        memset(g->px, 0, npx * 4 * sizeof *g->px);
        g->space = SP_LIN;
        r = render(c, n->below, n->children, depth + 1);
        if (r == CODEC_OK)
            r = layer_mask(c, index, &mask);
        if (r != CODEC_OK)
            return r;
        quantize(c, g);
        apply_node(c, n, acc, g->px, g->space, mask, l->opacity);
        return CODEC_OK;
    }
    r = layer_mask(c, index, &mask);
    if (r == CODEC_OK)
        r = fetch_layer(c, l, resolve_space(n->work, n->last ? SP_LIN : acc->space),
                        c->layer, &space);
    if (r == CODEC_OK && index == c->float_target && !c->float_on_mask)
        r = anchor(c, &space);
    if (r != CODEC_OK)
        return r;
    apply_node(c, n, acc, c->layer, space, mask, l->opacity);
    return CODEC_OK;
}

static enum codec_result render(struct ctx *c, const long *list, long count, int depth)
{
    long i;
    if (depth > MAX_DEPTH)
        return CODEC_INVALID;
    for (i = 0; i < count; i++) {
        enum codec_result r;
        if (!c->xcf->layers[list[i]].visible)
            continue;
        r = render_layer(c, list[i], depth);
        if (r != CODEC_OK)
            return r;
    }
    return CODEC_OK;
}

/* The deepest group nesting among visible layers. */
static int depth_of(const struct ctx *c, const long *list, long count)
{
    int deepest = 0;
    long i;
    for (i = 0; i < count; i++) {
        const struct node *n = &c->nodes[list[i]];
        if (c->xcf->layers[list[i]].visible && c->xcf->layers[list[i]].group) {
            int d = 1 + depth_of(c, n->below, n->children);
            if (d > deepest)
                deepest = d;
        }
    }
    return deepest;
}

/* The band, now in storage precision, to 8-bit sRGB. */
static void output(struct ctx *c, uint8_t *rgba)
{
    const struct xcf_file *xcf = c->xcf;
    size_t i, n = (size_t)c->width * c->rows;
    struct band *acc = &c->acc[0];
    uint8_t *out = rgba + (size_t)c->y0 * c->width * 4;
    int k;

    quantize(c, acc);
    for (i = 0; i < n; i++) {
        const float *p = acc->px + i * 4;
        for (k = 0; k < 3; k++) {
            float v = xcf->linear ? from_linear(p[k]) : p[k];
            v = v > 0.0f ? (v < 1.0f ? v : 1.0f) : 0.0f;
            out[i * 4 + k] = (uint8_t)(v * 255.0f + 0.5f);
        }
        out[i * 4 + 3] = (uint8_t)(p[3] * 255.0f + 0.5f);
    }
}

static void release(struct ctx *c)
{
    long i;
    int d;
    if (c->nodes != NULL)
        for (i = 0; i < c->xcf->count; i++)
            free(c->nodes[i].below);
    free(c->nodes);
    free(c->order);
    for (d = 0; d <= MAX_DEPTH; d++)
        free(c->acc[d].px);
    free(c->layer);
    free(c->floating);
    free(c->mask);
    free(c->tile);
    free(c->scratch);
    free(c->decode16);
}

enum codec_result xcf_composite(const struct xcf_file *xcf, uint8_t *rgba)
{
    struct ctx c;
    enum codec_result r = CODEC_OK;
    size_t npx;
    unsigned band, i;
    int depth, d;
    long k;

    memset(&c, 0, sizeof c);
    c.xcf = xcf;
    c.width = xcf->width;
    c.nodes = calloc((size_t)xcf->count, sizeof *c.nodes);
    c.order = list_children(xcf, xcf->top, &c.top_count);
    if (c.nodes == NULL || c.order == NULL) {
        release(&c);
        return CODEC_NO_MEMORY;
    }
    for (k = 0; k < c.top_count && r == CODEC_OK; k++)
        r = prepare(&c, c.order[k], 0);
    if (r != CODEC_OK) {
        release(&c);
        return r;
    }
    /* A visible floating selection is anchored to a layer or layer mask;
       on a channel it doesn't change the picture. */
    c.float_target = -1;
    if (xcf->floating >= 0 && xcf->layers[xcf->floating].visible) {
        const struct xcf_layer *f = &xcf->layers[xcf->floating];
        for (k = 0; k < xcf->count; k++) {
            const struct xcf_layer *t = &xcf->layers[k];
            if (k == xcf->floating || t->group)
                continue;
            if (t->start == f->float_target) {
                c.float_target = k;
                c.float_on_mask = 0;
            } else if (t->has_mask && t->mask_start == f->float_target) {
                c.float_target = k;
                c.float_on_mask = 1;
            }
        }
        if (c.float_target >= 0 && r == CODEC_OK)
            r = prepare(&c, xcf->floating, 0);
        if (c.float_target >= 0 && !c.float_on_mask)
            c.nodes[c.float_target].rect = rect_union(c.nodes[c.float_target].rect,
                                                      c.nodes[xcf->floating].rect);
    }
    mark_last(&c, c.order, c.top_count, (struct rect){0, 0, 0, 0});

    depth = depth_of(&c, c.order, c.top_count);
    /* Bands of 64 rows, fewer when wide images and deep groups would need
       more than about 64MB of buffers. */
    for (band = 64; band > 1 && (uint64_t)c.width * band * (unsigned)(depth + 4) > 4194304u; band /= 2)
        ;
    npx = (size_t)c.width * band;
    for (d = 0; d <= depth; d++) {
        c.acc[d].px = malloc(npx * 4 * sizeof(float));
        if (c.acc[d].px == NULL)
            r = CODEC_NO_MEMORY;
    }
    c.layer = malloc(npx * 4 * sizeof(float));
    if (c.float_target >= 0) {
        c.floating = malloc(npx * 4 * sizeof(float));
        if (c.floating == NULL)
            r = CODEC_NO_MEMORY;
    }
    c.mask = malloc(npx * sizeof(float));
    c.tile = malloc(XCF_TILE * XCF_TILE * 4 * sizeof *c.tile);
    c.scratch = malloc(XCF_TILE_SCRATCH);
    if (!xcf->linear && xcf->bpc > 1) {
        c.decode16 = malloc(65536 * sizeof *c.decode16);
        if (c.decode16 != NULL)
            for (i = 0; i < 65536; i++)
                c.decode16[i] = to_linear(i / 65535.0f);
    }
    if (c.layer == NULL || c.mask == NULL || c.tile == NULL || c.scratch == NULL ||
        (!xcf->linear && xcf->bpc > 1 && c.decode16 == NULL))
        r = CODEC_NO_MEMORY;
    if (r != CODEC_OK) {
        release(&c);
        return r;
    }
    for (i = 0; i < 256; i++) {
        double lin = i / 255.0 > 0.04045 ? fm_pow((i / 255.0 + 0.055) / 1.055, 2.4) : i / 255.0 / 12.92;
        c.decode8[i] = (float)lin;
    }
    mt_seed(&c, 314159265u);
    for (i = 0; i < 4096; i++)
        c.seeds[i] = mt_next(&c);
    for (c.y0 = 0; c.y0 < xcf->height && r == CODEC_OK; c.y0 += band) {
        c.y1 = c.y0 + band < xcf->height ? c.y0 + band : xcf->height;
        c.rows = c.y1 - c.y0;
        memset(c.acc[0].px, 0, (size_t)c.width * c.rows * 4 * sizeof(float));
        c.acc[0].space = SP_LIN;
        r = render(&c, c.order, c.top_count, 0);
        if (r == CODEC_OK)
            output(&c, rgba);
    }
    release(&c);
    return r;
}
