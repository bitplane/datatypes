#include "decode.h"
#include "common/atarist.h"
#include <stdlib.h>
#include <string.h>

/* Magic-less formats are recognised by their exact file size. */
#define TT_LOW_SIZE 154114u    /* resolution, 256 colours, 153600 bytes */
#define TT_MEDIUM_SIZE 153634u /* resolution, 16 colours, 153600 bytes */
#define TT_HIGH_SIZE 153606u   /* resolution, 2 colours, 153600 bytes */
#define DEGAS_ELITE_EXTRA 32u  /* animation data DEGAS Elite appends */
#define FTC_SIZE 184320u       /* 384x240 true colour */

typedef uint8_t palette_t[256][3];

static unsigned be16(const uint8_t *p)
{
    return ((unsigned)p[0] << 8) | p[1];
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

void falcon_free(struct falcon_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
    image->width = image->height = 0;
}

static int degas_size(size_t length, size_t size)
{
    return length == size || length == size + DEGAS_ELITE_EXTRA;
}

enum falcon_format falcon_identify(const uint8_t *data, size_t length)
{
    if (data == NULL)
        return FALCON_UNKNOWN;
    if (length >= 4) {
        if (memcmp(data, "PNT\0", 4) == 0)
            return FALCON_PRISM;
        if (memcmp(data, "DGU\1", 4) == 0)
            return FALCON_DUNE;
        if (memcmp(data, "DGC", 3) == 0)
            return FALCON_DUNE_PACKED;
        /* Spooky Sprites writes the same format with its own ID. */
        if (memcmp(data, "TRUP", 4) == 0 || memcmp(data, "tru?", 4) == 0)
            return FALCON_EGG;
        if (memcmp(data, "Indy", 4) == 0)
            return FALCON_INDY;
    }
    if (length >= 12 && memcmp(data, "COKE format.", 12) == 0)
        return FALCON_COKE;
    if (length >= 8 && memcmp(data, "TRUECOLR", 8) == 0)
        return FALCON_REMBRANDT;
    /* TT resolutions in DEGAS files: the resolution word and the size. */
    if (length >= 2 && data[0] == 0) {
        if (data[1] == 7 && degas_size(length, TT_LOW_SIZE))
            return FALCON_TT_LOW;
        if (data[1] == 4 && degas_size(length, TT_MEDIUM_SIZE))
            return FALCON_TT_MEDIUM;
        if (data[1] == 6 && degas_size(length, TT_HIGH_SIZE))
            return FALCON_TT_HIGH;
    }
    /* A 1024-byte palette and a 320x200, 320x240 or 640x480 screen. */
    if (length == 65024u || length == 77824u || length == 308224u)
        return FALCON_FUCKPAINT;
    if (length == FTC_SIZE)
        return FALCON_FTC;
    /* GodPaint's ID word varies and the program ignores it, so only the
       size tells. It comes after the fixed sizes, which as GodPaint files
       would need odd pixel counts; the ones seen are screen sized. */
    if (length > 6) {
        uint64_t pixels = (uint64_t)be16(data + 2) * be16(data + 4);
        if (pixels != 0 && 6u + pixels * 2u == length)
            return FALCON_GOD;
    }
    return FALCON_UNKNOWN;
}

static size_t padded_width(unsigned width)
{
    return ((size_t)width + 15u) & ~(size_t)15u;
}

static enum codec_result dimensions(unsigned width, unsigned height)
{
    if (width == 0 || height == 0)
        return CODEC_INVALID;
    if (width > FALCON_MAX_SIDE || height > FALCON_MAX_SIDE ||
        (uint64_t)width * height > FALCON_MAX_PIXELS)
        return CODEC_TOO_LARGE;
    return CODEC_OK;
}

static enum codec_result allocate(struct falcon_image *image,
                                  unsigned width, unsigned height)
{
    enum codec_result result = dimensions(width, height);

    if (result != CODEC_OK)
        return result;
    image->rgba = malloc((size_t)width * height * 4u);
    if (image->rgba == NULL)
        return CODEC_NO_MEMORY;
    image->width = width;
    image->height = height;
    return CODEC_OK;
}

static void put(uint8_t *dst, const uint8_t *rgb)
{
    dst[0] = rgb[0];
    dst[1] = rgb[1];
    dst[2] = rgb[2];
    dst[3] = 255;
}

/* A Falcon high-colour word, RRRRRGGG GGGBBBBB, with its top bits
   repeated into the low ones. */
static void put565(uint8_t *dst, const uint8_t *p)
{
    unsigned r = p[0] >> 3, g = ((p[0] & 7u) << 3) | (p[1] >> 5), b = p[1] & 31u;
    dst[0] = (uint8_t)((r << 3) | (r >> 2));
    dst[1] = (uint8_t)((g << 2) | (g >> 4));
    dst[2] = (uint8_t)((b << 3) | (b >> 2));
    dst[3] = 255;
}

/* Rows of true colour words, stride bytes apart. */
static enum codec_result true_colour(const uint8_t *data, size_t length,
                                     size_t offset, unsigned width,
                                     unsigned height, struct falcon_image *image)
{
    enum codec_result result = dimensions(width, height);
    size_t i;

    if (result != CODEC_OK)
        return result;
    if (offset > length || length - offset < (size_t)width * height * 2u)
        return CODEC_TRUNCATED;
    result = allocate(image, width, height);
    if (result != CODEC_OK)
        return result;
    for (i = 0; i < (size_t)width * height; i++)
        put565(image->rgba + i * 4u, data + offset + i * 2u);
    return CODEC_OK;
}

/* Interleaved bitplanes: each 16 pixels are one word from each plane. */
static void planar(struct falcon_image *image, const uint8_t *bitmap,
                   size_t stride, unsigned planes, const palette_t palette)
{
    unsigned x, y;

    for (y = 0; y < image->height; y++) {
        const uint8_t *line = bitmap + (size_t)y * stride;
        uint8_t *dst = image->rgba + (size_t)y * image->width * 4u;
        for (x = 0; x < image->width; x++, dst += 4)
            put(dst, palette[st_pixel(line, planes, x)]);
    }
}

/* Falcon palette registers: RRRRRRxx GGGGGGxx 00000000 BBBBBBxx. */
static void falcon_palette(const uint8_t *p, palette_t palette)
{
    unsigned i, c;

    for (i = 0; i < 256; i++)
        for (c = 0; c < 3; c++) {
            unsigned v = p[i * 4u + (c == 2 ? 3u : c)] & 0xfcu;
            palette[i][c] = (uint8_t)(v | v >> 6);
        }
}

/* TT palette registers: xxxxRRRR GGGGBBBB. */
static void tt_palette(const uint8_t *p, unsigned colours, palette_t palette)
{
    unsigned i;

    memset(palette, 0, sizeof(palette_t));
    for (i = 0; i < colours; i++) {
        unsigned c = be16(p + i * 2u);
        palette[i][0] = (uint8_t)(((c >> 8) & 15u) * 17u);
        palette[i][1] = (uint8_t)(((c >> 4) & 15u) * 17u);
        palette[i][2] = (uint8_t)((c & 15u) * 17u);
    }
}

/* 8 interleaved planes with a Falcon palette at palette_at. */
static enum codec_result falcon_planar(const uint8_t *data, size_t length,
                                       size_t palette_at, size_t bitmap_at,
                                       unsigned width, unsigned height,
                                       struct falcon_image *image)
{
    palette_t palette;
    size_t stride = padded_width(width);
    enum codec_result result = dimensions(width, height);

    if (result != CODEC_OK)
        return result;
    if (length < bitmap_at || length - bitmap_at < stride * height ||
        length < palette_at + 1024u)
        return CODEC_TRUNCATED;
    result = allocate(image, width, height);
    if (result != CODEC_OK)
        return result;
    falcon_palette(data + palette_at, palette);
    planar(image, data + bitmap_at, stride, 8, palette);
    return CODEC_OK;
}

/* DuneGraph DC1 methods 1 to 3: runs of bytes, words or longs, each a
   count less one then the value, filling plane 0 of every group, then
   plane 1, and so on. DuneGraph stops when only zeros are left, so the
   stream ending early leaves the rest zero. */
static void dune_unpack(const uint8_t *data, size_t end, unsigned method,
                        uint8_t *bitmap, size_t groups)
{
    size_t at = 1038, value_at = 0, left = 0, g;
    unsigned size = 1u << (method - 1u), count_size = method == 1 ? 1u : 2u;
    unsigned p, b, phase = 0;

    memset(bitmap, 0, groups * 16u);
    for (p = 0; p < 8; p++)
        for (g = 0; g < groups; g++)
            for (b = 0; b < 2; b++) {
                if (left == 0) {
                    if (end - at < count_size + size)
                        return;
                    left = ((size_t)(count_size == 1 ? data[at] : be16(data + at)) + 1u) * size;
                    value_at = at + count_size;
                    at = value_at + size;
                    phase = 0;
                }
                bitmap[g * 16u + p * 2u + b] = data[value_at + phase];
                phase = (phase + 1u) & (size - 1u);
                left--;
            }
    /* A run past the last plane is clamped. */
}

static enum codec_result dune_packed(const uint8_t *data, size_t length,
                                     struct falcon_image *image)
{
    palette_t palette;
    unsigned width, height, method;
    uint32_t packed;
    size_t stride;
    uint8_t *bitmap;
    enum codec_result result;

    if (length < 1034)
        return CODEC_TRUNCATED;
    method = data[3];
    width = be16(data + 4);
    height = be16(data + 6);
    if (method == 0)
        return falcon_planar(data, length, 10, 1034, width, height, image);
    if (method > 3)
        return CODEC_INVALID;
    result = dimensions(width, height);
    if (result != CODEC_OK)
        return result;
    /* The packed size, counting itself, bounds the runs. */
    if (length < 1038)
        return CODEC_TRUNCATED;
    packed = be32(data + 1034);
    if (packed < 4)
        return CODEC_INVALID;
    if (packed > length - 1034)
        return CODEC_TRUNCATED;
    stride = padded_width(width);
    bitmap = malloc(stride * height);
    if (bitmap == NULL)
        return CODEC_NO_MEMORY;
    dune_unpack(data, 1034u + (size_t)packed, method, bitmap, stride / 16u * height);
    result = allocate(image, width, height);
    if (result == CODEC_OK) {
        falcon_palette(data + 10, palette);
        planar(image, bitmap, stride, 8, palette);
    }
    free(bitmap);
    return result;
}

/* PackBits, where 128 is a no-op. */
struct packbits {
    const uint8_t *data;
    size_t at, length, left;
    int repeat;
};

static int packbits_next(struct packbits *s)
{
    while (s->left == 0) {
        unsigned n;
        if (s->at >= s->length)
            return -1;
        n = s->data[s->at++];
        if (n < 128) {
            s->left = n + 1u;
            s->repeat = 0;
        } else if (n > 128) {
            if (s->at >= s->length)
                return -1;
            s->left = 257u - n;
            s->repeat = 1;
        }
    }
    s->left--;
    if (s->repeat) {
        int v = s->data[s->at];
        if (s->left == 0)
            s->at++;
        return v;
    }
    if (s->at >= s->length)
        return -1;
    return s->data[s->at++];
}

/* VDI colour index to hardware pen. */
static unsigned vdi_pen(unsigned index, unsigned planes)
{
    static const uint8_t pens[16] = {
        0, 15, 1, 2, 4, 6, 3, 5, 7, 8, 9, 10, 12, 14, 11, 13
    };
    if (index == 1)
        return (1u << planes) - 1u;
    if (index < 16)
        return pens[index];
    return index == 255 ? 15u : index;
}

/* A VDI level, 0 to 1000. Some writers go slightly over. */
static uint8_t vdi_level(unsigned v)
{
    return v >= 1000u ? 255u : (uint8_t)((v * 255u + 500u) / 1000u);
}

static enum codec_result prism(const uint8_t *data, size_t length,
                               struct falcon_image *image)
{
    palette_t palette;
    unsigned colours, width, height, bpp, compression, x, y;
    size_t palette_at = 128, bitmap_at, stride, total;
    uint8_t *unpacked = NULL;
    const uint8_t *bitmap;
    enum codec_result result;

    if (length < 128)
        return CODEC_TRUNCATED;
    colours = be16(data + 6);
    width = be16(data + 8);
    height = be16(data + 10);
    bpp = be16(data + 12);
    compression = be16(data + 14);
    if (be16(data + 4) != 0x0100 || compression > 1 ||
        !(bpp == 1 || bpp == 2 || bpp == 4 || bpp == 8 || bpp == 16 || bpp == 24))
        return CODEC_INVALID;
    result = dimensions(width, height);
    if (result != CODEC_OK)
        return result;
    /* True colour pictures may carry a palette too; it is skipped. */
    bitmap_at = palette_at + (size_t)colours * 6u;
    if (length < bitmap_at)
        return CODEC_TRUNCATED;
    /* Lines are a whole number of 16-pixel groups at every depth. */
    stride = padded_width(width) * bpp / 8u;
    total = stride * height;
    if (compression == 0) {
        /* The size field at 16 is the packed size in packed files, so
           neither kind relies on it. */
        if (length - bitmap_at < total)
            return CODEC_TRUNCATED;
        bitmap = data + bitmap_at;
    } else {
        /* Each line packs plane by plane, true colour as if every word
           were a plane: reorder to interleaved groups while unpacking. */
        struct packbits s;
        size_t words = padded_width(width) / 16u, k;
        unsigned p, b;
        s.data = data;
        s.at = bitmap_at;
        s.length = length;
        s.left = 0;
        s.repeat = 0;
        unpacked = malloc(total);
        if (unpacked == NULL)
            return CODEC_NO_MEMORY;
        for (y = 0; y < height; y++)
            for (p = 0; p < bpp; p++)
                for (k = 0; k < words; k++)
                    for (b = 0; b < 2; b++) {
                        int v = packbits_next(&s);
                        if (v < 0) {
                            free(unpacked);
                            return CODEC_TRUNCATED;
                        }
                        unpacked[(size_t)y * stride + (k * bpp + p) * 2u + b] = (uint8_t)v;
                    }
        bitmap = unpacked;
    }
    result = allocate(image, width, height);
    if (result != CODEC_OK) {
        free(unpacked);
        return result;
    }
    if (bpp <= 8) {
        unsigned used = 1u << bpp, i;
        memset(palette, 0, sizeof palette);
        /* Without a palette, pen 0 is the white background. */
        memset(palette[0], 255, 3);
        for (i = 0; i < colours && i < used; i++) {
            unsigned pen = vdi_pen(i, bpp);
            const uint8_t *c = data + palette_at + (size_t)i * 6u;
            if (pen >= used)
                continue;
            palette[pen][0] = vdi_level(be16(c));
            palette[pen][1] = vdi_level(be16(c + 2));
            palette[pen][2] = vdi_level(be16(c + 4));
        }
        planar(image, bitmap, stride, bpp, palette);
    } else {
        for (y = 0; y < height; y++) {
            const uint8_t *line = bitmap + (size_t)y * stride;
            uint8_t *dst = image->rgba + (size_t)y * width * 4u;
            for (x = 0; x < width; x++, dst += 4) {
                if (bpp == 16) {
                    put565(dst, line + (size_t)x * 2u);
                } else {
                    put(dst, line + (size_t)x * 3u);
                }
            }
        }
    }
    free(unpacked);
    return CODEC_OK;
}

/* Rembrandt: a main header, then a header and true colour data for each
   picture. */
static enum codec_result rembrandt(const uint8_t *data, size_t length,
                                   unsigned index, struct falcon_image *image,
                                   unsigned *count)
{
    size_t at;
    unsigned pictures, i;

    if (length < 18)
        return CODEC_TRUNCATED;
    at = be16(data + 12);
    pictures = be16(data + 16);
    if (at < 18 || pictures == 0)
        return CODEC_INVALID;
    if (count != NULL)
        *count = pictures;
    if (index >= pictures)
        return CODEC_INVALID;
    for (i = 0;; i++) {
        size_t header, size;
        if (at > length || length - at < 20)
            return CODEC_TRUNCATED;
        header = be16(data + at + 8);
        if (memcmp(data + at, "PICT", 4) != 0 || header < 20)
            return CODEC_INVALID;
        if (i == index) {
            /* Compression was never implemented. */
            if (data[at + 18] != 0)
                return CODEC_INVALID;
            return true_colour(data, length, at + header, be16(data + at + 10),
                               be16(data + at + 12), image);
        }
        size = be32(data + at + 4);
        if (length - at < header || length - at - header < size)
            return CODEC_TRUNCATED;
        at += header + size;
    }
}

enum codec_result falcon_decode(const uint8_t *data, size_t length,
                                unsigned index, struct falcon_image *image,
                                unsigned *count)
{
    palette_t palette;
    enum falcon_format format;
    enum codec_result result;

    if (count != NULL)
        *count = 0;
    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->rgba = NULL;
    image->format = FALCON_UNKNOWN;
    if (data == NULL || length < 4)
        return CODEC_TRUNCATED;
    format = falcon_identify(data, length);
    if (format == FALCON_UNKNOWN)
        return CODEC_INVALID;
    image->format = format;
    if (format == FALCON_REMBRANDT)
        return rembrandt(data, length, index, image, count);
    if (count != NULL)
        *count = 1;
    if (index != 0)
        return CODEC_INVALID;
    switch (format) {
    case FALCON_PRISM:
        return prism(data, length, image);
    case FALCON_DUNE:
        if (length < 8)
            return CODEC_TRUNCATED;
        return falcon_planar(data, length, 8, 1032, be16(data + 4),
                             be16(data + 6), image);
    case FALCON_DUNE_PACKED:
        return dune_packed(data, length, image);
    case FALCON_EGG:
        if (length < 8)
            return CODEC_TRUNCATED;
        return true_colour(data, length, 8, be16(data + 4), be16(data + 6), image);
    case FALCON_INDY:
        if (length < 8)
            return CODEC_TRUNCATED;
        return true_colour(data, length, 256, be16(data + 4), be16(data + 6), image);
    case FALCON_COKE:
        if (length < 18)
            return CODEC_TRUNCATED;
        /* The data offset is 18 in version 1.00. */
        if (be16(data + 16) < 18)
            return CODEC_INVALID;
        return true_colour(data, length, be16(data + 16), be16(data + 12),
                           be16(data + 14), image);
    case FALCON_TT_LOW:
        result = allocate(image, 320, 480);
        if (result == CODEC_OK) {
            tt_palette(data + 2, 256, palette);
            planar(image, data + 514, 320, 8, palette);
        }
        return result;
    case FALCON_TT_MEDIUM:
        result = allocate(image, 640, 480);
        if (result == CODEC_OK) {
            tt_palette(data + 2, 16, palette);
            planar(image, data + 34, 320, 4, palette);
        }
        return result;
    case FALCON_TT_HIGH:
        /* Monochrome: the two words after the resolution don't matter. */
        result = allocate(image, 1280, 960);
        if (result == CODEC_OK) {
            memset(palette, 0, sizeof palette);
            memset(palette[0], 255, 3);
            planar(image, data + 6, 160, 1, palette);
        }
        return result;
    case FALCON_FUCKPAINT:
        if (length == 65024u)
            return falcon_planar(data, length, 0, 1024, 320, 200, image);
        if (length == 77824u)
            return falcon_planar(data, length, 0, 1024, 320, 240, image);
        return falcon_planar(data, length, 0, 1024, 640, 480, image);
    case FALCON_FTC:
        return true_colour(data, length, 0, 384, 240, image);
    case FALCON_GOD:
        return true_colour(data, length, 6, be16(data + 2), be16(data + 4), image);
    default:
        return CODEC_INVALID;
    }
}
