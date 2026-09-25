/* Pi: 4 or 8-bit pictures from Yanagisawa's Pi loader. */
#include <stdlib.h>
#include <string.h>

#include "japanpc.h"

struct pi {
    struct jp_bits bits;
    unsigned depth;
    uint8_t *indexes;
    uint8_t (*recent)[256]; /* per previous colour, most recent first */
};

/* 1 followed by up to max - bits further ones, then that many bits. */
static long pi_number(struct jp_bits *b, unsigned bits, unsigned max)
{
    long low;
    for (; bits < max; bits++) {
        int bit = jp_bit(b);
        if (bit < 0)
            return -1;
        if (bit == 0)
            break;
    }
    low = jp_read(b, bits);
    return low < 0 ? -1 : 1L << bits | low;
}

/* A colour given by its rank among those that followed the previous one. */
static int pi_literal(struct pi *pi, size_t at)
{
    unsigned prev = at == 0 ? 0 : pi->indexes[at - 1];
    uint8_t *list = pi->recent[prev];
    long rank;
    uint8_t colour;
    int bit = jp_bit(&pi->bits);

    if (bit < 0)
        return 0;
    rank = bit ? jp_bit(&pi->bits) : pi_number(&pi->bits, 1, pi->depth - 1);
    if (rank < 0)
        return 0;
    colour = list[rank];
    memmove(list + 1, list, (size_t)rank);
    list[0] = colour;
    pi->indexes[at] = colour;
    return 1;
}

static int pi_pair(struct pi *pi, size_t at, size_t count)
{
    return pi_literal(pi, at) && (at + 1 >= count || pi_literal(pi, at + 1));
}

/* 00, 01 or 10 for positions 0 to 2, then 110 or 111 for 3 and 4. */
static int pi_position(struct jp_bits *b)
{
    long position = jp_read(b, 2);
    int bit;
    if (position != 3)
        return (int)position;
    bit = jp_bit(b);
    return bit < 0 ? -1 : 3 + bit;
}

static enum codec_result pi_unpack(struct pi *pi, unsigned width,
                                   size_t count)
{
    unsigned colours = 1u << pi->depth, i, j;
    int last = -1;
    size_t at = 0;

    for (i = 0; i < colours; i++)
        for (j = 0; j < colours; j++)
            pi->recent[i][j] = (uint8_t)((i - j) & (colours - 1u));
    /* The first two colours fill the lines above the picture. */
    if (!pi_pair(pi, 0, count))
        return CODEC_TRUNCATED;
    while (at < count) {
        int position = pi_position(&pi->bits);
        if (position < 0)
            return CODEC_TRUNCATED;
        if (position == last) {
            /* Pairs of literal colours, while a 1 bit follows each. */
            do {
                if (!pi_pair(pi, at, count))
                    return CODEC_TRUNCATED;
                at += 2;
            } while (at < count && jp_bit(&pi->bits) == 1);
            last = -1;
        } else {
            size_t distance, end;
            long length = pi_number(&pi->bits, 0, 23);
            if (length < 0)
                return CODEC_TRUNCATED;
            last = position;
            switch (position) {
            case 0: {
                /* Repeat the last pair, or the pair before it if the last
                   one was two colours. */
                size_t pair = at == 0 ? 0 : at - 2;
                distance = pi->indexes[pair] == pi->indexes[pair + 1] ? 2 : 4;
                break;
            }
            case 1: distance = width; break;
            case 2: distance = (size_t)width * 2u; break;
            case 3: distance = (size_t)width - 1u; break;
            default: distance = (size_t)width + 1u; break;
            }
            end = (size_t)length * 2u > count - at ? count
                                                   : at + (size_t)length * 2u;
            /* A run past the last pixel is clipped. */
            for (; at < end; at++)
                pi->indexes[at] = at >= distance
                                ? pi->indexes[at - distance]
                                : pi->indexes[(distance - at) & 1u];
        }
    }
    return CODEC_OK;
}

/* 16 colours: bit 0 blue, 1 red, 2 green, 3 bright (F0 rather than 70),
   shown with 4-bit precision. 256 colours: GGGRRRBB. */
static void pi_default_palette(uint32_t *palette, unsigned depth)
{
    unsigned i;
    if (depth == 4) {
        for (i = 0; i < 16; i++) {
            unsigned on = i & 8u ? 0xf0u : 0x70u;
            palette[i] = jp_dac_colour(JP_DAC_4, i & 2u ? on : 0,
                                       i & 4u ? on : 0, i & 1u ? on : 0);
        }
    } else {
        for (i = 0; i < 256; i++) {
            unsigned r = i >> 2 & 7u, g = i >> 5, b = i & 3u;
            palette[i] = (uint32_t)(r << 5 | r << 2 | r >> 1) << 16 |
                         (uint32_t)(g << 5 | g << 2 | g >> 1) << 8 |
                         b * 0x55u;
        }
    }
}

enum codec_result jp_decode_pi(const uint8_t *data, size_t size,
                               struct jp_canvas *canvas)
{
    size_t at = 2, extra, stored, count, i;
    unsigned width, height, depth, repeat_x, repeat_y;
    uint32_t palette[256];
    enum codec_result result;
    enum jp_dac dac;
    struct pi pi;
    const uint8_t *h;

    /* A comment ending in 0x1A, then padding ending in 0. */
    while (at < size && data[at] != 0x1a)
        at++;
    while (at < size && data[at] != 0)
        at++;
    at++;
    if (at > size || size - at < 10)
        return CODEC_TRUNCATED;
    h = data + at;
    depth = h[3];
    if (depth != 4 && depth != 8)
        return CODEC_INVALID;
    /* Lines are n/m times as tall as they should be; either 0 means 1. */
    repeat_x = h[1] != 0 && h[2] == h[1] * 2u ? 2 : 1;
    dac = jp_machine_dac(h + 4, h[2] != 0 && h[1] == h[2] * 2u,
                         1u << depth, &repeat_y);
    extra = (size_t)h[8] << 8 | h[9];
    at += 10;
    /* The mode's top bit leaves the palette out for the default one. */
    stored = h[0] & 0x80u ? 0 : 3u << depth;
    if (size - at < extra + 4u + stored)
        return CODEC_TRUNCATED;
    at += extra;
    width = (unsigned)data[at] << 8 | data[at + 1];
    height = (unsigned)data[at + 2] << 8 | data[at + 3];
    at += 4;
    if (stored == 0)
        pi_default_palette(palette, depth);
    else
        jp_palette(palette, data + at, 1u << depth, 0, 1, dac);
    at += stored;
    /* Pictures 1 or 2 pixels wide are coded differently. */
    if (width != 0 && width <= 2)
        return CODEC_INVALID;

    result = jp_canvas_init(canvas, width, height, repeat_x, repeat_y);
    if (result != CODEC_OK)
        return result;
    count = (size_t)width * height;
    pi.depth = depth;
    pi.indexes = malloc(count);
    pi.recent = malloc(256u * sizeof *pi.recent);
    if (pi.indexes == NULL || pi.recent == NULL) {
        free(pi.indexes);
        free(pi.recent);
        return CODEC_NO_MEMORY;
    }
    jp_bits_init(&pi.bits, data, at, size);
    result = pi_unpack(&pi, width, count);
    if (result == CODEC_OK)
        for (i = 0; i < count; i++)
            canvas->rgb[i] = palette[pi.indexes[i]];
    free(pi.indexes);
    free(pi.recent);
    return result;
}
