#include "encode.h"

#include <stdlib.h>
#include <string.h>

#include "japanpc.h"

#define ID_SIZE 31u     /* magic, machine, user name and the 0x1A */
#define HEADER_SIZE 32u

/* Bytes left and lines up each copy code reads its 2 bytes from. */
static const uint8_t copy_x[16] = { 0, 2, 4, 8, 0, 2, 0, 2, 4, 0, 2, 4, 0, 2, 4, 0 };
static const uint8_t copy_y[16] = { 0, 0, 0, 0, 1, 1, 2, 2, 2, 4, 4, 4, 8, 8, 8, 16 };
/* Nearest sources first. */
static const uint8_t order[15] = { 1, 4, 5, 2, 6, 7, 8, 3, 9, 10, 11, 12, 13, 14, 15 };

static unsigned line_bytes(unsigned width, int deep)
{
    /* Whole flag units: 8 pixels of 16 colours, 4 of 256. */
    return deep ? (width + 3u) & ~3u : ((width + 7u) & ~7u) / 2u;
}

size_t japanpc_encode_bound(unsigned width, unsigned height)
{
    size_t stride, flags;
    if (width == 0 || height == 0 || width > JP_MAX_SIDE ||
        height > JP_MAX_SIDE || (size_t)width * height > JP_MAX_PIXELS)
        return 0;
    stride = line_bytes(width, 1);
    flags = stride / 4u * height;
    return ID_SIZE + HEADER_SIZE + 768u + (flags + 7u) / 8u + 1u + flags +
           1u + stride * height;
}

static void put16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, size_t v)
{
    put16(p, (unsigned)(v & 0xffffu));
    put16(p + 2, (unsigned)(v >> 16));
}

/* Pixel colours over white, and the palette of up to 256 of them. */
static enum codec_result index_colours(const uint8_t *rgba, size_t count,
                                       uint8_t *indexes, uint32_t *palette,
                                       unsigned *colours)
{
    enum { SLOTS = 1024 };
    uint32_t *keys = malloc(SLOTS * sizeof *keys);
    uint16_t *values = malloc(SLOTS * sizeof *values);
    uint32_t last = 0xffffffffu;
    unsigned last_index = 0;
    size_t i;

    if (keys == NULL || values == NULL) {
        free(keys);
        free(values);
        return CODEC_NO_MEMORY;
    }
    for (i = 0; i < SLOTS; i++)
        keys[i] = 0xffffffffu;
    *colours = 0;
    for (i = 0; i < count; i++, rgba += 4) {
        unsigned a = rgba[3], c, slot;
        uint32_t rgb = 0;
        for (c = 0; c < 3; c++)
            rgb = rgb << 8 | (rgba[c] * a + 255u * (255u - a) + 127u) / 255u;
        if (rgb != last) {
            slot = (unsigned)((rgb * 2654435761u) >> 22) & (SLOTS - 1u);
            while (keys[slot] != rgb && keys[slot] != 0xffffffffu)
                slot = (slot + 1u) & (SLOTS - 1u);
            if (keys[slot] != rgb) {
                if (*colours == 256) {
                    free(keys);
                    free(values);
                    return CODEC_INVALID;
                }
                keys[slot] = rgb;
                values[slot] = (uint16_t)*colours;
                palette[(*colours)++] = rgb;
            }
            last = rgb;
            last_index = values[slot];
        }
        indexes[i] = (uint8_t)last_index;
    }
    free(keys);
    free(values);
    return CODEC_OK;
}

