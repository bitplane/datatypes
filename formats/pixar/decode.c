#include "decode.h"
#include <stdlib.h>

#define PIXAR_HEADER 512u
#define PIXAR_MAX_PIXELS (16u * 1024u * 1024u)

/* Channel bits of the picture format; channels are stored in R, G, B, A order. */
enum { PF_A = 1, PF_B = 2, PF_G = 4, PF_R = 8 };
enum { STORAGE_8BIT_ENCODED, STORAGE_12BIT_ENCODED, STORAGE_8BIT_DUMPED,
       STORAGE_12BIT_DUMPED };
enum { MATTED_TO_BLACK, UNASSOCIATED };
enum { END_OF_BLOCK, FULL_DUMP, FULL_RUN, PARTIAL_DUMP, PARTIAL_RUN };

struct tile {
    struct pixar_image *image;
    unsigned x0, y0, width, channels;
    size_t index, total;
};

static unsigned le16(const uint8_t *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8;
}

static unsigned long le32(const uint8_t *p)
{
    return (unsigned long)le16(p) | (unsigned long)le16(p + 2) << 16;
}

/* Store the next pixel of the tile. Edge tiles are stored full size, so pixels
   past the picture are dropped, and so is anything past the end of the tile. */
static void put(struct tile *t, const uint8_t *v, unsigned alpha)
{
    unsigned x, y;
    uint8_t *rgba;
    if (t->index == t->total)
        return;
    x = t->x0 + (unsigned)(t->index % t->width);
    y = t->y0 + (unsigned)(t->index / t->width);
    t->index++;
    if (x >= t->image->width || y >= t->image->height)
        return;
    rgba = t->image->rgba + ((size_t)y * t->image->width + x) * 4u;
    switch (t->channels) {
    case 1: rgba[0] = rgba[1] = rgba[2] = v[0]; break;
    case 2: rgba[0] = rgba[1] = rgba[2] = v[0]; rgba[3] = v[1]; break;
    default:
        rgba[0] = v[0]; rgba[1] = v[1]; rgba[2] = v[2];
        rgba[3] = (uint8_t)(t->channels == 4 ? alpha : 255u);
        break;
    }
}

static enum codec_result dumped(struct tile *t, const uint8_t *data, size_t length)
{
    if (length / t->channels < t->total)
        return CODEC_TRUNCATED;
    while (t->index < t->total) {
        const uint8_t *p = data + t->index * t->channels;
        put(t, p, t->channels == 4 ? p[3] : 255u);
    }
    return CODEC_OK;
}

/* Packets are a little-endian word, flag in the top 4 bits and count - 1 in the
   rest, then pixel data. Flag 0 pads out the rest of a disk block, measured
   from the start of the tile. */
static enum codec_result encoded(struct tile *t, const uint8_t *data, size_t length,
                                 unsigned blocking)
{
    size_t pos = 0, need, i;
    unsigned word, flag, count, alpha, n, rgb;
    const uint8_t *p;

    while (t->index < t->total) {
        /* Writers start a new block when a packet header won't fit. */
        if (blocking != 0 && blocking - pos % blocking < 2u)
            pos += blocking - pos % blocking;
        if (pos > length || length - pos < 2u)
            return CODEC_TRUNCATED;
        word = le16(data + pos);
        flag = word >> 12;
        count = (word & 0xfffu) + 1u;
        if (flag == END_OF_BLOCK) {
            if (blocking == 0)
                return CODEC_INVALID;
            pos += blocking - pos % blocking;
            continue;
        }
        pos += 2;
        n = t->channels;
        switch (flag) {
        case FULL_DUMP: need = (size_t)count * n; break;
        case FULL_RUN: need = (size_t)count * (n + 1u); break;
        case PARTIAL_DUMP: need = 1u + (size_t)count * 3u; break;
        case PARTIAL_RUN: need = 1u + (size_t)count * 4u; break;
        default: return CODEC_INVALID;
        }
        /* Partial packets share one alpha across RGB pixels. */
        if ((flag == PARTIAL_DUMP || flag == PARTIAL_RUN) && n != 4)
            return CODEC_INVALID;
        if (length - pos < need)
            return CODEC_TRUNCATED;
        p = data + pos;
        pos += need;
        alpha = 255;
        if (flag == PARTIAL_DUMP || flag == PARTIAL_RUN)
            alpha = *p++;
        rgb = flag == PARTIAL_DUMP || flag == PARTIAL_RUN;
        for (i = 0; i < count; i++) {
            unsigned run = 1, size = rgb ? 3u : n;
            if (flag == FULL_RUN || flag == PARTIAL_RUN)
                run = *p++ + 1u;
            if (!rgb && n == 4)
                alpha = p[3];
            while (run-- != 0 && t->index < t->total)
                put(t, p, alpha);
            p += size;
        }
    }
    return CODEC_OK;
}

