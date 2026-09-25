#include <stdlib.h>
#include <string.h>

#include "common/bcn.h"
#include "decode.h"

#define MAX_SIDE 65535u
#define MAX_PIXELS (16ul * 1024ul * 1024ul)
#define LEVELS 16u
#define BLP1_HEADER 156u
#define BLP2_HEADER 148u
#define PALETTE_SIZE 1024u

enum encoding { PALETTE, DXT1, DXT3, DXT5, BGRA };

struct header {
    enum encoding encoding;
    unsigned alpha_bits, width, height, levels;
    const uint8_t *palette;
    const uint8_t *offsets, *sizes;
    int version;
};

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
           (uint32_t)p[3] << 24;
}

static unsigned side(unsigned full, unsigned level)
{
    return (full >> level) ? full >> level : 1u;
}

static int valid_alpha_bits(uint32_t bits)
{
    return bits == 0 || bits == 1 || bits == 4 || bits == 8;
}

static enum codec_result read_header(const uint8_t *data, size_t length, struct header *h)
{
    uint32_t mips;

    if (length < 4)
        return CODEC_TRUNCATED;
    if (memcmp(data, "BLP1", 4) == 0) {
        if (length < BLP1_HEADER)
            return CODEC_TRUNCATED;
        /* Compression 0 is JPEG, which needs a JPEG decoder. */
        if (get32(data + 4) != 1 || !valid_alpha_bits(get32(data + 8)))
            return CODEC_INVALID;
        if (length < BLP1_HEADER + PALETTE_SIZE)
            return CODEC_TRUNCATED;
        h->version = 1;
        h->encoding = PALETTE;
        h->alpha_bits = get32(data + 8);
        h->width = get32(data + 12);
        h->height = get32(data + 16);
        /* data + 20 is a picture type that nothing depends on. */
        mips = get32(data + 24);
        h->offsets = data + 28;
        h->sizes = data + 92;
        h->palette = data + BLP1_HEADER;
    } else if (memcmp(data, "BLP2", 4) == 0) {
        if (length < BLP2_HEADER)
            return CODEC_TRUNCATED;
        /* Type 0 is JPEG. */
        if (get32(data + 4) != 1 || !valid_alpha_bits(data[9]))
            return CODEC_INVALID;
        switch (data[8]) {
        case 1: h->encoding = PALETTE; break;
        case 2:
            switch (data[10]) {
            case 0: h->encoding = DXT1; break;
            case 1: h->encoding = DXT3; break;
            case 7: h->encoding = DXT5; break;
            default: return CODEC_INVALID;
            }
            break;
        case 3: h->encoding = BGRA; break;
        default: return CODEC_INVALID;
        }
        if (length < BLP2_HEADER + PALETTE_SIZE)
            return CODEC_TRUNCATED;
        h->version = 2;
        h->alpha_bits = data[9];
        h->width = get32(data + 12);
        h->height = get32(data + 16);
        mips = data[11];
        h->offsets = data + 20;
        h->sizes = data + 84;
        h->palette = data + BLP2_HEADER;
    } else {
        /* BLP0 keeps its mip levels in separate files. */
        return CODEC_INVALID;
    }
    if (h->width == 0 || h->height == 0)
        return CODEC_INVALID;
    if (h->width > MAX_SIDE || h->height > MAX_SIDE ||
        (unsigned long)h->width * h->height > MAX_PIXELS)
        return CODEC_TOO_LARGE;
    if (get32(h->offsets) == 0)
        return CODEC_INVALID;
    /* Levels stop at 1x1 or at the first empty directory entry. */
    h->levels = 1;
    while (mips != 0 && h->levels < LEVELS &&
           (side(h->width, h->levels - 1u) > 1u || side(h->height, h->levels - 1u) > 1u) &&
           get32(h->offsets + h->levels * 4u) != 0 && get32(h->sizes + h->levels * 4u) != 0)
        h->levels++;
    return CODEC_OK;
}

/* Pillow's writer declares alpha in palette images but stores it in the
   palette instead of after the indices; the level size then covers the
   indices only. */
static int palette_alpha(const struct header *h, unsigned level, unsigned long pixels)
{
    return h->encoding == PALETTE && h->alpha_bits != 0 &&
           get32(h->sizes + level * 4u) == pixels;
}

static unsigned long level_bytes(const struct header *h, unsigned level)
{
    unsigned long w = side(h->width, level), hh = side(h->height, level);
    unsigned long pixels = w * hh, blocks = ((w + 3u) / 4u) * ((hh + 3u) / 4u);

    switch (h->encoding) {
    case PALETTE:
        if (palette_alpha(h, level, pixels))
            return pixels;
        return pixels + (pixels * h->alpha_bits + 7u) / 8u;
    case DXT1: return blocks * 8u;
    case DXT3:
    case DXT5: return blocks * 16u;
    default: return pixels * 4u;
    }
}

