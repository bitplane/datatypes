#include "decode.h"
#include "palette.h"
#include <stdlib.h>

#define WAL_MAX_PIXELS (16u * 1024u * 1024u)

static uint32_t get32(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static enum codec_result check(const uint8_t *data, size_t length,
                               uint32_t *width, uint32_t *height)
{
    if (data == NULL || length < WAL_HEADER)
        return CODEC_TRUNCATED;
    *width = get32(data + 32);
    *height = get32(data + 36);
    if (*width == 0 || *height == 0)
        return CODEC_INVALID;
    if (*width > 65535u || *height > 65535u ||
        (size_t)*width * *height > WAL_MAX_PIXELS)
        return CODEC_TOO_LARGE;
    return CODEC_OK;
}

/* Level 0 always counts; a mip counts while it has pixels and an offset. */
unsigned wal_count(const uint8_t *data, size_t length)
{
    uint32_t width, height;
    unsigned level;

    if (check(data, length, &width, &height) != CODEC_OK)
        return 0;
    for (level = 1; level < WAL_LEVELS; level++)
        if ((width >> level) == 0 || (height >> level) == 0 ||
            get32(data + 40 + level * 4u) == 0)
            break;
    return level;
}

void wal_free(struct wal_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
    image->width = image->height = 0;
}

enum codec_result wal_decode(const uint8_t *data, size_t length, unsigned level,
                             struct wal_image *image)
{
    uint32_t width, height, offset;
    size_t pixels, i;
    enum codec_result result;
    uint8_t *out;

    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->rgba = NULL;
    if ((result = check(data, length, &width, &height)) != CODEC_OK)
        return result;
    if (level >= wal_count(data, length))
        return CODEC_INVALID;
    width >>= level;
    height >>= level;
    offset = get32(data + 40 + level * 4u);
    pixels = (size_t)width * height;
    if (offset > length || length - offset < pixels)
        return CODEC_TRUNCATED;
    image->rgba = malloc(pixels * 4u);
    if (image->rgba == NULL)
        return CODEC_NO_MEMORY;
    image->width = width;
    image->height = height;
    out = image->rgba;
    for (i = 0; i < pixels; i++, out += 4) {
        const uint8_t *rgb = wal_palette + data[offset + i] * 3u;
        out[0] = rgb[0]; out[1] = rgb[1]; out[2] = rgb[2]; out[3] = 255;
    }
    return CODEC_OK;
}
