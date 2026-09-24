#include <stdlib.h>
#include <string.h>

#include "encode.h"
#include "png.h"

struct slot { unsigned size; char type[5], mask[5]; };

static const struct slot slots[] = {
    {16, "is32", "s8mk"}, {32, "il32", "l8mk"}, {48, "ih32", "h8mk"},
    {128, "it32", "t8mk"}, {64, "icp6", ""}, {256, "ic08", ""},
    {512, "ic09", ""}, {1024, "ic10", ""}
};

static const struct slot *find_slot(unsigned width, unsigned height)
{
    size_t i;
    if (width != height)
        return NULL;
    for (i = 0; i < sizeof slots / sizeof slots[0]; i++)
        if (slots[i].size == width)
            return &slots[i];
    return NULL;
}

int icns_can_encode(unsigned width, unsigned height)
{
    return find_slot(width, height) != NULL;
}

static void put32(uint8_t *p, size_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* Pack one channel: runs of 3 to 130 as 0x80 + (n - 3) and the byte,
   everything else as literals of up to 128 after n - 1. */
static size_t pack(const uint8_t *src, size_t count, uint8_t *out)
{
    size_t i = 0, o = 0;
    while (i < count) {
        size_t run = 1, start, n;
        uint8_t v = src[i * 4u];
        while (i + run < count && run < 130 && src[(i + run) * 4u] == v)
            run++;
        if (run >= 3) {
            out[o++] = (uint8_t)(0x80u + run - 3u);
            out[o++] = v;
            i += run;
            continue;
        }
        for (start = i, n = 0; i < count && n < 128; i++, n++)
            if (i + 2 < count && src[i * 4u] == src[(i + 1) * 4u] &&
                src[i * 4u] == src[(i + 2) * 4u])
                break;
        out[o++] = (uint8_t)(n - 1u);
        while (n-- > 0)
            out[o++] = src[(start++) * 4u];
    }
    return o;
}

enum codec_result icns_encode(const uint8_t *rgba, unsigned width, unsigned height,
                              uint8_t **out, size_t *length)
{
    const struct slot *slot = find_slot(width, height);
    size_t pixels, i, body, pos, prefix;
    uint8_t *file, *png = NULL;
    enum codec_result result;

    *out = NULL;
    *length = 0;
    if (slot == NULL)
        return CODEC_INVALID;
    pixels = (size_t)width * height;

    if (slot->mask[0] == '\0') {
        result = png_encode(rgba, width, height, &png, &body);
        if (result != CODEC_OK)
            return result;
        file = malloc(16u + body);
        if (file == NULL) {
            free(png);
            return CODEC_NO_MEMORY;
        }
        memcpy(file + 16, png, body);
        free(png);
        pos = 16u + body;
    } else {
        /* Three packed channels at worst, then the mask entry. */
        file = malloc(16u + 4u + 3u * (pixels + pixels / 128u + 1u) + 8u + pixels);
        if (file == NULL)
            return CODEC_NO_MEMORY;
        prefix = strcmp(slot->type, "it32") == 0 ? 4u : 0u;
        memset(file + 16, 0, prefix);
        body = prefix;
        for (i = 0; i < 3; i++)
            body += pack(rgba + i, pixels, file + 16 + body);
        /* Readers take exactly three bytes a pixel as uncompressed. */
        if (body - prefix == pixels * 3u)
            for (i = 0; i < pixels; i++)
                memcpy(file + 16 + prefix + i * 3u, rgba + i * 4u, 3);
        /* The mask goes in even for opaque images: libicns won't load
           24-bit icons without one. */
        pos = 16u + body;
        memcpy(file + pos, slot->mask, 4);
        put32(file + pos + 4, 8u + pixels);
        for (i = 0; i < pixels; i++)
            file[pos + 8u + i] = rgba[i * 4u + 3u];
        pos += 8u + pixels;
    }
    memcpy(file, "icns", 4);
    put32(file + 4, pos);
    memcpy(file + 8, slot->type, 4);
    put32(file + 12, 8u + body);
    *out = file;
    *length = pos;
    return CODEC_OK;
}
