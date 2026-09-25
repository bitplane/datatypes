#include <stdlib.h>
#include <string.h>

#include "decode.h"
#include "common/png.h"
#include "common/zlib.h"

enum format { MONO, PAL4, PAL8, RGB, PACKED };

struct type_info {
    char type[5];
    unsigned short width, height;
    unsigned char format, scale;
    char mask[5];
};

/* Classic icons pair with a 1-bit '#' mask, 24-bit ones with an 8-bit mask.
   PACKED entries hold PNG, JPEG 2000 or ARGB; icp4 and icp5 may hold 24-bit
   RLE instead. */
static const struct type_info types[] = {
    {"ICON", 32, 32, MONO, 1, ""},
    {"ICN#", 32, 32, MONO, 1, "ICN#"},
    {"icm#", 16, 12, MONO, 1, "icm#"},
    {"icm4", 16, 12, PAL4, 1, "icm#"},
    {"icm8", 16, 12, PAL8, 1, "icm#"},
    {"ics#", 16, 16, MONO, 1, "ics#"},
    {"ics4", 16, 16, PAL4, 1, "ics#"},
    {"ics8", 16, 16, PAL8, 1, "ics#"},
    {"icl4", 32, 32, PAL4, 1, "ICN#"},
    {"icl8", 32, 32, PAL8, 1, "ICN#"},
    {"ich#", 48, 48, MONO, 1, "ich#"},
    {"ich4", 48, 48, PAL4, 1, "ich#"},
    {"ich8", 48, 48, PAL8, 1, "ich#"},
    {"is32", 16, 16, RGB, 1, "s8mk"},
    {"il32", 32, 32, RGB, 1, "l8mk"},
    {"ih32", 48, 48, RGB, 1, "h8mk"},
    {"it32", 128, 128, RGB, 1, "t8mk"},
    {"icp4", 16, 16, PACKED, 1, "rgb"},
    {"icp5", 32, 32, PACKED, 1, "rgb"},
    {"icp6", 64, 64, PACKED, 1, ""},
    {"ic07", 128, 128, PACKED, 1, ""},
    {"ic08", 256, 256, PACKED, 1, ""},
    {"ic09", 512, 512, PACKED, 1, ""},
    {"ic10", 1024, 1024, PACKED, 2, ""},
    {"ic11", 32, 32, PACKED, 2, ""},
    {"ic12", 64, 64, PACKED, 2, ""},
    {"ic13", 256, 256, PACKED, 2, ""},
    {"ic14", 512, 512, PACKED, 2, ""},
    {"ic04", 16, 16, PACKED, 1, ""},
    {"ic05", 32, 32, PACKED, 1, ""},
    {"icsb", 18, 18, PACKED, 1, ""},
    {"icsB", 36, 36, PACKED, 2, ""},
    {"sb24", 24, 24, PACKED, 1, ""},
    {"SB24", 48, 48, PACKED, 2, ""}
};
#define TYPE_COUNT (sizeof types / sizeof types[0])

static const char mask_types[][5] = {
    "ICN#", "icm#", "ics#", "ich#", "s8mk", "l8mk", "h8mk", "t8mk"
};
#define MASK_COUNT (sizeof mask_types / sizeof mask_types[0])

/* The classic Mac OS 16-colour palette. */
static const uint8_t palette4[16][3] = {
    {0xff, 0xff, 0xff}, {0xfc, 0xf3, 0x05}, {0xff, 0x64, 0x02}, {0xdd, 0x08, 0x06},
    {0xf2, 0x08, 0x84}, {0x46, 0x00, 0xa5}, {0x00, 0x00, 0xd4}, {0x02, 0xab, 0xea},
    {0x1f, 0xb7, 0x14}, {0x00, 0x64, 0x11}, {0x56, 0x2c, 0x05}, {0x90, 0x71, 0x3a},
    {0xc0, 0xc0, 0xc0}, {0x80, 0x80, 0x80}, {0x40, 0x40, 0x40}, {0x00, 0x00, 0x00}
};

struct span { const uint8_t *data; size_t length; };

struct candidate {
    const struct type_info *info;
    struct span body;
    unsigned width, height, depth;
};

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static int is_jpeg2000(const struct span *s)
{
    static const uint8_t box[12] = {0, 0, 0, 12, 'j', 'P', ' ', ' ', 13, 10, 0x87, 10};
    static const uint8_t codestream[4] = {0xff, 0x4f, 0xff, 0x51};
    return (s->length >= 12 && memcmp(s->data, box, 12) == 0) ||
           (s->length >= 4 && memcmp(s->data, codestream, 4) == 0);
}

/* The classic Mac OS 256-colour palette: a 6x6x6 cube from white down, then
   ramps of red, green, blue and grey without the cube's levels, then black. */
