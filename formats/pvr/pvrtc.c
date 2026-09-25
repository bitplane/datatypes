#include "pvrtc.h"

/* Modulation weights, out of 8. PUNCH marks a punch-through pixel, whose
   weight is 4 and whose alpha is 0. */
#define PUNCH 14u
static const unsigned weights[4] = { 0, 3, 5, 8 };
static const unsigned punch_weights[4] = { 0, 4, PUNCH, 8 };

struct texture {
    const uint8_t *data;
    unsigned blocks_x, blocks_y;    /* powers of two */
    unsigned block_width;           /* 4 or 8 pixels; blocks are 4 high */
    unsigned width, height;         /* in pixels, a whole number of blocks */
};

static unsigned blocks(unsigned pixels, unsigned block)
{
    unsigned n = pixels / block;
    return n < 2 ? 2 : n;
}

size_t pvrtc_size(unsigned width, unsigned height, int two_bpp)
{
    return (size_t)blocks(width, two_bpp ? 8 : 4) * blocks(height, 4) * 8u;
}

static uint32_t get32(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Morton order: y takes the lower bit of each pair, and the longer side's
   remaining bits go on top. */
static const uint8_t *block(const struct texture *t, unsigned bx, unsigned by)
{
    unsigned least = t->blocks_x < t->blocks_y ? t->blocks_x : t->blocks_y;
    unsigned bit, shift = 0;
    size_t index = 0;

    for (bit = 1; bit < least; bit <<= 1, shift++) {
        if (by & bit)
            index |= (size_t)1 << (2u * shift);
        if (bx & bit)
            index |= (size_t)1 << (2u * shift + 1u);
    }
    index |= (size_t)((t->blocks_x < t->blocks_y ? by : bx) >> shift) << (2u * shift);
    return t->data + index * 8u;
}

/* Colours A and B of a block as 5-bit red, green and blue and 4-bit alpha. */
static void colours(const uint8_t *b, unsigned c[2][4])
{
    uint32_t w = get32(b + 4);
    unsigned a = w & 0xffffu, v = w >> 16;

    if (v & 0x8000u) {
        c[1][0] = (v >> 10) & 31u;
        c[1][1] = (v >> 5) & 31u;
        c[1][2] = v & 31u;
        c[1][3] = 15;
    } else {
        c[1][0] = ((v >> 8) & 15u) << 1 | ((v >> 11) & 1u);
        c[1][1] = ((v >> 4) & 15u) << 1 | ((v >> 7) & 1u);
        c[1][2] = (v & 15u) << 1 | ((v >> 3) & 1u);
        c[1][3] = ((v >> 12) & 7u) << 1;
    }
    if (a & 0x8000u) {
        c[0][0] = (a >> 10) & 31u;
        c[0][1] = (a >> 5) & 31u;
        c[0][2] = ((a >> 1) & 15u) << 1 | ((a >> 4) & 1u);
        c[0][3] = 15;
    } else {
        c[0][0] = ((a >> 8) & 15u) << 1 | ((a >> 11) & 1u);
        c[0][1] = ((a >> 4) & 15u) << 1 | ((a >> 7) & 1u);
        c[0][2] = ((a >> 1) & 7u) << 2 | ((a >> 2) & 3u);
        c[0][3] = ((a >> 12) & 7u) << 1;
    }
}

/* Weight of a pixel whose modulation value is stored, in pixels. */
static unsigned stored_weight(const struct texture *t, unsigned x, unsigned y)
{
    const uint8_t *b = block(t, x / t->block_width, y / 4u);
    uint32_t w = get32(b);
    unsigned lx = x % t->block_width, ly = y % 4u, k, v;

    if (t->block_width == 4) {
        v = (w >> ((ly * 4u + lx) * 2u)) & 3u;
        return (b[4] & 1u) ? punch_weights[v] : weights[v];
    }
    if (!(b[4] & 1u))
        return (w >> (ly * 8u + lx)) & 1u ? 8u : 0u;
    /* Interpolated 2bpp blocks store every other pixel. Bit 0, and bit 20
       in some modes, choose the mode instead of holding a value. */
    k = ly * 4u + lx / 2u;
    v = (w >> (2u * k)) & 3u;
    if (k == 0)
        v = (w & 2u) ? 3u : 0u;
    else if (k == 10 && (w & 1u))
        v = (w & (1u << 21)) ? 3u : 0u;
    return weights[v];
}

static unsigned weight(const struct texture *t, unsigned x, unsigned y)
{
    const uint8_t *b;
    uint32_t w;
    unsigned left, right, up, down;

    if (t->block_width == 4 || ((x ^ y) & 1u) == 0)
        return stored_weight(t, x, y);
    b = block(t, x / 8u, y / 4u);
    if (!(b[4] & 1u))
        return stored_weight(t, x, y);
    w = get32(b);
    left = stored_weight(t, (x + t->width - 1u) % t->width, y);
    right = stored_weight(t, (x + 1u) % t->width, y);
    up = stored_weight(t, x, (y + t->height - 1u) % t->height);
    down = stored_weight(t, x, (y + 1u) % t->height);
    if (!(w & 1u))
        return (left + right + up + down + 2u) / 4u;
    if (w & (1u << 20))
        return (up + down + 1u) / 2u;
    return (left + right + 1u) / 2u;
}

void pvrtc_decode(const uint8_t *data, unsigned width, unsigned height, int two_bpp,
                  uint8_t *rgba)
{
    struct texture t;
    unsigned x, y, k, i, bw = two_bpp ? 8u : 4u;

    t.data = data;
    t.block_width = bw;
    t.blocks_x = blocks(width, bw);
    t.blocks_y = blocks(height, 4);
    t.width = t.blocks_x * bw;
    t.height = t.blocks_y * 4u;
    for (y = 0; y < height; y++) {
        /* Each block's colours sit at its centre; pixels blend the four
           nearest, wrapping at the edges. */
        unsigned ay = y + t.height - 2u, fy = ay % 4u;
        unsigned by0 = (ay / 4u) % t.blocks_y, by1 = (by0 + 1u) % t.blocks_y;
        for (x = 0; x < width; x++, rgba += 4) {
            unsigned ax = x + t.width - bw / 2u, fx = ax % bw;
            unsigned bx0 = (ax / bw) % t.blocks_x, bx1 = (bx0 + 1u) % t.blocks_x;
            unsigned p[2][4], q[2][4], r[2][4], s[2][4], m = weight(&t, x, y);
            colours(block(&t, bx0, by0), p);
            colours(block(&t, bx1, by0), q);
            colours(block(&t, bx0, by1), r);
            colours(block(&t, bx1, by1), s);
            for (k = 0; k < 4; k++) {
                unsigned c[2];
                for (i = 0; i < 2; i++) {
                    unsigned v = p[i][k] * (bw - fx) * (4u - fy) + q[i][k] * fx * (4u - fy) +
                                 r[i][k] * (bw - fx) * fy + s[i][k] * fx * fy;
                    /* v is 16 or 32 times the 5 or 4-bit value: widen it to
                       8 bits by repeating its top bits. */
                    if (k == 3)
                        c[i] = two_bpp ? (v >> 5) + (v >> 1) : (v >> 4) + v;
                    else
                        c[i] = two_bpp ? (v >> 7) + (v >> 2) : (v >> 6) + (v >> 1);
                }
                if (m == PUNCH)
                    rgba[k] = (uint8_t)(k == 3 ? 0u : (c[0] * 4u + c[1] * 4u) / 8u);
                else
                    rgba[k] = (uint8_t)((c[0] * (8u - m) + c[1] * m) / 8u);
            }
        }
    }
}
