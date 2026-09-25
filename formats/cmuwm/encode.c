#include "encode.h"
#include <string.h>

static void put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

int cmuwm_make_header(uint8_t header[CMUWM_HEADER_SIZE], unsigned width, unsigned height)
{
    if (width == 0 || height == 0 || width > CMUWM_MAX_SIDE || height > CMUWM_MAX_SIDE ||
        (unsigned long)width * height > CMUWM_MAX_PIXELS)
        return 0;
    put32(header, 0xf10040bbUL);
    put32(header + 4, width);
    put32(header + 8, height);
    header[12] = 0;
    header[13] = 1;
    return 1;
}

static unsigned over_white(const uint8_t *pixel, unsigned channel)
{
    unsigned a = pixel[3];
    return (pixel[channel] * a + 255u * (255u - a) + 127u) / 255u;
}

size_t cmuwm_encode_row(const uint8_t *rgba, unsigned width, uint8_t *output, size_t capacity)
{
    size_t size = (width + 7u) / 8u;
    unsigned x;

    if (rgba == NULL || output == NULL || width == 0 || width > CMUWM_MAX_SIDE || capacity < size)
        return 0;
    memset(output, 0xff, size);
    for (x = 0; x < width; x++, rgba += 4) {
        unsigned luma = 77u * over_white(rgba, 0) + 150u * over_white(rgba, 1) +
                        29u * over_white(rgba, 2);
        if (luma < 128u * 256u)
            output[x / 8u] &= (uint8_t)~(0x80u >> (x % 8u));
    }
    return size;
}
