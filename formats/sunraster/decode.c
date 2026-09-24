#include "decode.h"
#include <stdlib.h>

#define SUNRASTER_MAGIC 0x59a66a95ul
#define SUNRASTER_MAX_PIXELS (16u * 1024u * 1024u)

enum { TYPE_OLD, TYPE_STANDARD, TYPE_RLE, TYPE_RGB };
enum { MAP_NONE, MAP_RGB };

static unsigned long be32(const uint8_t *p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) |
           ((unsigned long)p[2] << 8) | (unsigned long)p[3];
}

/* Expand the 0x80 escapes: 80 00 is a literal 0x80, 80 n v is n + 1 copies of v. */
static enum codec_result unpack_rle(const uint8_t *data, size_t length,
                                    uint8_t *output, size_t size)
{
    size_t pos = 0, out = 0, run;
    while (out < size) {
        if (pos == length)
            return CODEC_TRUNCATED;
        if (data[pos] != 0x80) {
            output[out++] = data[pos++];
            continue;
        }
        if (length - pos < 2)
            return CODEC_TRUNCATED;
        if (data[pos + 1] == 0) {
            output[out++] = 0x80;
            pos += 2;
            continue;
        }
        if (length - pos < 3)
            return CODEC_TRUNCATED;
        run = (size_t)data[pos + 1] + 1u;
        if (run > size - out)
            return CODEC_INVALID;
        while (run-- != 0)
            output[out++] = data[pos + 2];
        pos += 3;
    }
    return CODEC_OK;
}

void sunraster_free(struct sunraster_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
    image->width = image->height = 0;
}

enum codec_result sunraster_decode(const uint8_t *data, size_t length,
                                   struct sunraster_image *image)
{
    unsigned long width, height, depth, type, maptype, maplength;
    size_t row_bytes, size, pos = 32, entries = 0, x, y;
    const uint8_t *map = NULL, *pixels;
    uint8_t *unpacked = NULL;
    enum codec_result result = CODEC_OK;

    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->rgba = NULL;
    if (data == NULL || length < 32)
        return CODEC_TRUNCATED;
    if (be32(data) != SUNRASTER_MAGIC)
        return CODEC_INVALID;
    width = be32(data + 4); height = be32(data + 8); depth = be32(data + 12);
    type = be32(data + 20); maptype = be32(data + 24); maplength = be32(data + 28);
    if (width == 0 || height == 0 ||
        (depth != 1 && depth != 8 && depth != 24 && depth != 32) ||
        type > TYPE_RGB || maptype > MAP_RGB)
        return CODEC_INVALID;
    if (width > 65535u || height > 65535u ||
        (size_t)width * height > SUNRASTER_MAX_PIXELS)
        return CODEC_TOO_LARGE;
    if (maptype == MAP_NONE && maplength != 0)
        return CODEC_INVALID;
    if (maptype == MAP_RGB) {
        if (maplength == 0 || maplength % 3u != 0)
            return CODEC_INVALID;
        entries = maplength / 3u;
        if (depth <= 8 && entries > (1u << depth))
            return CODEC_INVALID;
        if (length - pos < maplength)
            return CODEC_TRUNCATED;
        /* A true-colour image has no use for a colormap; skip it. */
        if (depth <= 8)
            map = data + pos;
        pos += maplength;
    }

    row_bytes = ((size_t)width * depth + 15u) / 16u * 2u;
    size = row_bytes * height;
    if (type == TYPE_RLE) {
        unpacked = malloc(size);
        if (unpacked == NULL)
            return CODEC_NO_MEMORY;
        result = unpack_rle(data + pos, length - pos, unpacked, size);
        if (result != CODEC_OK) {
            free(unpacked);
            return result;
        }
        pixels = unpacked;
    } else {
        if (length - pos < size)
            return CODEC_TRUNCATED;
        pixels = data + pos;
    }

    image->rgba = malloc((size_t)width * height * 4u);
    if (image->rgba == NULL) {
        free(unpacked);
        return CODEC_NO_MEMORY;
    }
    image->width = (unsigned)width; image->height = (unsigned)height;
    for (y = 0; y < height; y++) {
        const uint8_t *row = pixels + y * row_bytes;
        for (x = 0; x < width; x++) {
            uint8_t *rgba = image->rgba + (y * width + x) * 4u;
            if (depth >= 24) {
                /* 32-bit pixels start with a pad byte, which is not alpha. */
                const uint8_t *p = row + x * (depth / 8u) + (depth == 32);
                unsigned r = type == TYPE_RGB ? 0 : 2;
                rgba[0] = p[r]; rgba[1] = p[1]; rgba[2] = p[2u - r];
            } else {
                unsigned index = depth == 8 ? row[x] :
                                 (row[x / 8u] >> (7u - x % 8u)) & 1u;
                if (map != NULL) {
                    if (index >= entries) {
                        result = CODEC_INVALID;
                        goto done;
                    }
                    rgba[0] = map[index];
                    rgba[1] = map[entries + index];
                    rgba[2] = map[entries * 2u + index];
                } else {
                    /* Without a colormap, 1-bit is 0 white, 1 black; 8-bit is gray. */
                    uint8_t gray = depth == 8 ? (uint8_t)index : index ? 0 : 255;
                    rgba[0] = rgba[1] = rgba[2] = gray;
                }
            }
            rgba[3] = 255;
        }
    }
done:
    free(unpacked);
    if (result != CODEC_OK)
        sunraster_free(image);
    return result;
}
