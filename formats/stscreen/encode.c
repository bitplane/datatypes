#include "encode.h"
#include "common/atarist.h"
#include <string.h>

/* Paintworks layouts, in the order they are tried for a size. */
static const struct layout {
    unsigned width, height, mode, doubled;
} layouts[] = {
    { 320, 200, 0, 0 }, { 640, 200, 1, 0 }, { 640, 400, 2, 0 },
    { 640, 400, 1, 1 }, { 320, 400, 0, 1 }, { 640, 800, 2, 1 }
};

static void put_be16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static uint8_t over_white(uint8_t c, uint8_t a)
{
    return (uint8_t)((c * a + 255u * (255u - a) + 127u) / 255u);
}

static void composite(const uint8_t *p, uint8_t rgb[3])
{
    rgb[0] = over_white(p[0], p[3]);
    rgb[1] = over_white(p[1], p[3]);
    rgb[2] = over_white(p[2], p[3]);
}

/* The 12-bit palette entry for rgb, or -1 if it can't be stored. */
static int entry(const uint8_t *rgb, int ste)
{
    int r = st_nibble(rgb[0], ste), g = st_nibble(rgb[1], ste), b = st_nibble(rgb[2], ste);

    if (r < 0 || g < 0 || b < 0)
        return -1;
    return (r << 8) | (g << 4) | b;
}

static enum codec_result encode(const uint8_t *rgba, const struct layout *l,
                                uint8_t *output, size_t *size)
{
    uint8_t colour[16][3];
    unsigned planes = 4u >> l->mode, limit = 1u << planes, count = 0, x, y, i;
    int words[16], ste, marked = 0;
    size_t pixel, pixels = (size_t)l->width * l->height;

    if (l->mode == 2) {
        /* High resolution has no palette: 0 is white and 1 is black. */
        memset(colour[0], 255, 3);
        memset(colour[1], 0, 3);
        count = 2;
        words[0] = 0x777;
        words[1] = 0;
    }
    for (pixel = 0; pixel < pixels; pixel++) {
        uint8_t rgb[3];
        composite(rgba + pixel * 4u, rgb);
        for (i = 0; i < count && memcmp(colour[i], rgb, 3) != 0; i++)
            ;
        if (i == count) {
            if (count == limit)
                return CODEC_INVALID;
            memcpy(colour[count++], rgb, 3);
        }
    }

    if (l->mode < 2) {
        /* ST levels where they cover the image, STE levels otherwise. */
        for (ste = 0; ste < 2; ste++) {
            for (i = 0; i < count && (words[i] = entry(colour[i], ste)) >= 0; i++)
                marked |= words[i] & 0x888;
            if (i == count)
                break;
            marked = 0;
        }
        if (ste == 2)
            return CODEC_INVALID;
        /* An STE palette is recognised by a fourth bit in a used colour. */
        if (ste && !marked) {
            if (count == limit)
                return CODEC_INVALID;
            words[count++] = 0x888;
        }
    }

    *size = 128u + ((size_t)32000u << l->doubled);
    memset(output, 0, *size);
    put_be16(output + 2, l->mode);
    for (i = 0; i < count; i++)
        put_be16(output + 4 + i * 2u, (unsigned)words[i]);
    memcpy(output + 36, "        .   ", 12);
    memcpy(output + 0x36, "ANvisionA", 9);
    /* Low nibble 1 is a screen, 0 a page of twice the height. */
    output[0x3f] = (uint8_t)(l->mode << 4 | (l->doubled ? 0u : 1u));
    for (y = 0; y < l->height; y++) {
        uint8_t *line = output + 128 + (size_t)y * l->width * planes / 8u;
        for (x = 0; x < l->width; x++) {
            uint8_t rgb[3], *group = line + (x / 16u) * planes * 2u;
            unsigned bit = 15u - x % 16u, p;
            composite(rgba + ((size_t)y * l->width + x) * 4u, rgb);
            for (i = 0; memcmp(colour[i], rgb, 3) != 0; i++)
                ;
            for (p = 0; p < planes; p++)
                if (i >> p & 1u)
                    group[p * 2u + (bit < 8u)] |= (uint8_t)(1u << bit % 8u);
        }
    }
    return CODEC_OK;
}

enum codec_result stscreen_encode(const uint8_t *rgba, unsigned width,
                                  unsigned height,
                                  uint8_t output[STSCREEN_MAX_OUTPUT],
                                  size_t *size)
{
    size_t i;

    if (rgba == NULL || output == NULL || size == NULL)
        return CODEC_INVALID;
    *size = 0;
    for (i = 0; i < sizeof layouts / sizeof layouts[0]; i++)
        if (layouts[i].width == width && layouts[i].height == height &&
            encode(rgba, &layouts[i], output, size) == CODEC_OK)
            return CODEC_OK;
    return CODEC_INVALID;
}
