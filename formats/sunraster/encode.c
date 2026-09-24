#include "encode.h"
#include <string.h>

static void put_be32(uint8_t *p, unsigned long value)
{
    p[0] = (uint8_t)(value >> 24); p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8); p[3] = (uint8_t)value;
}

static size_t row_bytes(unsigned width)
{
    return ((size_t)width * 3u + 1u) & ~(size_t)1u;
}

int sunraster_make_header(unsigned width, unsigned height, uint8_t header[32])
{
    unsigned long long length;
    if (header == NULL || width == 0 || height == 0 || width > 65535u || height > 65535u)
        return 0;
    length = (unsigned long long)row_bytes(width) * height;
    if (length > 0xfffffffful)
        return 0;
    memset(header, 0, 32);
    put_be32(header, 0x59a66a95ul);
    put_be32(header + 4, width);
    put_be32(header + 8, height);
    put_be32(header + 12, 24);
    put_be32(header + 16, (unsigned long)length);
    put_be32(header + 20, 1);
    return 1;
}

static uint8_t channel(const uint8_t *rgba, unsigned pixel, unsigned plane)
{
    unsigned a = rgba[pixel * 4u + 3u];
    unsigned value = rgba[pixel * 4u + plane];
    return (uint8_t)((value * a + 255u * (255u - a) + 127u) / 255u);
}

size_t sunraster_encode_row(const uint8_t *rgba, unsigned width,
                            uint8_t *output, size_t capacity)
{
    size_t size, pos = 0;
    unsigned x;
    if (rgba == NULL || output == NULL || width == 0 || width > 65535u)
        return 0;
    size = row_bytes(width);
    if (capacity < size)
        return 0;
    for (x = 0; x < width; x++) {
        output[pos++] = channel(rgba, x, 2);
        output[pos++] = channel(rgba, x, 1);
        output[pos++] = channel(rgba, x, 0);
    }
    if (pos < size)
        output[pos++] = 0;
    return pos;
}
