#include "etc.h"

/* Intensity modifiers of the individual and differential modes, small then
   large; pixel indices 0 to 3 select +small, +large, -small and -large. */
static const int modifiers[8][2] = {
    { 2, 8 }, { 5, 17 }, { 9, 29 }, { 13, 42 },
    { 18, 60 }, { 24, 80 }, { 33, 106 }, { 47, 183 }
};
/* Distances of the T and H modes. */
static const int distances[8] = { 3, 6, 11, 16, 23, 32, 41, 64 };
static const int eac_tables[16][8] = {
    { -3, -6, -9, -15, 2, 5, 8, 14 }, { -3, -7, -10, -13, 2, 6, 9, 12 },
    { -2, -5, -8, -13, 1, 4, 7, 12 }, { -2, -4, -6, -13, 1, 3, 5, 12 },
    { -3, -6, -8, -12, 2, 5, 7, 11 }, { -3, -7, -9, -11, 2, 6, 8, 10 },
    { -4, -7, -8, -11, 3, 6, 7, 10 }, { -3, -5, -8, -11, 2, 4, 7, 10 },
    { -2, -6, -8, -10, 1, 5, 7, 9 }, { -2, -5, -8, -10, 1, 4, 7, 9 },
    { -2, -4, -8, -10, 1, 3, 7, 9 }, { -2, -5, -7, -10, 1, 4, 6, 9 },
    { -3, -4, -7, -10, 2, 3, 6, 9 }, { -1, -2, -3, -10, 0, 1, 2, 9 },
    { -4, -6, -8, -9, 3, 5, 7, 8 }, { -3, -5, -7, -9, 2, 4, 6, 8 }
};

static uint64_t get64(const uint8_t *p)
{
    uint64_t v = 0;
    unsigned i;
    for (i = 0; i < 8; i++)
        v = v << 8 | p[i];
    return v;
}

static unsigned bits(uint64_t v, unsigned low, unsigned count)
{
    return (unsigned)(v >> low) & ((1u << count) - 1u);
}

static uint8_t clamp(int v)
{
    return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}

static int extend4(unsigned v) { return (int)(v << 4 | v); }
static int extend5(unsigned v) { return (int)(v << 3 | v >> 2); }
static int extend6(unsigned v) { return (int)(v << 2 | v >> 4); }
static int extend7(unsigned v) { return (int)(v << 1 | v >> 6); }

/* The 2-bit index of pixel x, y: pixels run down columns. */
static unsigned pixel_index(uint64_t w, unsigned x, unsigned y)
{
    unsigned i = x * 4u + y;
    return bits(w, 16u + i, 1) << 1 | bits(w, i, 1);
}

static void put(uint8_t *out, unsigned x, unsigned y, int r, int g, int b, int a)
{
    uint8_t *p = out + (y * 4u + x) * 4u;
    p[0] = clamp(r);
    p[1] = clamp(g);
    p[2] = clamp(b);
    p[3] = (uint8_t)a;
}

/* Individual and differential modes: two sub-blocks, each a base colour
   shifted by an intensity modifier. */
static void subblocks(uint64_t w, const int base[2][3], int transparent, uint8_t *out)
{
    unsigned flip = bits(w, 32, 1), x, y;
    unsigned table[2];

    table[0] = bits(w, 37, 3);
    table[1] = bits(w, 34, 3);
    for (y = 0; y < 4; y++) {
        for (x = 0; x < 4; x++) {
            unsigned s = flip ? y >= 2 : x >= 2, index = pixel_index(w, x, y);
            int m = modifiers[table[s]][index & 1u];
            const int *c = base[s];
            if (transparent && index == 2) {
                put(out, x, y, 0, 0, 0, 0);
                continue;
            }
            if (transparent && index == 0)
                m = 0;
            if (index & 2u)
                m = -m;
            put(out, x, y, c[0] + m, c[1] + m, c[2] + m, 255);
        }
    }
}

/* T and H modes: four paint colours. */
static void paint(uint64_t w, int colours[4][3], int transparent, uint8_t *out)
{
    unsigned x, y;
    for (y = 0; y < 4; y++) {
        for (x = 0; x < 4; x++) {
            unsigned index = pixel_index(w, x, y);
            const int *c = colours[index];
            if (transparent && index == 2)
                put(out, x, y, 0, 0, 0, 0);
            else
                put(out, x, y, c[0], c[1], c[2], 255);
        }
    }
}

static void t_mode(uint64_t w, int transparent, uint8_t *out)
{
    int c[4][3], d = distances[bits(w, 34, 2) << 1 | bits(w, 32, 1)];
    unsigned k;

    c[0][0] = extend4(bits(w, 59, 2) << 2 | bits(w, 56, 2));
    c[0][1] = extend4(bits(w, 52, 4));
    c[0][2] = extend4(bits(w, 48, 4));
    c[2][0] = extend4(bits(w, 44, 4));
    c[2][1] = extend4(bits(w, 40, 4));
    c[2][2] = extend4(bits(w, 36, 4));
    for (k = 0; k < 3; k++) {
        c[1][k] = clamp(c[2][k] + d);
        c[3][k] = clamp(c[2][k] - d);
    }
    paint(w, c, transparent, out);
}