enum codec_result japanpc_encode(const uint8_t *rgba, unsigned width,
                                 unsigned height, uint8_t *output,
                                 size_t capacity, size_t *size)
{
    uint32_t palette[256];
    uint8_t *indexes, *lines, *codes, *flags, *flag_b, *pixels, *out;
    unsigned colours, stride, units, x, y, i;
    size_t count = (size_t)width * height, a_bytes, b_size = 0, p_size = 0;
    size_t flag_a_at, flag_b_at, pixels_at, bits = 0;
    enum codec_result result;
    int deep;

    if (japanpc_encode_bound(width, height) == 0)
        return CODEC_TOO_LARGE;
    if (capacity < japanpc_encode_bound(width, height))
        return CODEC_NO_MEMORY;
    indexes = malloc(count);
    if (indexes == NULL)
        return CODEC_NO_MEMORY;
    result = index_colours(rgba, count, indexes, palette, &colours);
    if (result != CODEC_OK) {
        free(indexes);
        return result;
    }
    deep = colours > 16;
    stride = line_bytes(width, deep);
    units = stride / 2u;

    /* Lines padded with colour 0 to whole flag units. */
    lines = calloc((size_t)stride * height, 1);
    codes = calloc(units, 1);
    flags = calloc(stride / 4u, 1);
    flag_b = malloc((size_t)stride / 4u * height + 1u);
    pixels = malloc((size_t)stride * height);
    if (lines == NULL || codes == NULL || flags == NULL || flag_b == NULL ||
        pixels == NULL) {
        result = CODEC_NO_MEMORY;
        goto done;
    }
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++) {
            uint8_t c = indexes[(size_t)y * width + x];
            uint8_t *p = lines + (size_t)y * stride;
            if (deep)
                p[x] = c;
            else
                p[x / 2u] |= (uint8_t)(x & 1u ? c : c << 4);
        }

    /* Header and palette */
    flag_a_at = HEADER_SIZE + (deep ? 768u : 48u);
    a_bytes = ((size_t)stride / 4u * height + 7u) / 8u;
    a_bytes += a_bytes & 1u; /* offsets stay even */
    out = output;
    memset(out, 0, ID_SIZE + flag_a_at + a_bytes);
    memcpy(out, "MAKI02  AROS                  \x1a", ID_SIZE);
    out += ID_SIZE;
    out[3] = deep ? 0x80u : 0x00u;
    put16(out + 8, width - 1u);
    put16(out + 10, height - 1u);
    for (i = 0; i < colours; i++) {
        uint8_t *entry = out + HEADER_SIZE + i * 3u;
        entry[0] = (uint8_t)(palette[i] >> 8);
        entry[1] = (uint8_t)(palette[i] >> 16);
        entry[2] = (uint8_t)palette[i];
    }

    /* Each 2-byte unit is copied from one of 15 places, or stored. A flag
       byte holds the codes of 2 units, XORed with the line above's; flag A
       has a bit for each, set when that isn't 0 and stored in flag B. */
    for (y = 0; y < height; y++) {
        const uint8_t *line = lines + (size_t)y * stride;
        for (x = 0; x < units; x++) {
            unsigned at = x * 2u, code = 0, k;
            unsigned above = codes[x];
            for (k = 0; k < 16 && code == 0; k++) {
                unsigned c = k == 0 ? above : order[k - 1u];
                const uint8_t *from;
                if (c == 0 || at < copy_x[c] || y < copy_y[c])
                    continue;
                from = line - (size_t)copy_y[c] * stride + at - copy_x[c];
                if (from[0] == line[at] && from[1] == line[at + 1u])
                    code = c;
            }
            codes[x] = (uint8_t)code;
            if (code == 0) {
                pixels[p_size++] = line[at];
                pixels[p_size++] = line[at + 1u];
            }
            if (x & 1u) {
                unsigned flag = codes[x - 1u] << 4 | code;
                unsigned change = flag ^ flags[x / 2u];
                flags[x / 2u] = (uint8_t)flag;
                if (change) {
                    out[flag_a_at + bits / 8u] |= (uint8_t)(0x80u >> bits % 8u);
                    flag_b[b_size++] = (uint8_t)change;
                }
                bits++;
            }
        }
    }
    if (b_size & 1u)
        flag_b[b_size++] = 0;
    flag_b_at = flag_a_at + a_bytes;
    pixels_at = flag_b_at + b_size;
    memcpy(out + flag_b_at, flag_b, b_size);
    memcpy(out + pixels_at, pixels, p_size);
    put32(out + 12, flag_a_at);
    put32(out + 16, flag_b_at);
    put32(out + 20, b_size);
    put32(out + 24, pixels_at);
    put32(out + 28, p_size);
    *size = ID_SIZE + pixels_at + p_size;
    result = CODEC_OK;
done:
    free(indexes);
    free(lines);
    free(codes);
    free(flags);
    free(flag_b);
    free(pixels);
    return result;
}
