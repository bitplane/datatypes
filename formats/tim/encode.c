#include "encode.h"

/* The pixel block's width is 16 bits, counted in 16-bit units. */
#define TIM_MAX_WIDTH (65535u * 2u / 3u)

static void put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, v & 0xffffu);
    put16(p + 2, v >> 16);
}

size_t tim_row_size(unsigned width)
{
    return ((size_t)width * 3u + 1u) & ~(size_t)1u;
}

int tim_make_header(unsigned width, unsigned height, uint8_t header[20])
{
    size_t row = tim_row_size(width);
    if (width == 0 || height == 0 || width > TIM_MAX_WIDTH || height > 65535u ||
        row > (0xffffffffu - 12u) / height)
        return 0;
    put32(header, 0x10);
    put32(header + 4, 3); /* 24-bit, no CLUT */
    put32(header + 8, (uint32_t)(12u + row * height));
    put16(header + 12, 0);
    put16(header + 14, 0);
    put16(header + 16, (uint32_t)(row / 2u));
    put16(header + 18, height);
    return 1;
}

static uint8_t over_white(unsigned value, unsigned a)
{
    return (uint8_t)((value * a + 255u * (255u - a) + 127u) / 255u);
}

void tim_encode_row(const uint8_t *rgba, unsigned width, uint8_t *output)
{
    unsigned x;
    for (x = 0; x < width; x++, rgba += 4, output += 3) {
        output[0] = over_white(rgba[0], rgba[3]);
        output[1] = over_white(rgba[1], rgba[3]);
        output[2] = over_white(rgba[2], rgba[3]);
    }
    if (width & 1u)
        *output = 0;
}