static void palette8(unsigned index, uint8_t *rgb)
{
    static const uint8_t ramp[10] = {0xee, 0xdd, 0xbb, 0xaa, 0x88, 0x77, 0x55, 0x44, 0x22, 0x11};
    if (index < 215) {
        rgb[0] = (uint8_t)(0xff - 0x33 * (index / 36u));
        rgb[1] = (uint8_t)(0xff - 0x33 * (index / 6u % 6u));
        rgb[2] = (uint8_t)(0xff - 0x33 * (index % 6u));
    } else if (index < 255) {
        unsigned group = (index - 215u) / 10u, level = ramp[(index - 215u) % 10u];
        rgb[0] = (uint8_t)(group == 0 || group == 3 ? level : 0);
        rgb[1] = (uint8_t)(group == 1 || group == 3 ? level : 0);
        rgb[2] = (uint8_t)(group == 2 || group == 3 ? level : 0);
    } else {
        rgb[0] = rgb[1] = rgb[2] = 0;
    }
}

/* Classify an entry. Returns 0 for entries that aren't images this codec can
   load: masks, metadata, JPEG 2000, and unknown types. */
static int classify(const uint8_t *type, struct span body, struct candidate *c)
{
    size_t i;
    for (i = 0; i < TYPE_COUNT; i++)
        if (memcmp(types[i].type, type, 4) == 0)
            break;
    if (i == TYPE_COUNT)
        return 0;
    c->info = &types[i];
    c->body = body;
    c->width = types[i].width;
    c->height = types[i].height;
    switch (types[i].format) {
    case MONO: c->depth = 1; break;
    case PAL4: c->depth = 4; break;
    case PAL8: c->depth = 8; break;
    case RGB: c->depth = 32; break;
    default:
        c->depth = 32;
        if (png_signature(body.data, body.length)) {
            /* The PNG's own size counts, whatever the slot says. A broken
               header still counts as an image, so indexes stay stable. */
            if (png_info(body.data, body.length, &c->width, &c->height) != CODEC_OK)
                c->width = c->height = 0;
        } else if (is_jpeg2000(&body)) {
            return 0;
        } else if (body.length >= 4 && memcmp(body.data, "ARGB", 4) == 0) {
            break;
        } else if (strcmp(types[i].mask, "rgb") == 0) {
            c->depth = 24;
        } else {
            return 0;
        }
        break;
    }
    /* A '#' entry is both an icon and a mask; only the image part counts. */
    return 1;
}

static int better(const struct candidate *a, const struct candidate *b)
{
    unsigned long pa = (unsigned long)a->width * a->height;
    unsigned long pb = (unsigned long)b->width * b->height;
    if (pa != pb)
        return pa > pb;
    if (a->depth != b->depth)
        return a->depth > b->depth;
    return a->info->scale < b->info->scale;
}

/* Fill count bytes at dst, stride apart, from ICNS RLE: a byte below 0x80
   copies that many plus one literals; from 0x80 up, it repeats the next byte
   that many minus 125 times. A code that runs past the end is clamped. */
static enum codec_result unpack(const struct span *src, size_t *pos,
                                uint8_t *dst, size_t count, size_t stride)
{
    size_t filled = 0, n, k;
    while (filled < count) {
        unsigned code;
        if (*pos >= src->length)
            return CODEC_TRUNCATED;
        code = src->data[(*pos)++];
        if (code & 0x80u) {
            if (*pos >= src->length)
                return CODEC_TRUNCATED;
            n = code - 125u;
            for (k = 0; k < n && filled < count; k++, filled++)
                dst[filled * stride] = src->data[*pos];
            (*pos)++;
        } else {
            n = code + 1u;
            if (n > src->length - *pos)
                return CODEC_TRUNCATED;
            for (k = 0; k < n && filled < count; k++, filled++)
                dst[filled * stride] = src->data[*pos + k];
            *pos += n;
        }
    }
    return CODEC_OK;
}

static void apply_bitmask(uint8_t *rgba, size_t pixels, const uint8_t *bits)
{
    size_t i;
    for (i = 0; i < pixels; i++)
        rgba[i * 4u + 3u] = (bits[i >> 3] >> (7u - (i & 7u))) & 1u ? 255 : 0;
}

