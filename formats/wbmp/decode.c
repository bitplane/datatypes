#include "decode.h"
#include <stdlib.h>

#define WBMP_MAX_PIXELS (16u * 1024u * 1024u)

/* Seven bits per byte, most significant first; a set top bit means more follow. */
static enum codec_result read_uintvar(const uint8_t *data, size_t length,
                                      size_t *pos, uint32_t *value)
{
    uint32_t result = 0;
    unsigned count;

    for (count = 0; count < 5; count++) {
        uint8_t byte;
        if (*pos >= length)
            return CODEC_TRUNCATED;
        byte = data[(*pos)++];
        if (result > (UINT32_MAX >> 7))
            return CODEC_INVALID;
        result = (result << 7) | (byte & 0x7fu);
        if ((byte & 0x80u) == 0) {
            *value = result;
            return CODEC_OK;
        }
    }
    return CODEC_INVALID;
}

void wbmp_free(struct wbmp_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
    image->width = image->height = 0;
}

enum codec_result wbmp_decode(const uint8_t *data, size_t length, struct wbmp_image *image)
{
    uint32_t type, width, height;
    size_t pos = 0, row_bytes, x, y;
    uint8_t *out;
    enum codec_result result;

    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->rgba = NULL;
    if (data == NULL)
        return CODEC_TRUNCATED;
    if ((result = read_uintvar(data, length, &pos, &type)) != CODEC_OK)
        return result;
    if (type != 0)
        return CODEC_INVALID;
    if (pos >= length)
        return CODEC_TRUNCATED;
    /* Type 0 has no extension headers, so its fixed header byte is zero. */
    if (data[pos++] != 0)
        return CODEC_INVALID;
    if ((result = read_uintvar(data, length, &pos, &width)) != CODEC_OK ||
        (result = read_uintvar(data, length, &pos, &height)) != CODEC_OK)
        return result;
    if (width == 0 || height == 0)
        return CODEC_INVALID;
    if (width > 65535u || height > 65535u ||
        (size_t)width * height > WBMP_MAX_PIXELS)
        return CODEC_TOO_LARGE;
    row_bytes = (width + 7u) / 8u;
    if (length - pos < row_bytes * height)
        return CODEC_TRUNCATED;
    image->rgba = malloc((size_t)width * height * 4u);
    if (image->rgba == NULL)
        return CODEC_NO_MEMORY;
    image->width = width; image->height = height;
    out = image->rgba;
    for (y = 0; y < height; y++, pos += row_bytes) {
        for (x = 0; x < width; x++) {
            uint8_t value = (data[pos + x / 8u] & (0x80u >> (x % 8u))) ? 255 : 0;
            *out++ = value; *out++ = value; *out++ = value; *out++ = 255;
        }
    }
    return CODEC_OK;
}
