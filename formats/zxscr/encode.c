#include "encode.h"
#include <string.h>

static uint8_t over_white(uint8_t c, uint8_t a)
{
    return (uint8_t)((c * a + 255u * (255u - a) + 127u) / 255u);
}

/* Colour 0-7 plus 8 if bright, or -1 if p isn't a Spectrum colour.
   Black is returned as 0. */
static int lookup(const uint8_t *p)
{
    uint8_t rgb[3], want[3];
    unsigned c;
    int bright;

    rgb[0] = over_white(p[0], p[3]);
    rgb[1] = over_white(p[1], p[3]);
    rgb[2] = over_white(p[2], p[3]);
    for (bright = 0; bright < 2; bright++)
        for (c = 0; c < 8; c++) {
            zxscr_colour(c, bright, want);
            if (memcmp(rgb, want, 3) == 0)
                return (int)c + (c ? bright * 8 : 0);
        }
    return -1;
}

/* Fill output with cells cell_height rows high (8 or 1). Attributes go
   after the bitmap: linear for 8x8 cells, interleaved like it for 8x1. */
static enum codec_result encode_cells(const uint8_t *rgba, unsigned cell_height,
                                      uint8_t *output)
{
    unsigned cx, cy, x, y;

    memset(output, 0, cell_height == 8u ? ZXSCR_FILE_SIZE : ZXSCR_TIMEX_SIZE);
    for (cy = 0; cy < ZXSCR_HEIGHT / cell_height; cy++)
        for (cx = 0; cx < ZXSCR_WIDTH / 8u; cx++) {
            int index[64], used[2], count = 0, bright = -1, i, n = (int)cell_height * 8;
            unsigned paper, ink;
            size_t at;

            for (i = 0; i < n; i++) {
                x = cx * 8u + (unsigned)i % 8u;
                y = cy * cell_height + (unsigned)i / 8u;
                index[i] = lookup(rgba + ((size_t)y * ZXSCR_WIDTH + x) * 4u);
                if (index[i] < 0)
                    return CODEC_INVALID;
                if (index[i] != 0) {
                    if (bright >= 0 && bright != index[i] >> 3)
                        return CODEC_INVALID;
                    bright = index[i] >> 3;
                }
                index[i] &= 7;
                if (!(count > 0 && used[0] == index[i]) &&
                    !(count > 1 && used[1] == index[i])) {
                    if (count == 2)
                        return CODEC_INVALID;
                    used[count++] = index[i];
                }
            }
            /* The lower colour is paper; a plain cell is paper only. */
            paper = (unsigned)used[0];
            ink = (unsigned)used[count - 1];
            if (ink < paper) {
                ink = paper;
                paper = (unsigned)used[1];
            }
            at = cell_height == 8u ? cy * 32u + cx : zxscr_offset(cx * 8u, cy);
            output[6144u + at] = (uint8_t)(ink | paper << 3 | (bright > 0 ? 0x40u : 0u));
            for (i = 0; i < n; i++)
                if ((unsigned)index[i] != paper) {
                    x = cx * 8u + (unsigned)i % 8u;
                    y = cy * cell_height + (unsigned)i / 8u;
                    output[zxscr_offset(x, y)] |= (uint8_t)(0x80u >> (x % 8u));
                }
        }
    return CODEC_OK;
}

enum codec_result zxscr_encode(const uint8_t *rgba, unsigned width, unsigned height,
                               uint8_t output[ZXSCR_TIMEX_SIZE], size_t *length)
{
    if (rgba == NULL || output == NULL || length == NULL ||
        width != ZXSCR_WIDTH || height != ZXSCR_HEIGHT)
        return CODEC_INVALID;
    *length = ZXSCR_FILE_SIZE;
    if (encode_cells(rgba, 8, output) == CODEC_OK)
        return CODEC_OK;
    *length = ZXSCR_TIMEX_SIZE;
    if (encode_cells(rgba, 1, output) == CODEC_OK)
        return CODEC_OK;
    *length = 0;
    return CODEC_INVALID;
}
