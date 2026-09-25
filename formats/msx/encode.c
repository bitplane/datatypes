#include <string.h>

#include "encode.h"

#define HEADER 7u

/* An 8-bit level's 3-bit V9938 level, or -1 if it isn't one exactly. */
static int level(unsigned v)
{
    unsigned l = v >> 5;
    return (l << 5 | l << 2 | l >> 1) == v ? (int)l : -1;
}

static void over_white(const uint8_t *p, unsigned rgb[3])
{
    unsigned c;

    for (c = 0; c < 3; c++)
        rgb[c] = (p[c] * p[3] + 255u * (255u - p[3]) + 127u) / 255u;
}

static void start(uint8_t *output, unsigned end)
{
    memset(output, 0, HEADER + end + 1u);
    output[0] = 0xfe;
    output[3] = (uint8_t)end;
    output[4] = (uint8_t)(end >> 8);
}

/* Screens 5 and 7: up to 16 colours, a nibble each. The dump reaches the
   palette, and the first sprite hides the rest. */
static enum codec_result paletted(const uint8_t *rgba, unsigned width,
                                  uint8_t *output, size_t *size)
{
    unsigned stride = width / 2u, count = 0, x, y, i;
    unsigned sprite = width == 256 ? 0x7600 : 0xfa00;
    unsigned end = width == 256 ? 0x769f : 0xfa9f;
    uint8_t *vram = output + HEADER, *palette = vram + sprite + 0x80;
    unsigned colors[16];

    start(output, end);
    for (y = 0; y < 212; y++)
        for (x = 0; x < width; x++) {
            unsigned rgb[3], packed;
            int r, g, b;

            over_white(rgba + ((size_t)y * width + x) * 4u, rgb);
            r = level(rgb[0]);
            g = level(rgb[1]);
            b = level(rgb[2]);
            if (r < 0 || g < 0 || b < 0)
                return CODEC_INVALID;
            packed = (unsigned)(r << 6 | g << 3 | b);
            for (i = 0; i < count && colors[i] != packed; i++)
                ;
            if (i == count) {
                if (count == 16)
                    return CODEC_INVALID;
                colors[count++] = packed;
                palette[2 * i] = (uint8_t)(r << 4 | b);
                palette[2 * i + 1] = (uint8_t)g;
            }
            vram[y * stride + x / 2] |= (uint8_t)(x & 1 ? i : i << 4);
        }
    vram[sprite] = 216;
    *size = HEADER + end + 1u;
    return CODEC_OK;
}

/* Screen 8: GGGRRRBB, blue on four of the eight levels. */
static enum codec_result direct(const uint8_t *rgba, uint8_t *output,
                                size_t *size)
{
    unsigned x, y;

    start(output, 0xd3ff);
    for (y = 0; y < 212; y++)
        for (x = 0; x < 256; x++) {
            unsigned rgb[3];
            int r, g, b;

            over_white(rgba + ((size_t)y * 256u + x) * 4u, rgb);
            r = level(rgb[0]);
            g = level(rgb[1]);
            b = level(rgb[2]);
            if (r < 0 || g < 0 || b < 0 || b == 1 || b == 3 || b == 5 || b == 6)
                return CODEC_INVALID;
            output[HEADER + y * 256u + x] =
                (uint8_t)(g << 5 | r << 2 | (b == 7 ? 3 : b >> 1));
        }
    *size = HEADER + 0xd400u;
    return CODEC_OK;
}

enum codec_result msx_encode(const uint8_t *rgba, unsigned width,
                             unsigned height, uint8_t output[MSX_MAX_OUTPUT],
                             size_t *size)
{
    if (rgba == NULL || output == NULL || size == NULL || height != 212)
        return CODEC_INVALID;
    if (width == 512)
        return paletted(rgba, width, output, size);
    if (width != 256)
        return CODEC_INVALID;
    if (paletted(rgba, width, output, size) == CODEC_OK)
        return CODEC_OK;
    return direct(rgba, output, size);
}
