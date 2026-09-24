#include "encode.h"
#include <string.h>

static void put_be32(uint8_t *p, unsigned long value)
{
    p[0] = (uint8_t)(value >> 24); p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8); p[3] = (uint8_t)value;
}

int xwd_make_header(unsigned width, unsigned height,
                    uint8_t header[XWD_WRITE_HEADER])
{
    /* Header fields in file order; the empty window name pads it to 104 bytes. */
    const unsigned long fields[25] = {
        XWD_WRITE_HEADER, 7, 2, 24, width, height, 0,
        1, 32, 1, 32, 32, (unsigned long)width * 4u,
        4, 0xff0000ul, 0xff00ul, 0xfful, 8, 256, 0,
        width, height, 0, 0, 0};
    unsigned i;
    if (header == NULL || width == 0 || height == 0 || width > 65535u || height > 65535u)
        return 0;
    memset(header, 0, XWD_WRITE_HEADER);
    for (i = 0; i < 25; i++)
        put_be32(header + 4u * i, fields[i]);
    return 1;
}

static uint8_t channel(const uint8_t *rgba, unsigned pixel, unsigned plane)
{
    unsigned a = rgba[pixel * 4u + 3u];
    unsigned value = rgba[pixel * 4u + plane];
    return (uint8_t)((value * a + 255u * (255u - a) + 127u) / 255u);
}

size_t xwd_encode_row(const uint8_t *rgba, unsigned width,
                      uint8_t *output, size_t capacity)
{
    unsigned x;
    if (rgba == NULL || output == NULL || width == 0 || width > 65535u ||
        capacity < (size_t)width * 4u)
        return 0;
    for (x = 0; x < width; x++) {
        output[x * 4u] = 0;
        output[x * 4u + 1u] = channel(rgba, x, 0);
        output[x * 4u + 2u] = channel(rgba, x, 1);
        output[x * 4u + 3u] = channel(rgba, x, 2);
    }
    return (size_t)width * 4u;
}
