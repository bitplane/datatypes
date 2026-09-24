#include "decode.h"
#include <stdlib.h>
#include <string.h>

#define FARBFELD_MAX_PIXELS (16u * 1024u * 1024u)

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

/* Nearest 8-bit value: round(v * 255 / 65535). */
static uint8_t to8(const uint8_t *p)
{
    unsigned long v = ((unsigned long)p[0] << 8) | p[1];
    return (uint8_t)((v * 255u + 32767u) / 65535u);
}

void farbfeld_free(struct farbfeld_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
    image->width = image->height = 0;
}

enum codec_result farbfeld_decode(const uint8_t *data, size_t length,
                                  struct farbfeld_image *image)
{
    uint32_t width, height;
    size_t pixels, i;

    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->rgba = NULL;
    if (data == NULL || length < 16)
        return CODEC_TRUNCATED;
    if (memcmp(data, "farbfeld", 8) != 0)
        return CODEC_INVALID;
    width = be32(data + 8);
    height = be32(data + 12);
    if (width == 0 || height == 0)
        return CODEC_INVALID;
    if (width > 65535u || height > 65535u ||
        (size_t)width * height > FARBFELD_MAX_PIXELS)
        return CODEC_TOO_LARGE;
    pixels = (size_t)width * height;
    /* Bytes after the last pixel are ignored. */
    if ((length - 16) / 8 < pixels)
        return CODEC_TRUNCATED;
    image->rgba = malloc(pixels * 4u);
    if (image->rgba == NULL)
        return CODEC_NO_MEMORY;
    image->width = width;
    image->height = height;
    for (i = 0; i < pixels; i++) {
        const uint8_t *src = data + 16 + i * 8u;
        uint8_t *dst = image->rgba + i * 4u;
        dst[0] = to8(src);
        dst[1] = to8(src + 2);
        dst[2] = to8(src + 4);
        dst[3] = to8(src + 6);
    }
    return CODEC_OK;
}
