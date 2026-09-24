#include "encode.h"
#include <string.h>

size_t otb_make_header(unsigned width, unsigned height, uint8_t header[OTB_HEADER_MAX])
{
    if (header == NULL || width == 0 || height == 0 || width > 65535u || height > 65535u)
        return 0;
    if (width <= 255u && height <= 255u) {
        header[0] = 0;
        header[1] = (uint8_t)width; header[2] = (uint8_t)height;
        header[3] = 1;
        return 4;
    }
    header[0] = 0x10;
    header[1] = (uint8_t)(width >> 8); header[2] = (uint8_t)width;
    header[3] = (uint8_t)(height >> 8); header[4] = (uint8_t)height;
    header[5] = 1;
    return 6;
}

static unsigned over_white(const uint8_t *pixel, unsigned channel)
{
    unsigned a = pixel[3];
    return (pixel[channel] * a + 255u * (255u - a) + 127u) / 255u;
}

size_t otb_encode_row(const uint8_t *rgba, unsigned width, uint8_t *output, size_t capacity)
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
        if (luma < 128u * 256u)
            output[x / 8u] |= (uint8_t)(0x80u >> (x % 8u));
    }
    return bytes;
}
