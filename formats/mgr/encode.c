#include "encode.h"
#include <string.h>

int mgr_make_header(uint8_t header[MGR_HEADER_SIZE], unsigned width, unsigned height)
{
    if (width == 0 || height == 0 || width > MGR_MAX_SIDE || height > MGR_MAX_SIDE)
        return 0;
    header[0] = 'y';
    header[1] = 'z';
    header[2] = (uint8_t)(' ' + (width >> 6));
    header[3] = (uint8_t)(' ' + (width & 63u));
    header[4] = (uint8_t)(' ' + (height >> 6));
    header[5] = (uint8_t)(' ' + (height & 63u));
    header[6] = ' ' + 1;
    header[7] = ' ';
    return 1;
}

static unsigned over_white(const uint8_t *pixel, unsigned channel)
{
    unsigned a = pixel[3];
    return (pixel[channel] * a + 255u * (255u - a) + 127u) / 255u;
}

size_t mgr_encode_row(const uint8_t *rgba, unsigned width, uint8_t *output, size_t capacity)
{
    size_t size = (width + 7u) / 8u;
    unsigned x;

    if (rgba == NULL || output == NULL || width == 0 || width > MGR_MAX_SIDE || capacity < size)
        return 0;
    memset(output, 0, size);
    for (x = 0; x < width; x++, rgba += 4) {
        unsigned luma = 77u * over_white(rgba, 0) + 150u * over_white(rgba, 1) +
                        29u * over_white(rgba, 2);
        if (luma < 128u * 256u)
            output[x / 8u] |= (uint8_t)(0x80u >> (x % 8u));
    }
    return size;
}