/* Decode anything but PNG into rgba, which holds the candidate's pixels. */
static enum codec_result decode_entry(const struct candidate *c,
                                      const struct span *masks, uint8_t *rgba)
{
    size_t pixels = (size_t)c->width * c->height, i, pos = 0;
    const struct span *mask = NULL;
    const struct span *body = &c->body;
    enum codec_result result;
    uint8_t rgb[3];

    for (i = 0; i < MASK_COUNT; i++)
        if (strcmp(c->info->mask, mask_types[i]) == 0 && masks[i].data != NULL)
            mask = &masks[i];

    switch (c->info->format) {
    case MONO:
        if (body->length < pixels / 8u)
            return CODEC_TRUNCATED;
        for (i = 0; i < pixels; i++) {
            uint8_t v = (body->data[i >> 3] >> (7u - (i & 7u))) & 1u ? 0 : 255;
            rgba[i * 4u] = rgba[i * 4u + 1u] = rgba[i * 4u + 2u] = v;
            rgba[i * 4u + 3u] = 255;
        }
        /* The '#' types carry their mask after the image. */
        if (c->info->mask[0] != '\0') {
            if (body->length < pixels / 4u)
                return CODEC_TRUNCATED;
            apply_bitmask(rgba, pixels, body->data + pixels / 8u);
        }
        return CODEC_OK;
    case PAL4:
    case PAL8:
        if (body->length < (c->info->format == PAL4 ? pixels / 2u : pixels))
            return CODEC_TRUNCATED;
        for (i = 0; i < pixels; i++) {
            if (c->info->format == PAL4) {
                unsigned v = body->data[i >> 1];
                memcpy(rgba + i * 4u, palette4[i & 1u ? v & 15u : v >> 4], 3);
            } else {
                palette8(body->data[i], rgb);
                memcpy(rgba + i * 4u, rgb, 3);
            }
            rgba[i * 4u + 3u] = 255;
        }
        /* A missing or short mask leaves the icon opaque. */
        if (mask != NULL && mask->length >= pixels / 4u)
            apply_bitmask(rgba, pixels, mask->data + pixels / 8u);
        return CODEC_OK;
    default:
        break;
    }

    if (c->info->format == PACKED && c->depth == 32) {
        /* "ARGB", then the four channels packed one after another. */
        pos = 4;
        for (i = 0; i < 4; i++) {
            result = unpack(body, &pos, rgba + (i + 3u) % 4u, pixels, 4);
            if (result != CODEC_OK)
                return result;
        }
        return CODEC_OK;
    }

    /* 24-bit: it32 has four bytes before its data. Data exactly three bytes
       per pixel is uncompressed and interleaved; otherwise each channel is
       packed in turn. */
    if (memcmp(c->info->type, "it32", 4) == 0)
        pos = 4;
    if (body->length < pos)
        return CODEC_TRUNCATED;
    if (body->length - pos == pixels * 3u) {
        for (i = 0; i < pixels; i++)
            memcpy(rgba + i * 4u, body->data + pos + i * 3u, 3);
    } else {
        for (i = 0; i < 3; i++) {
            result = unpack(body, &pos, rgba + i, pixels, 4);
            if (result != CODEC_OK)
                return result;
        }
    }
    for (i = 0; i < pixels; i++)
        rgba[i * 4u + 3u] = mask != NULL && mask->length >= pixels ? mask->data[i] : 255;
    return CODEC_OK;
}

enum codec_result icns_decode(const uint8_t *data, size_t length, long index,
                              struct icns_image *image, unsigned *count)
{
    struct span masks[MASK_COUNT];
    struct candidate c, chosen;
    size_t end, pos, i, pixels;
    unsigned found = 0;
    int have = 0;
    enum codec_result result;

    image->width = image->height = 0;
    image->rgba = NULL;
    *count = 0;
    memset(masks, 0, sizeof masks);
    memset(&chosen, 0, sizeof chosen);
    if (length < 4 || memcmp(data, "icns", 4) != 0)
        return CODEC_INVALID;
    if (length < 8)
        return CODEC_TRUNCATED;
    end = be32(data + 4);
    if (end < 8)
        return CODEC_INVALID;
    if (end > length)
        return CODEC_TRUNCATED;

    for (pos = 8; pos < end; pos += be32(data + pos + 4)) {
        struct span body;
        size_t size;
        if (end - pos < 8)
            return CODEC_TRUNCATED;
        size = be32(data + pos + 4);
        if (size < 8)
            return CODEC_INVALID;
        if (size > end - pos)
            return CODEC_TRUNCATED;
        body.data = data + pos + 8;
        body.length = size - 8u;
        for (i = 0; i < MASK_COUNT; i++)
            if (memcmp(data + pos, mask_types[i], 4) == 0 && masks[i].data == NULL)
                masks[i] = body;
        if (!classify(data + pos, body, &c))
            continue;
        if (index == ICNS_BEST ? !have || better(&c, &chosen) : (long)found == index) {
            chosen = c;
            have = 1;
        }
        found++;
    }
    *count = found;
    if (!have)
        return CODEC_INVALID;
    if (chosen.width == 0)
        return png_info(chosen.body.data, chosen.body.length, &chosen.width, &chosen.height);

    pixels = (size_t)chosen.width * chosen.height;
    if (chosen.info->format == PACKED && png_signature(chosen.body.data, chosen.body.length)) {
        unsigned w, h;
        result = png_decode(chosen.body.data, chosen.body.length, &w, &h, &image->rgba);
    } else {
        image->rgba = malloc(pixels * 4u);
        if (image->rgba == NULL)
            return CODEC_NO_MEMORY;
        result = decode_entry(&chosen, masks, image->rgba);
    }
    if (result != CODEC_OK) {
        icns_free(image);
        return result;
    }
    image->width = chosen.width;
    image->height = chosen.height;
    return CODEC_OK;
}

void icns_free(struct icns_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
}
