#include "encode.h"
#include <string.h>

/* Active window coordinates are signed 16-bit, starting at 0. */
#define RLA_MAX_SIDE 32768u

static void put16(uint8_t *p, unsigned value)
{
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

void alias_put32(uint8_t *p, unsigned long value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

int alias_row_has_alpha(const uint8_t *rgba, unsigned width)
{
    unsigned x;

    for (x = 0; x < width; x++)
        if (rgba[x * 4u + 3u] != 255u)
            return 1;
    return 0;
}

int alias_make_header(unsigned width, unsigned height, int alpha,
                      uint8_t header[RLA_HEADER_SIZE])
{
    if (width == 0 || height == 0 || width > RLA_MAX_SIDE ||
        height > RLA_MAX_SIDE)
        return 0;
    memset(header, 0, RLA_HEADER_SIZE);
    /* The full window and the active window are the whole image. */
    put16(header + 2, width - 1u);
    put16(header + 6, height - 1u);
    put16(header + 10, width - 1u);
    put16(header + 14, height - 1u);
    put16(header + 16, 1u);                 /* frame */
    put16(header + 18, RLA_BYTE);
    put16(header + 20, 3u);
    put16(header + 22, alpha ? 1u : 0u);
    put16(header + 26, 0xfffeu);            /* revision */
    memcpy(header + 28, "2.2", 3);          /* gamma */
    memcpy(header + 580, "rgb", 3);         /* colour channel format */
    put16(header + 658, 8u);
    put16(header + 660, RLA_BYTE);
    put16(header + 662, alpha ? 8u : 0u);
    return 1;
}

size_t alias_row_capacity(unsigned width)
{
    return 4u * (2u + (size_t)width + ((size_t)width + 127u) / 128u);
}

static uint8_t sample(const uint8_t *px, unsigned channel, int alpha)
{
    unsigned a = px[3];

    if (channel == 3u)
        return (uint8_t)a;
    if (alpha)
        return (uint8_t)((px[channel] * a + 127u) / 255u);
    return (uint8_t)((px[channel] * a + 255u * (255u - a) + 127u) / 255u);
}

/* One channel as a length and RLE record: a run of 2 to 128 equal values is
   (count - 1, value), up to 128 other values are (-count, values...). */
static size_t encode_channel(const uint8_t *rgba, unsigned width,
                             unsigned channel, int alpha, uint8_t *output)
{
    size_t pos = 2, x = 0, run, lit;
    uint8_t v;

    while (x < width) {
        v = sample(rgba + x * 4u, channel, alpha);
        for (run = 1; x + run < width && run < 128u &&
             sample(rgba + (x + run) * 4u, channel, alpha) == v; run++)
            ;
        if (run >= 2u) {
            output[pos++] = (uint8_t)(run - 1u);
            output[pos++] = v;
            x += run;
            continue;
        }
        /* Literals until two equal values start a run. */
        for (lit = 1; x + lit < width && lit < 128u; lit++)
            if (x + lit + 1u < width &&
                sample(rgba + (x + lit) * 4u, channel, alpha) ==
                sample(rgba + (x + lit + 1u) * 4u, channel, alpha))
                break;
        output[pos++] = (uint8_t)(256u - lit);
        for (run = 0; run < lit; run++)
            output[pos++] = sample(rgba + (x + run) * 4u, channel, alpha);
        x += lit;
    }
    put16(output, (unsigned)(pos - 2u));
    return pos;
}

size_t alias_encode_row(const uint8_t *rgba, unsigned width, int alpha,
                        uint8_t *output)
{
    size_t size = 0;
    unsigned c, channels = alpha ? 4u : 3u;

    for (c = 0; c < channels; c++)
        size += encode_channel(rgba, width, c, alpha, output + size);
    return size;
}
