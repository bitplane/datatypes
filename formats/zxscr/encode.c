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

enum codec_result zxscr_encode(const uint8_t *rgba, unsigned width,
                               unsigned height, uint8_t output[ZXSCR_FILE_SIZE])
{
    unsigned cx, cy, x, y;

    if (rgba == NULL || output == NULL || width != ZXSCR_WIDTH || height != ZXSCR_HEIGHT)
        return CODEC_INVALID;
    memset(output, 0, ZXSCR_FILE_SIZE);
    for (cy = 0; cy < ZXSCR_HEIGHT / 8u; cy++)
        for (cx = 0; cx < ZXSCR_WIDTH / 8u; cx++) {
            int index[64], used[2], count = 0, bright = -1, i;
            unsigned paper, ink;

            for (i = 0; i < 64; i++) {
                x = cx * 8u + (unsigned)i % 8u;
                y = cy * 8u + (unsigned)i / 8u;
                index[i] = lookup(rgba + ((size_t)y * width + x) * 4u);
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
            output[6144u + cy * 32u + cx] =
                (uint8_t)(ink | paper << 3 | (bright > 0 ? 0x40u : 0u));
            for (i = 0; i < 64; i++)
                if ((unsigned)index[i] != paper) {
                    x = cx * 8u + (unsigned)i % 8u;
                    y = cy * 8u + (unsigned)i / 8u;
                    output[zxscr_offset(x, y)] |= (uint8_t)(0x80u >> (x % 8u));
                }
        }
    return CODEC_OK;
}
