#include "encode.h"
#include <string.h>

#define FLAG_COLORMAP 0x4000u
#define FLAG_TRANSPARENT 0x2000u
#define FLAG_DIRECT 0x0400u

static void put_be16(uint8_t *p, unsigned value)
{
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static unsigned over_white(unsigned value, unsigned alpha)
{
    return (value * alpha + 255u * (255u - alpha) + 127u) / 255u;
}

static uint32_t color_of(const uint8_t *pixel)
{
    unsigned a = pixel[3];
    return ((uint32_t)over_white(pixel[0], a) << 16) |
           ((uint32_t)over_white(pixel[1], a) << 8) | over_white(pixel[2], a);
}

static unsigned to565(uint32_t color)
{
    unsigned r = (unsigned)(color >> 16) & 255u, g = (unsigned)(color >> 8) & 255u;
    unsigned b = (unsigned)color & 255u;
    return (((r * 31u + 127u) / 255u) << 11) | (((g * 63u + 127u) / 255u) << 5) |
           ((b * 31u + 127u) / 255u);
}

/* Position of color in the sorted palette, or where it would go. */
static unsigned find(const struct palm_encoder *e, uint32_t color, int *found)
{
    unsigned low = 0, high = e->colors < 256 ? e->colors : 256, mid;
    while (low < high) {
        mid = (low + high) / 2u;
        if (e->palette[mid] < color)
            low = mid + 1u;
        else
            high = mid;
    }
    *found = low < e->colors && low < 256 && e->palette[low] == color;
    return low;
}

void palm_encode_begin(struct palm_encoder *e, unsigned width, unsigned height)
{
    memset(e, 0, sizeof *e);
    e->width = width;
    e->height = height;
}

void palm_encode_scan(struct palm_encoder *e, const uint8_t *rgba)
{
    unsigned x, at, value;
    uint32_t color;
    int found;

    for (x = 0; x < e->width; x++, rgba += 4) {
        if (rgba[3] == 0) {
            e->transparent = 1;
            continue;
        }
        color = color_of(rgba);
        value = to565(color);
        e->used[value >> 3] |= (uint8_t)(1u << (value & 7u));
        if (e->colors > 256)
            continue;
        at = find(e, color, &found);
        if (found)
            continue;
        if (e->colors == 256) {
            e->colors = 257;
            continue;
        }
        memmove(e->palette + at + 1, e->palette + at,
                (e->colors - at) * sizeof e->palette[0]);
        e->palette[at] = color;
        e->colors++;
    }
}

size_t palm_encode_row_size(const struct palm_encoder *e)
{
    return e->direct ? (size_t)e->width * 2u : ((size_t)e->width + 1u) & ~(size_t)1u;
}

size_t palm_encode_header(struct palm_encoder *e, uint8_t *output, size_t capacity)
{
    unsigned flags, i, entries;
    uint32_t color = 0;
    size_t pos = 16, rows;
    int found;

    if (output == NULL || e->width == 0 || e->height == 0 ||
        e->width > 65535u || e->height > 65535u)
        return 0;
    e->direct = e->colors + (e->transparent ? 1u : 0u) > 256u;
    rows = palm_encode_row_size(e);
    if (rows > 65535u || capacity < PALM_HEADER_MAX)
        return 0;
    memset(output, 0, PALM_HEADER_MAX);
    if (e->direct) {
        /* The key is the first RGB565 value no pixel uses. Black, if free, also
           reads as transparent in ImageMagick, which takes the key's bytes as
           5- and 6-bit values. With every value in use, transparent pixels
           become white instead. */
        for (e->key = 0; e->transparent && e->key < 65536u; e->key++)
            if (!(e->used[e->key >> 3] & (1u << (e->key & 7u))))
                break;
        if (e->key > 65535u)
            e->transparent = 0;
        flags = FLAG_DIRECT;
        output[16] = 5;
        output[17] = 6;
        output[18] = 5;
        if (e->transparent) {
            output[21] = (uint8_t)((e->key >> 11) * 255u / 31u);
            output[22] = (uint8_t)(((e->key >> 5) & 63u) * 255u / 63u);
            output[23] = (uint8_t)((e->key & 31u) * 255u / 31u);
        }
        pos += 8;
    } else {
        flags = FLAG_COLORMAP;
        entries = e->colors;
        if (e->transparent) {
            /* Give the key a colour no pixel has, so readers that match on
               colour rather than index find only the transparent pixels. */
            e->key = entries++;
            for (i = 0; i <= 256; i++) {
                color = 0xff00ffu ^ i;
                find(e, color, &found);
                if (!found)
                    break;
            }
            e->palette[e->key] = color;
        }
        put_be16(output + pos, entries);
        pos += 2;
        for (i = 0; i < entries; i++, pos += 4) {
            output[pos] = (uint8_t)i;
            output[pos + 1] = (uint8_t)(e->palette[i] >> 16);
            output[pos + 2] = (uint8_t)(e->palette[i] >> 8);
            output[pos + 3] = (uint8_t)e->palette[i];
        }
    }
    if (e->transparent)
        flags |= FLAG_TRANSPARENT;
    put_be16(output, e->width);
    put_be16(output + 2, e->height);
    put_be16(output + 4, (unsigned)rows);
    put_be16(output + 6, flags);
    output[8] = e->direct ? 16 : 8;
    output[9] = e->direct || e->transparent ? 2 : 1;
    output[12] = e->direct ? 0 : (uint8_t)e->key;
    output[13] = 0xff;
    return pos;
}

size_t palm_encode_row(const struct palm_encoder *e, const uint8_t *rgba,
                       uint8_t *output, size_t capacity)
{
    size_t size = palm_encode_row_size(e);
    unsigned x, value;
    int found;

    if (rgba == NULL || output == NULL || capacity < size || size == 0)
        return 0;
    output[size - 1] = 0;
    for (x = 0; x < e->width; x++, rgba += 4) {
        if (e->direct) {
            value = rgba[3] == 0 && e->transparent ? e->key : to565(color_of(rgba));
            put_be16(output + (size_t)x * 2u, value);
        } else if (rgba[3] == 0) {
            output[x] = (uint8_t)e->key;
        } else {
            output[x] = (uint8_t)find(e, color_of(rgba), &found);
            if (!found)
                return 0;
        }
    }
    return size;
}
