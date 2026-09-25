#include "encode.h"
#include "common/atarist.h"
#include <string.h>

static void put_be16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static uint8_t over_white(uint8_t c, uint8_t a)
{
    return (uint8_t)((c * a + 255u * (255u - a) + 127u) / 255u);
}

/* The 12-bit palette entry for rgb, or -1 if it can't be stored. */
static int entry(const uint8_t *rgb, int ste)
{
    int r = st_nibble(rgb[0], ste), g = st_nibble(rgb[1], ste), b = st_nibble(rgb[2], ste);

    if (r < 0 || g < 0 || b < 0)
        return -1;
    return (r << 8) | (g << 4) | b;
}

enum codec_result neo_encode(const uint8_t *rgba, unsigned width,
                             unsigned height, uint8_t output[NEO_FILE_SIZE])
{
    uint8_t colour[16][3];
    unsigned resolution, planes, limit, count = 0, x, y, i;
    int words[16], ste = -1, marked = 0;
    size_t pixel;

    if (rgba == NULL || output == NULL)
        return CODEC_INVALID;
    if (width == 320 && height == 200)
        resolution = 0;
    else if (width == 640 && height == 200)
        resolution = 1;
    else if (width == 640 && height == 400)
        resolution = 2;
    else
        return CODEC_INVALID;
    planes = 4u >> resolution;
    limit = 1u << planes;
    memset(output, 0, NEO_FILE_SIZE);

    if (resolution == 2) {
        /* High resolution has no palette: 0 is white and 1 is black. */
        memset(colour[0], 255, 3);
        memset(colour[1], 0, 3);
        count = 2;
        words[0] = 0x777;
        words[1] = 0;
    }
    for (pixel = 0; pixel < (size_t)width * height; pixel++) {
        const uint8_t *p = rgba + pixel * 4u;
        uint8_t rgb[3];
        rgb[0] = over_white(p[0], p[3]);
        rgb[1] = over_white(p[1], p[3]);
        rgb[2] = over_white(p[2], p[3]);
        for (i = 0; i < count && memcmp(colour[i], rgb, 3) != 0; i++)
            ;
        if (i == count) {
            if (count == limit)
                return CODEC_INVALID;
            memcpy(colour[count++], rgb, 3);
        }
    }

    if (resolution < 2) {
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

    put_be16(output + 2, resolution);
    for (i = 0; i < count; i++)
        put_be16(output + 4 + i * 2u, (unsigned)words[i]);
    memcpy(output + 36, "        .   ", 12);
    for (y = 0; y < height; y++) {
        uint8_t *line = output + 128 + (size_t)y * width * planes / 8u;
        for (x = 0; x < width; x++) {
            const uint8_t *p = rgba + ((size_t)y * width + x) * 4u;
            uint8_t rgb[3], *group = line + (x / 16u) * planes * 2u;
            unsigned bit = 15u - x % 16u, p2;
            rgb[0] = over_white(p[0], p[3]);
            rgb[1] = over_white(p[1], p[3]);
            rgb[2] = over_white(p[2], p[3]);
            for (i = 0; memcmp(colour[i], rgb, 3) != 0; i++)
                ;
            for (p2 = 0; p2 < planes; p2++)
                if (i >> p2 & 1u)
                    group[p2 * 2u + (bit < 8u)] |= (uint8_t)(1u << bit % 8u);
        }
    }
    return CODEC_OK;
}