void pixar_free(struct pixar_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
    image->width = image->height = 0;
}

enum codec_result pixar_decode(const uint8_t *data, size_t length,
                               struct pixar_image *image)
{
    unsigned width, height, tile_width, tile_height, format, storage;
    unsigned blocking, alpha_mode, channels, across, down, tx, ty;
    size_t pixels, tiles, i;
    enum codec_result result = CODEC_OK;

    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->rgba = NULL;
    if (data == NULL || length < PIXAR_HEADER)
        return CODEC_TRUNCATED;
    if (data[0] != 0x80 || data[1] != 0xe8 || data[2] != 0 || data[3] != 0)
        return CODEC_INVALID;
    height = le16(data + 416); width = le16(data + 418);
    tile_height = le16(data + 420); tile_width = le16(data + 422);
    format = le16(data + 424); storage = le16(data + 426);
    blocking = le16(data + 428); alpha_mode = le16(data + 430);

    switch (format) {
    case PF_R: case PF_G: case PF_B: case PF_A: channels = 1; break;
    case PF_R | PF_A: channels = 2; break;
    case PF_R | PF_G | PF_B: channels = 3; break;
    case PF_R | PF_G | PF_B | PF_A: channels = 4; break;
    default: return CODEC_INVALID;
    }
    /* 12-bit samples are fixed point with 1.0 at 2048 and room above it. */
    if (storage != STORAGE_8BIT_ENCODED && storage != STORAGE_8BIT_DUMPED)
        return CODEC_INVALID;
    if (width == 0 || height == 0 || tile_width == 0 || tile_height == 0)
        return CODEC_INVALID;
    pixels = (size_t)width * height;
    if (pixels > PIXAR_MAX_PIXELS ||
        (size_t)tile_width * tile_height > PIXAR_MAX_PIXELS)
        return CODEC_TOO_LARGE;
    across = (width - 1u) / tile_width + 1u;
    down = (height - 1u) / tile_height + 1u;
    tiles = (size_t)across * down;
    if ((length - PIXAR_HEADER) / 8u < tiles)
        return CODEC_TRUNCATED;

    image->rgba = malloc(pixels * 4u);
    if (image->rgba == NULL)
        return CODEC_NO_MEMORY;
    image->width = width; image->height = height;
    /* Missing tiles show as black, or as transparent when there is alpha. */
    for (i = 0; i < pixels; i++) {
        uint8_t *rgba = image->rgba + i * 4u;
        rgba[0] = rgba[1] = rgba[2] = 0;
        rgba[3] = (uint8_t)(format & PF_A && channels > 1 ? 0 : 255);
    }

    for (ty = 0; ty < down && result == CODEC_OK; ty++) {
        for (tx = 0; tx < across && result == CODEC_OK; tx++) {
            const uint8_t *entry = data + PIXAR_HEADER + ((size_t)ty * across + tx) * 8u;
            unsigned long offset = le32(entry);
            struct tile t;
            /* A zero pointer is a null tile. The length is ignored: -1 marks
               an incomplete tile, and the data is bounded by the file. */
            if (offset == 0)
                continue;
            if (offset >= length) {
                result = CODEC_TRUNCATED;
                break;
            }
            t.image = image;
            t.x0 = tx * tile_width; t.y0 = ty * tile_height;
            t.width = tile_width; t.channels = channels;
            t.index = 0; t.total = (size_t)tile_width * tile_height;
            if (storage == STORAGE_8BIT_DUMPED)
                result = dumped(&t, data + offset, length - offset);
            else
                result = encoded(&t, data + offset, length - offset, blocking);
        }
    }
    if (result != CODEC_OK) {
        pixar_free(image);
        return result;
    }

    /* Matted-to-black alpha is premultiplied; unpremultiply it. */
    if (channels > 1 && format & PF_A && alpha_mode == MATTED_TO_BLACK) {
        for (i = 0; i < pixels; i++) {
            uint8_t *rgba = image->rgba + i * 4u;
            unsigned a = rgba[3], c;
            if (a == 255)
                continue;
            for (c = 0; c < 3; c++) {
                unsigned v = a == 0 ? 0 : (rgba[c] * 255u + a / 2u) / a;
                rgba[c] = (uint8_t)(v > 255u ? 255u : v);
            }
        }
    }
    return CODEC_OK;
}
