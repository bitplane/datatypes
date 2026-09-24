#include "encode.h"
#include <string.h>

static size_t write_uintvar(unsigned value, uint8_t *output)
{
    size_t count = 1, i;
    unsigned rest;

    for (rest = value >> 7; rest != 0; rest >>= 7)
        count++;
    for (i = count; i-- > 0; value >>= 7)
        output[i] = (uint8_t)((value & 0x7fu) | (i + 1 < count ? 0x80u : 0));
    return count;
}

size_t wbmp_make_header(unsigned width, unsigned height, uint8_t header[WBMP_HEADER_MAX])
{
    size_t pos = 2;
    if (header == NULL || width == 0 || height == 0 || width > 65535u || height > 65535u)
        return 0;
    header[0] = 0; header[1] = 0;
    pos += write_uintvar(width, header + pos);
    pos += write_uintvar(height, header + pos);
    return pos;
}

static unsigned over_white(const uint8_t *pixel, unsigned channel)
{
    unsigned a = pixel[3];
    return (pixel[channel] * a + 255u * (255u - a) + 127u) / 255u;
}

size_t wbmp_encode_row(const uint8_t *rgba, unsigned width, uint8_t *output, size_t capacity)
{
    size_t bytes;
    unsigned x;
    if (rgba == NULL || output == NULL || width == 0 || width > 65535u)
        return 0;
    bytes = (width + 7u) / 8u;
    if (capacity < bytes)
        return 0;
    memset(output, 0, bytes);
    for (x = 0; x < width; x++, rgba += 4) {
        unsigned luma = 77u * over_white(rgba, 0) + 150u * over_white(rgba, 1) +
                        29u * over_white(rgba, 2);
        if (luma >= 128u * 256u)
            output[x / 8u] |= (uint8_t)(0x80u >> (x % 8u));
    }
    return bytes;
}
