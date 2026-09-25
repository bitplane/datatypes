#include "decode.h"
#include "common/bcn.h"
#include <stdlib.h>
#include <string.h>

#define HEADER_SIZE 24u
#define ENTRY_SIZE 8u
#define MAX_SIDE 65535u
#define MAX_PIXELS (16ul * 1024ul * 1024ul)
/* A full mip chain of a 65535-pixel side. */
#define MAX_LEVELS 17u

struct level {
    unsigned format, width, height;
    const uint8_t *pixels;
};

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
           (uint32_t)p[3] << 24;
}

static int known(uint32_t format)
{
    return format == FTEX_DXT1 || format == FTEX_RGB;
}

/* Bytes the level's pixels need. */
static uint64_t level_bytes(unsigned format, unsigned w, unsigned h)
{
    if (format == FTEX_DXT1)
        return (uint64_t)((w + 3u) / 4u) * ((h + 3u) / 4u) * 8u;
    return (uint64_t)w * h * 3u;
}

static enum codec_result check_header(const uint8_t *data, size_t length,
                                      uint32_t *formats)
{
    uint32_t w, h;

    if (length < 4)
        return CODEC_TRUNCATED;
    if (memcmp(data, "FTEX", 4) != 0)
        return CODEC_INVALID;
    if (length < HEADER_SIZE)
        return CODEC_TRUNCATED;
    w = get32(data + 8);
    h = get32(data + 12);
    *formats = get32(data + 20);
    if (w == 0 || h == 0 || *formats == 0)
        return CODEC_INVALID;
    if (w > MAX_SIDE || h > MAX_SIDE || (uint64_t)w * h > MAX_PIXELS)
        return CODEC_TOO_LARGE;
    if ((uint64_t)*formats * ENTRY_SIZE > length - HEADER_SIZE)
        return CODEC_TRUNCATED;
    return CODEC_OK;
}

/* Walk to image index, counting images into *count on the way. With target
   NULL, walk the whole file and only count. */
static enum codec_result walk(const uint8_t *data, size_t length, unsigned long index,
                              struct level *target, unsigned long *count)
{
    enum codec_result result;
    uint32_t formats, levels, f, i, format, size;
    unsigned w, h;
    uint64_t at;

    *count = 0;
    result = check_header(data, length, &formats);
    if (result != CODEC_OK)
        return result;
    /* A file must hold at least the top level, whatever it declares. */
    levels = get32(data + 16);
    if (levels == 0)
        levels = 1;
    if (levels > MAX_LEVELS)
        levels = MAX_LEVELS;
    for (f = 0; f < formats; f++) {
        const uint8_t *entry = data + HEADER_SIZE + (size_t)f * ENTRY_SIZE;
        format = get32(entry);
        if (!known(format))
            continue;
        at = get32(entry + 4);
        w = (unsigned)get32(data + 8);
        h = (unsigned)get32(data + 12);
        for (i = 0; i < levels; i++) {
            if (at + 4u > length) {
                result = CODEC_TRUNCATED;
                break;
            }
            size = get32(data + at);
            if (size < level_bytes(format, w, h)) {
                result = CODEC_INVALID;
                break;
            }
            if (size > length - at - 4u) {
                result = CODEC_TRUNCATED;
                break;
            }
            if (target != NULL && *count == index) {
                target->format = format;
                target->width = w;
                target->height = h;
                target->pixels = data + at + 4u;
                return CODEC_OK;
            }
            ++*count;
            at += 4u + size;
            w = w > 1 ? w / 2u : 1u;
            h = h > 1 ? h / 2u : 1u;
        }
        /* A later level cut short only matters when it is the one asked for. */
        if (target != NULL && result != CODEC_OK && *count == index)
            return result;
        result = CODEC_OK;
    }
    return target != NULL ? CODEC_INVALID : CODEC_OK;
}

enum codec_result ftex_count(const uint8_t *data, size_t length, unsigned long *count)
{
    return walk(data, length, 0, NULL, count);
}

static void decode_dxt1(const struct level *l, uint8_t *rgba)
{
    unsigned bx, by, x, y, bw = (l->width + 3u) / 4u, bh = (l->height + 3u) / 4u;
    const uint8_t *in = l->pixels;
    uint8_t block[64];

    for (by = 0; by < bh; by++)
        for (bx = 0; bx < bw; bx++, in += 8) {
            bc1_block(in, block, 1);
            for (y = 0; y < 4 && by * 4u + y < l->height; y++)
                for (x = 0; x < 4 && bx * 4u + x < l->width; x++)
                    memcpy(rgba + ((size_t)(by * 4u + y) * l->width + bx * 4u + x) * 4u,
                           block + (y * 4u + x) * 4u, 4);
        }
}

static void decode_rgb(const struct level *l, uint8_t *rgba)
{
    size_t i, n = (size_t)l->width * l->height;
    const uint8_t *in = l->pixels;

    for (i = 0; i < n; i++, in += 3, rgba += 4) {
        rgba[0] = in[0];
        rgba[1] = in[1];
        rgba[2] = in[2];
        rgba[3] = 255;
    }
}

enum codec_result ftex_decode(const uint8_t *data, size_t length, unsigned long index,
                              struct ftex_image *image)
{
    struct level l;
    unsigned long seen;
    enum codec_result result;

    memset(image, 0, sizeof *image);
    result = walk(data, length, index, &l, &seen);
    if (result != CODEC_OK)
        return result;
    image->rgba = malloc((size_t)l.width * l.height * 4u);
    if (image->rgba == NULL)
        return CODEC_NO_MEMORY;
    image->width = l.width;
    image->height = l.height;
    if (l.format == FTEX_DXT1)
        decode_dxt1(&l, image->rgba);
    else
        decode_rgb(&l, image->rgba);
    return CODEC_OK;
}

void ftex_free(struct ftex_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
}
