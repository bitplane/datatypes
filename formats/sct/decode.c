#include "decode.h"
#include <stdlib.h>

#define SCT_MAX_PIXELS (16u * 1024u * 1024u)
#define SCT_PARAMETERS 1024u
#define SCT_DATA 2048u

/* Separation mask bits, stored in this order. */
#define SEP_C 1u
#define SEP_M 2u
#define SEP_Y 4u
#define SEP_K 8u

/* A 12-character Long: "+00000000512". Leading blanks and trailing blanks
   or NULs are tolerated; the value saturates above 65536. Returns 0 for
   anything else, including negative numbers. */
static int parse_long(const uint8_t *p, unsigned long *value)
{
    size_t i = 0;

    while (i < 12 && p[i] == ' ')
        i++;
    if (i < 12 && p[i] == '+')
        i++;
    if (i >= 12 || p[i] < '0' || p[i] > '9')
        return 0;
    *value = 0;
    while (i < 12 && p[i] >= '0' && p[i] <= '9') {
        *value = *value * 10u + (unsigned long)(p[i] - '0');
        if (*value > 65536u)
            *value = 65536u;
        i++;
    }
    while (i < 12 && (p[i] == ' ' || p[i] == '\0'))
        i++;
    return i == 12;
}

static unsigned bit_count(unsigned mask)
{
    unsigned n = 0;
    for (; mask != 0; mask >>= 1)
        n += mask & 1u;
    return n;
}

void sct_free(struct sct_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
    image->width = image->height = 0;
}

enum codec_result sct_decode(const uint8_t *data, size_t length,
                             struct sct_image *image)
{
    const uint8_t *par = data + SCT_PARAMETERS;
    static const uint8_t no_ink = 255;
    const uint8_t *sep[4];
    unsigned present[4];
    unsigned long width, height;
    unsigned count, mask, s, n, x, y;
    size_t row;

    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->rgba = NULL;
    if (data == NULL || length < 82)
        return CODEC_TRUNCATED;
    if (data[80] != 'C' || data[81] != 'T')
        return CODEC_INVALID;
    if (length < SCT_DATA)
        return CODEC_TRUNCATED;
    count = par[1];
    mask = (unsigned)par[2] << 8 | par[3];
    /* Writers that leave the mask empty get the usual meaning of the count. */
    if (mask == 0)
        mask = count == 1 ? SEP_K : count == 3 ? SEP_C | SEP_M | SEP_Y :
               count == 4 ? SEP_C | SEP_M | SEP_Y | SEP_K : 0;
    /* Separations beyond CMYK are undefined; each present one is stored. */
    if (mask == 0 || mask > 15u || bit_count(mask) != count)
        return CODEC_INVALID;
    if (!parse_long(par + 0x20, &height) || !parse_long(par + 0x2c, &width) ||
        width == 0 || height == 0)
        return CODEC_INVALID;
    if (width > 65535u || height > 65535u || width * height > SCT_MAX_PIXELS)
        return CODEC_TOO_LARGE;
    row = ((size_t)width + 1u) & ~(size_t)1u;
    if ((length - SCT_DATA) / (row * count) < height)
        return CODEC_TRUNCATED;
    image->rgba = malloc((size_t)width * height * 4u);
    if (image->rgba == NULL)
        return CODEC_NO_MEMORY;
    image->width = (unsigned)width;
    image->height = (unsigned)height;
    for (y = 0; y < height; y++) {
        const uint8_t *line = data + SCT_DATA + (size_t)y * row * count;
        uint8_t *dst = image->rgba + (size_t)y * width * 4u;
        /* Absent separations read as a single 255: no ink. */
        for (s = n = 0; s < 4; s++) {
            present[s] = mask >> s & 1u;
            sep[s] = present[s] ? line + row * n++ : &no_ink;
        }
        for (x = 0; x < width; x++) {
            /* 255 is no ink and 0 full ink, so each ink scales the light
               that the others leave. Rounded down, as by ImageMagick. */
            unsigned k = sep[3][x * present[3]];
            for (s = 0; s < 3; s++)
                dst[x * 4u + s] = (uint8_t)(sep[s][x * present[s]] * k / 255u);
            dst[x * 4u + 3u] = 255;
        }
    }
    return CODEC_OK;
}
