#include "common/icoenc.h"
#include <string.h>

#define HEADER_SIZE (6u + 16u + 40u)

static void put16(uint8_t *p, unsigned value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *p, uint32_t value)
{
    put16(p, value & 0xffffu);
    put16(p + 2, value >> 16);
}

static size_t image_size(unsigned width, unsigned height, unsigned bpp)
{
    size_t stride = ((size_t)width * bpp + 31u) / 32u * 4u;
    size_t mask_stride = ((size_t)width + 31u) / 32u * 4u;
    return (stride + mask_stride) * height;
}

size_t ico_encode_capacity(unsigned width, unsigned height)
{
    if (width == 0 || height == 0 ||
        width > ICO_MAX_ENCODE_SIDE || height > ICO_MAX_ENCODE_SIDE)
        return 0;
    return HEADER_SIZE + image_size(width, height, 32);
}

size_t ico_encode(const uint8_t *rgba, unsigned width, unsigned height,
                  int cursor, unsigned hot_x, unsigned hot_y,
                  uint8_t *output, size_t capacity)
{
    size_t pixels = (size_t)width * height, i, x, y, stride, mask_stride, size;
    unsigned bpp = 24;
    uint8_t *e, *d, *row;

    if (ico_encode_capacity(width, height) == 0 || hot_x > 0xffffu || hot_y > 0xffffu)
        return 0;
    for (i = 0; i < pixels; i++)
        if (rgba[i * 4u + 3] != 255)
            bpp = 32;
    size = HEADER_SIZE + image_size(width, height, bpp);
    if (capacity < size)
        return 0;
    memset(output, 0, size);
    put16(output + 2, cursor ? 2 : 1);
    put16(output + 4, 1);
    /* The directory stores 256 as 0. */
    e = output + 6;
    e[0] = (uint8_t)width;
    e[1] = (uint8_t)height;
    put16(e + 4, cursor ? hot_x : 1);
    put16(e + 6, cursor ? hot_y : bpp);
    put32(e + 8, (uint32_t)(size - 22u));
    put32(e + 12, 22);
    d = output + 22;
    put32(d, 40);
    put32(d + 4, width);
    put32(d + 8, height * 2u);
    put16(d + 12, 1);
    put16(d + 14, bpp);
    put32(d + 20, (uint32_t)image_size(width, height, bpp));
    stride = ((size_t)width * bpp + 31u) / 32u * 4u;
    mask_stride = ((size_t)width + 31u) / 32u * 4u;
    /* Rows run bottom up: the colour image, then the AND mask. */
    for (y = 0; y < height; y++) {
        const uint8_t *in = rgba + (height - 1u - y) * (size_t)width * 4u;
        row = output + HEADER_SIZE + y * stride;
        for (x = 0; x < width; x++, in += 4) {
            uint8_t *o = row + x * (bpp / 8u);
            o[0] = in[2];
            o[1] = in[1];
            o[2] = in[0];
            if (bpp == 32)
                o[3] = in[3];
            if (in[3] == 0)
                output[HEADER_SIZE + stride * height + y * mask_stride + x / 8u] |=
                    (uint8_t)(0x80u >> (x % 8u));
        }
    }
    return size;
}