static void h_mode(uint64_t w, int transparent, uint8_t *out)
{
    unsigned r1 = bits(w, 59, 4), g1 = bits(w, 56, 3) << 1 | bits(w, 52, 1);
    unsigned b1 = bits(w, 51, 1) << 3 | bits(w, 47, 3);
    unsigned r2 = bits(w, 43, 4), g2 = bits(w, 39, 4), b2 = bits(w, 35, 4);
    unsigned index = bits(w, 34, 1) << 2 | bits(w, 32, 1) << 1;
    int c[4][3], base[2][3], d;
    unsigned k;

    if ((r1 << 8 | g1 << 4 | b1) >= (r2 << 8 | g2 << 4 | b2))
        index |= 1u;
    d = distances[index];
    base[0][0] = extend4(r1);
    base[0][1] = extend4(g1);
    base[0][2] = extend4(b1);
    base[1][0] = extend4(r2);
    base[1][1] = extend4(g2);
    base[1][2] = extend4(b2);
    for (k = 0; k < 3; k++) {
        c[0][k] = clamp(base[0][k] + d);
        c[1][k] = clamp(base[0][k] - d);
        c[2][k] = clamp(base[1][k] + d);
        c[3][k] = clamp(base[1][k] - d);
    }
    paint(w, c, transparent, out);
}

/* Planar mode: a colour gradient from an origin, horizontal and vertical
   colour. */
static void planar(uint64_t w, uint8_t *out)
{
    int o[3], h[3], v[3];
    unsigned x, y, k;

    o[0] = extend6(bits(w, 57, 6));
    o[1] = extend7(bits(w, 56, 1) << 6 | bits(w, 49, 6));
    o[2] = extend6(bits(w, 48, 1) << 5 | bits(w, 43, 2) << 3 | bits(w, 39, 3));
    h[0] = extend6(bits(w, 34, 5) << 1 | bits(w, 32, 1));
    h[1] = extend7(bits(w, 25, 7));
    h[2] = extend6(bits(w, 19, 6));
    v[0] = extend6(bits(w, 13, 6));
    v[1] = extend7(bits(w, 6, 7));
    v[2] = extend6(bits(w, 0, 6));
    for (y = 0; y < 4; y++) {
        for (x = 0; x < 4; x++) {
            int c[3];
            for (k = 0; k < 3; k++) {
                c[k] = (int)x * (h[k] - o[k]) + (int)y * (v[k] - o[k]) + 4 * o[k] + 2;
                c[k] = c[k] < 0 ? 0 : c[k] >> 2;
            }
            put(out, x, y, c[0], c[1], c[2], 255);
        }
    }
}

void etc2_rgb_block(const uint8_t *in, uint8_t *out, int punch_through)
{
    uint64_t w = get64(in);
    /* ETC2 RGB A1 reuses the differential bit as the opaque bit. */
    int differential = punch_through || bits(w, 33, 1);
    int transparent = punch_through && !bits(w, 33, 1);
    int base[2][3];
    unsigned k;

    if (!differential) {
        for (k = 0; k < 3; k++) {
            base[0][k] = extend4(bits(w, 60u - 8u * k, 4));
            base[1][k] = extend4(bits(w, 56u - 8u * k, 4));
        }
        subblocks(w, base, 0, out);
        return;
    }
    for (k = 0; k < 3; k++) {
        int c = (int)bits(w, 59u - 8u * k, 5);
        int d = (int)bits(w, 56u - 8u * k, 3);
        d = d >= 4 ? d - 8 : d;
        if (c + d < 0 || c + d > 31) {
            /* A second colour out of range selects another mode. */
            if (k == 0)
                t_mode(w, transparent, out);
            else if (k == 1)
                h_mode(w, transparent, out);
            else
                planar(w, out);
            return;
        }
        base[0][k] = extend5((unsigned)c);
        base[1][k] = extend5((unsigned)(c + d));
    }
    subblocks(w, base, transparent, out);
}

/* The table modifier that pixel x, y's 3-bit index selects. */
static int eac_value(uint64_t w, unsigned x, unsigned y)
{
    unsigned i = x * 4u + y;
    return eac_tables[bits(w, 48, 4)][bits(w, 45u - 3u * i, 3)];
}

void eac8_block(const uint8_t *in, uint8_t *out, unsigned channel)
{
    uint64_t w = get64(in);
    int base = (int)bits(w, 56, 8), multiplier = (int)bits(w, 52, 4);
    unsigned x, y;

    for (y = 0; y < 4; y++)
        for (x = 0; x < 4; x++)
            out[(y * 4u + x) * 4u + channel] =
                clamp(base + eac_value(w, x, y) * multiplier);
}

void eac11_block(const uint8_t *in, uint8_t *out, unsigned channel)
{
    uint64_t w = get64(in);
    int base = (int)bits(w, 56, 8) * 8 + 4, multiplier = (int)bits(w, 52, 4);
    unsigned x, y;

    for (y = 0; y < 4; y++) {
        for (x = 0; x < 4; x++) {
            int m = eac_value(w, x, y);
            int v = base + (multiplier ? m * multiplier * 8 : m);
            v = v < 0 ? 0 : v > 2047 ? 2047 : v;
            out[(y * 4u + x) * 4u + channel] = (uint8_t)(v >> 3);
        }
    }
}
