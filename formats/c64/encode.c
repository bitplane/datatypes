#include "encode.h"
#include <stdlib.h>
#include <string.h>

#define CELLS 1000u

static uint8_t over_white(uint8_t c, uint8_t a)
{
    return (uint8_t)((c * a + 255u * (255u - a) + 127u) / 255u);
}

/* Palette index of every pixel, or 0 if any pixel isn't a Pepto colour. */
static int indexes(const uint8_t *rgba, uint8_t *index)
{
    size_t i;

    for (i = 0; i < (size_t)C64_WIDTH * C64_HEIGHT; i++) {
        const uint8_t *p = rgba + i * 4u;
        uint8_t rgb[3];
        int c;
        rgb[0] = over_white(p[0], p[3]);
        rgb[1] = over_white(p[1], p[3]);
        rgb[2] = over_white(p[2], p[3]);
        c = c64_index(rgb);
        if (c < 0)
            return 0;
        index[i] = (uint8_t)c;
    }
    return 1;
}

/* Bit mask of the colours in the cell whose top left pixel is (cx, cy). */
static unsigned cell_colours(const uint8_t *index, unsigned cx, unsigned cy)
{
    unsigned mask = 0, x, y;

    for (y = cy; y < cy + 8u; y++)
        for (x = cx; x < cx + 8u; x++)
            mask |= 1u << index[y * C64_WIDTH + x];
    return mask;
}

static unsigned count_bits(unsigned mask)
{
    unsigned n = 0;

    for (; mask != 0; mask &= mask - 1u)
        n++;
    return n;
}

static void put_load_address(uint8_t *out, unsigned address)
{
    out[0] = (uint8_t)(address & 0xffu);
    out[1] = (uint8_t)(address >> 8);
}

static int multicolour_shaped(const uint8_t *index)
{
    size_t i;

    for (i = 0; i < (size_t)C64_WIDTH * C64_HEIGHT; i += 2)
        if (index[i] != index[i + 1])
            return 0;
    return 1;
}

/* Koala Painter: bitmap, video matrix, colour RAM, background. The
   background is the most used colour among those every four-colour cell
   contains; each cell's other colours take bit pairs 1-3 in ascending order. */
static int koala(const uint8_t *index, uint8_t *out)
{
    unsigned masks[CELLS], allowed = 0xffffu, counts[16] = { 0 };
    unsigned background = 16, cell, best = 0, c;
    size_t i;

    for (cell = 0; cell < CELLS; cell++) {
        masks[cell] = cell_colours(index, cell % 40u * 8u, cell / 40u * 8u);
        if (count_bits(masks[cell]) > 4)
            return 0;
        if (count_bits(masks[cell]) == 4)
            allowed &= masks[cell];
    }
    for (i = 0; i < (size_t)C64_WIDTH * C64_HEIGHT; i++)
        counts[index[i]]++;
    for (c = 0; c < 16; c++)
        if ((allowed >> c & 1u) && (background == 16 || counts[c] > best)) {
            background = c;
            best = counts[c];
        }
    if (background == 16)
        return 0;

    memset(out, 0, C64_KOALA_SIZE);
    put_load_address(out, 0x6000);
    out[0x2712] = (uint8_t)background;
    for (cell = 0; cell < CELLS; cell++) {
        unsigned cx = cell % 40u * 8u, cy = cell / 40u * 8u, x, y;
        uint8_t slot[16] = { 0 }, colours[3] = { 0, 0, 0 };
        unsigned n = 0;
        for (c = 0; c < 16; c++)
            if ((masks[cell] >> c & 1u) && c != background) {
                colours[n] = (uint8_t)c;
                slot[c] = (uint8_t)++n;
            }
        out[0x1f42 + cell] = (uint8_t)(colours[0] << 4 | colours[1]);
        out[0x232a + cell] = colours[2];
        for (y = 0; y < 8u; y++) {
            unsigned bits = 0;
            for (x = 0; x < 8u; x += 2)
                bits = bits << 2 | slot[index[(cy + y) * C64_WIDTH + cx + x]];
            out[2 + cell * 8u + y] = (uint8_t)bits;
        }
    }
    return 1;
}

/* Art Studio: bitmap, then video matrix; in a two-colour cell the lower
   colour is the clear bits, a one-colour cell is all clear. */
static int art_studio(const uint8_t *index, uint8_t *out)
{
    unsigned cell;

    memset(out, 0, C64_ART_STUDIO_SIZE);
    put_load_address(out, 0x2000);
    for (cell = 0; cell < CELLS; cell++) {
        unsigned cx = cell % 40u * 8u, cy = cell / 40u * 8u, x, y, c;
        unsigned mask = cell_colours(index, cx, cy), low = 16, high = 0;
        if (count_bits(mask) > 2)
            return 0;
        for (c = 0; c < 16; c++)
            if (mask >> c & 1u) {
                if (low == 16)
                    low = c;
                else
                    high = c;
            }
        out[0x1f42 + cell] = (uint8_t)(high << 4 | low);
        for (y = 0; y < 8u; y++) {
            unsigned bits = 0;
            for (x = 0; x < 8u; x++)
                bits = bits << 1 | (index[(cy + y) * C64_WIDTH + cx + x] != low);
            out[2 + cell * 8u + y] = (uint8_t)bits;
        }
    }
    return 1;
}

enum codec_result c64_encode(const uint8_t *rgba, unsigned width, unsigned height,
                             uint8_t output[C64_ENCODE_MAX], size_t *size)
{
    uint8_t *index;
    enum codec_result r = CODEC_INVALID;

    if (size != NULL)
        *size = 0;
    if (rgba == NULL || output == NULL || size == NULL ||
        width != C64_WIDTH || height != C64_HEIGHT)
        return CODEC_INVALID;
    index = malloc((size_t)C64_WIDTH * C64_HEIGHT);
    if (index == NULL)
        return CODEC_NO_MEMORY;
    if (indexes(rgba, index)) {
        if (multicolour_shaped(index) && koala(index, output)) {
            *size = C64_KOALA_SIZE;
            r = CODEC_OK;
        } else if (art_studio(index, output)) {
            *size = C64_ART_STUDIO_SIZE;
            r = CODEC_OK;
        }
    }
    free(index);
    return r;
}
