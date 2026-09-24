#include "encode.h"
#include <string.h>

static void put16(uint8_t *p, unsigned value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

int msp_make_header(unsigned width, unsigned height, uint8_t header[MSP_HEADER_SIZE])
{
    unsigned checksum = 0, i;

    if (header == NULL || width == 0 || height == 0 || width > 65535u || height > 65535u ||
        (unsigned long)width * height > 16ul * 1024ul * 1024ul)
        return 0;
    memset(header, 0, MSP_HEADER_SIZE);
    memcpy(header, "DanM", 4);
    put16(header + 4, width);
    put16(header + 6, height);
    /* Square pixels for screen and printer, and a printed size of one dot per pixel. */
    put16(header + 8, 1); put16(header + 10, 1);
    put16(header + 12, 1); put16(header + 14, 1);
    put16(header + 16, width);
    put16(header + 18, height);
    /* The checksum makes the XOR of all sixteen header words zero. */
    for (i = 0; i < 12; i++)
        checksum ^= header[i * 2u] | ((unsigned)header[i * 2u + 1u] << 8);
    put16(header + 24, checksum);
    return 1;
}

static unsigned over_white(const uint8_t *pixel, unsigned channel)
{
    unsigned a = pixel[3];
    return (pixel[channel] * a + 255u * (255u - a) + 127u) / 255u;
}

size_t msp_encode_row(const uint8_t *rgba, unsigned width, uint8_t *output, size_t capacity)
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
