#include "encode.h"
#include <string.h>

static void put_be32(uint8_t *p, unsigned long value)
{
    p[0] = (uint8_t)(value >> 24); p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8); p[3] = (uint8_t)value;
}

int viff_row_has_alpha(const uint8_t *rgba, unsigned width)
{
    unsigned x;
    if (rgba == NULL)
        return 0;
    for (x = 0; x < width; x++)
        if (rgba[x * 4u + 3u] != 255)
            return 1;
    return 0;
}

int viff_make_header(unsigned width, unsigned height, unsigned bands,
                     uint8_t header[VIFF_WRITE_HEADER])
{
    /* From offset 520: size, no subrows or offset, 1.0 pixel sizes, implicit
       locations, one image of 1-byte bands, raw, no map (optional), generic RGB. */
    const unsigned long fields[21] = {
        width, height, 0, 0, 0, 0x3f800000ul, 0x3f800000ul,
        1, 0, 1, bands, 1, 0,
        0, 0, 0, 0, 0, 1, 0, 15};
    unsigned i;
    if (header == NULL || width == 0 || height == 0 || width > 65535u ||
        height > 65535u || (bands != 3u && bands != 4u))
        return 0;
    memset(header, 0, VIFF_WRITE_HEADER);
    /* Magic, VIFF file type, release 1, version 3, IEEE (big-endian) order. */
    header[0] = 0xab; header[1] = 1; header[2] = 1; header[3] = 3; header[4] = 2;
    for (i = 0; i < 21; i++)
        put_be32(header + 520u + 4u * i, fields[i]);
    return 1;
}

size_t viff_encode_band(const uint8_t *rgba, unsigned width, unsigned band,
                        uint8_t *output, size_t capacity)
{
    unsigned x;
    if (rgba == NULL || output == NULL || width == 0 || width > 65535u ||
        band > 3u || capacity < width)
        return 0;
    for (x = 0; x < width; x++)
        output[x] = rgba[x * 4u + band];
    return width;
}
