#include "decode.h"
#include <stdlib.h>

void cmuwm_free(struct cmuwm_image *image)
{
    free(image->pixels);
    image->pixels = NULL;
    image->width = image->height = 0;
}

static uint32_t get32(const uint8_t *p, int little)
{
    if (little)
        return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
}

static unsigned get16(const uint8_t *p, int little)
{
    return little ? (unsigned)p[0] | (unsigned)p[1] << 8 : (unsigned)p[0] << 8 | p[1];
}

enum codec_result cmuwm_decode(const uint8_t *data, size_t length, struct cmuwm_image *image)
{
    size_t header, row_bytes, need, x, y;
    uint32_t width, height;
    int little;

    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->pixels = NULL;
    if (data == NULL || length < 4)
        return CODEC_TRUNCATED;
    if (get32(data, 0) == 0xf10040bbUL)
        little = 0;
    else if (get32(data, 1) == 0xf10040bbUL)
        little = 1;
    else
        return CODEC_INVALID;
    if (length < CMUWM_HEADER_SIZE)
        return CODEC_TRUNCATED;
    width = get32(data + 4, little);
    height = get32(data + 8, little);
    if (width == 0 || height == 0)
        return CODEC_INVALID;
    if (width > CMUWM_MAX_SIDE || height > CMUWM_MAX_SIDE ||
        (unsigned long)width * height > CMUWM_MAX_PIXELS)
        return CODEC_TOO_LARGE;
    row_bytes = (width + 7u) / 8u;
    need = row_bytes * height;
    /* As the Andrew Toolkit reads it: a file with at least two bytes more
       than a 14-byte header needs has a 16-byte one. */
    if (length - CMUWM_HEADER_SIZE < need)
        return CODEC_TRUNCATED;
    header = length - CMUWM_HEADER_SIZE - need >= 2 ? CMUWM_LONG_HEADER_SIZE : CMUWM_HEADER_SIZE;
    /* A 16-bit depth, or a 32-bit one in a 16-byte header. */
    if (get16(data + 12, little) != 1 &&
        !(header == CMUWM_LONG_HEADER_SIZE && get32(data + 12, little) == 1))
        return CODEC_INVALID;
    image->pixels = malloc((size_t)width * height);
    if (image->pixels == NULL)
        return CODEC_NO_MEMORY;
    data += header;
    for (y = 0; y < height; y++, data += row_bytes)
        for (x = 0; x < width; x++)
            image->pixels[y * width + x] = ((data[x / 8] >> (7 - x % 8)) & 1u) ^ 1u;
    image->width = width;
    image->height = height;
    return CODEC_OK;
}
