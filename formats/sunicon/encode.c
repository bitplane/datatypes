#include "encode.h"
#include <stdio.h>

#define ITEMS_PER_LINE 8u

size_t sunicon_make_header(unsigned width, unsigned height, char *output, size_t capacity)
{
    int n;

    if (width == 0 || height == 0 || width > 65520u || height > 65535u)
        return 0;
    n = snprintf(output, capacity,
                 "/* Format_version=1, Width=%u, Height=%u, Depth=1, "
                 "Valid_bits_per_item=16\n */\n", (width + 15u) & ~15u, height);
    return n > 0 && (size_t)n < capacity ? (size_t)n : 0;
}

void sunicon_encoder_init(struct sunicon_encoder *encoder, unsigned width, unsigned height)
{
    encoder->remaining = (size_t)((width + 15u) / 16u) * height;
    encoder->column = 0;
}

size_t sunicon_row_capacity(unsigned width)
{
    /* A tab, "0xFFFF" and a separator per item. */
    return (size_t)((width + 15u) / 16u) * 9u;
}

static int dark(const uint8_t *p)
{
    unsigned a = p[3], white = 255u * (255u - a);
    unsigned r = (p[0] * a + white) / 255u;
    unsigned g = (p[1] * a + white) / 255u;
    unsigned b = (p[2] * a + white) / 255u;
    /* Rec. 601 luma of the colour composited over white. */
    return 299u * r + 587u * g + 114u * b < 128000u;
}

size_t sunicon_encode_row(struct sunicon_encoder *encoder, const uint8_t *rgba,
                          unsigned width, char *output, size_t capacity)
{
    static const char hex[] = "0123456789ABCDEF";
    unsigned items = (width + 15u) / 16u, i, b;
    size_t pos = 0;

    if (capacity < sunicon_row_capacity(width) || items > encoder->remaining)
        return SIZE_MAX;
    for (i = 0; i < items; i++) {
        unsigned value = 0;
        for (b = 0; b < 16u && i * 16u + b < width; b++)
            if (dark(rgba + (size_t)(i * 16u + b) * 4u))
                value |= 0x8000u >> b;
        if (encoder->column == 0)
            output[pos++] = '\t';
        output[pos++] = '0';
        output[pos++] = 'x';
        output[pos++] = hex[value >> 12];
        output[pos++] = hex[(value >> 8) & 15u];
        output[pos++] = hex[(value >> 4) & 15u];
        output[pos++] = hex[value & 15u];
        if (--encoder->remaining == 0) {
            output[pos++] = '\n';
        } else if (++encoder->column == ITEMS_PER_LINE) {
            output[pos++] = ',';
            output[pos++] = '\n';
            encoder->column = 0;
        } else {
            output[pos++] = ',';
        }
    }
    return pos;
}