/* Pillow's writer always gives the first level BLP2's offset, 1172, which
   in BLP1 is inside the palette: its data starts after the palette. */
static unsigned long level_offset(const struct header *h, unsigned level)
{
    unsigned long offset = get32(h->offsets + level * 4u);
    if (h->version == 1 && level == 0 && offset == BLP2_HEADER + PALETTE_SIZE)
        return BLP1_HEADER + PALETTE_SIZE;
    return offset;
}

static int level_present(const struct header *h, size_t length, unsigned level)
{
    unsigned long offset = level_offset(h, level);
    return offset <= length && level_bytes(h, level) <= length - offset;
}

enum codec_result blp_count(const uint8_t *data, size_t length, unsigned long *count)
{
    struct header h;
    enum codec_result result = read_header(data, length, &h);
    unsigned level = 0;

    *count = 0;
    if (result != CODEC_OK)
        return result;
    while (level < h.levels && level_present(&h, length, level))
        level++;
    *count = level;
    return CODEC_OK;
}

static uint8_t alpha_at(const uint8_t *alpha, unsigned bits, unsigned long i)
{
    switch (bits) {
    case 1: return (alpha[i / 8u] >> (i % 8u)) & 1u ? 255 : 0;
    case 4: return (uint8_t)(((alpha[i / 2u] >> (i % 2u * 4u)) & 15u) * 17u);
    case 8: return alpha[i];
    default: return 255;
    }
}

static void decode_palette(const struct header *h, const uint8_t *p, unsigned long pixels,
                           int in_palette, uint8_t *rgba)
{
    const uint8_t *alpha = p + pixels;
    unsigned long i;

    for (i = 0; i < pixels; i++) {
        const uint8_t *c = h->palette + p[i] * 4u;
        rgba[i * 4u] = c[2];
        rgba[i * 4u + 1u] = c[1];
        rgba[i * 4u + 2u] = c[0];
        rgba[i * 4u + 3u] = in_palette ? c[3] : alpha_at(alpha, h->alpha_bits, i);
    }
}

static void decode_blocks(const struct header *h, const uint8_t *p, uint8_t *rgba,
                          unsigned width, unsigned height)
{
    unsigned bx, by, x, y;
    uint8_t block[64];

    for (by = 0; by < height; by += 4) {
        for (bx = 0; bx < width; bx += 4) {
            switch (h->encoding) {
            case DXT1: bc1_block(p, block, h->alpha_bits != 0); p += 8; break;
            case DXT3: bc2_block(p, block); p += 16; break;
            default: bc3_block(p, block); p += 16; break;
            }
            for (y = 0; y < 4 && by + y < height; y++)
                for (x = 0; x < 4 && bx + x < width; x++)
                    memcpy(rgba + ((unsigned long)(by + y) * width + bx + x) * 4u,
                           block + (y * 4u + x) * 4u, 4);
        }
    }
    if (h->alpha_bits == 0)
        for (x = 0; x < width * height; x++)
            rgba[x * 4u + 3u] = 255;
}

static void decode_bgra(const struct header *h, const uint8_t *p, unsigned long pixels,
                        uint8_t *rgba)
{
    unsigned long i;

    for (i = 0; i < pixels; i++, p += 4) {
        rgba[i * 4u] = p[2];
        rgba[i * 4u + 1u] = p[1];
        rgba[i * 4u + 2u] = p[0];
        rgba[i * 4u + 3u] = h->alpha_bits ? p[3] : 255;
    }
}

enum codec_result blp_decode(const uint8_t *data, size_t length, unsigned long index,
                             struct blp_image *image)
{
    struct header h;
    enum codec_result result = read_header(data, length, &h);
    unsigned w, hh;
    unsigned long pixels;
    const uint8_t *p;

    image->rgba = NULL;
    if (result != CODEC_OK)
        return result;
    if (index >= h.levels)
        return CODEC_INVALID;
    if (!level_present(&h, length, (unsigned)index))
        return CODEC_TRUNCATED;
    w = side(h.width, (unsigned)index);
    hh = side(h.height, (unsigned)index);
    pixels = (unsigned long)w * hh;
    image->rgba = malloc(pixels * 4u);
    if (image->rgba == NULL)
        return CODEC_NO_MEMORY;
    p = data + level_offset(&h, (unsigned)index);
    switch (h.encoding) {
    case PALETTE:
        decode_palette(&h, p, pixels, palette_alpha(&h, (unsigned)index, pixels), image->rgba);
        break;
    case BGRA: decode_bgra(&h, p, pixels, image->rgba); break;
    default: decode_blocks(&h, p, image->rgba, w, hh); break;
    }
    image->width = w;
    image->height = hh;
    return CODEC_OK;
}

void blp_free(struct blp_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
}
