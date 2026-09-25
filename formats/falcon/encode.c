#include "encode.h"
#include "decode.h"
#include <string.h>

#define PRISM_HEADER 128u

static size_t stride(unsigned width)
{
    return (((size_t)width + 15u) & ~(size_t)15u) * 3u;
}

size_t falcon_encode_size(unsigned width, unsigned height)
{
    if (width == 0 || height == 0 || width > FALCON_MAX_SIDE ||
        height > FALCON_MAX_SIDE || (uint64_t)width * height > FALCON_MAX_PIXELS)
        return 0;
    return PRISM_HEADER + stride(width) * height;
}

static void put16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

enum codec_result falcon_encode(const uint8_t *rgba, unsigned width,
                                unsigned height, uint8_t *out, size_t capacity)
{
    size_t size = falcon_encode_size(width, height), data, line = stride(width);
    unsigned x, y, c;

    if (size == 0 || rgba == NULL || out == NULL)
        return size == 0 ? CODEC_TOO_LARGE : CODEC_INVALID;
    if (capacity < size)
        return CODEC_NO_MEMORY;
    memset(out, 0, size);
    memcpy(out, "PNT", 4);
    put16(out + 4, 0x0100);
    /* No palette. */
    put16(out + 8, width);
    put16(out + 10, height);
    put16(out + 12, 24);
    /* Uncompressed; the size of the bitmap follows. */
    data = line * height;
    out[16] = (uint8_t)(data >> 24);
    out[17] = (uint8_t)(data >> 16);
    out[18] = (uint8_t)(data >> 8);
    out[19] = (uint8_t)data;
    for (y = 0; y < height; y++) {
        uint8_t *dst = out + PRISM_HEADER + (size_t)y * line;
        const uint8_t *src = rgba + (size_t)y * width * 4u;
        for (x = 0; x < width; x++, src += 4)
            for (c = 0; c < 3; c++)
                *dst++ = (uint8_t)((src[c] * src[3] + 255u * (255u - src[3]) + 127u) / 255u);
    }
    return CODEC_OK;
}
