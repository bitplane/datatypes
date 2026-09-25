#include "encode.h"
#include <string.h>

static void put16(uint8_t *p, unsigned value)
{
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

unsigned pdb_padded_width(unsigned width)
{
    return (width + 15u) & ~15u;
}

static int valid(unsigned width, unsigned depth)
{
    return width != 0 && pdb_padded_width(width) <= 65535u &&
           (depth == 1 || depth == 2 || depth == 4);
}

static unsigned over_white(const uint8_t *pixel, unsigned channel)
{
    unsigned a = pixel[3];
    return (pixel[channel] * a + 255u * (255u - a) + 127u) / 255u;
}

static unsigned gray(const uint8_t *pixel)
{
    return (77u * over_white(pixel, 0) + 150u * over_white(pixel, 1) +
            29u * over_white(pixel, 2) + 128u) >> 8;
}

unsigned pdb_row_depth(const uint8_t *rgba, unsigned width)
{
    unsigned depth = 1, x;
    if (rgba == NULL)
        return 4;
    for (x = 0; x < width; x++, rgba += 4) {
        unsigned g = gray(rgba);
        if (g % 85u != 0)
            return 4;
        if (g != 0 && g != 255u)
            depth = 2;
    }
    return depth;
}

int pdb_make_header(const char *name, unsigned width, unsigned height, unsigned depth,
                    uint8_t header[PDB_HEADER_SIZE])
{
    static const uint8_t types[5] = { 0, 0xff, 0x00, 0, 0x02 };
    unsigned padded = pdb_padded_width(width);
    size_t length;

    if (header == NULL || !valid(width, depth) || height == 0 || height > 65535u ||
        (unsigned long)padded * height > 16ul * 1024ul * 1024ul)
        return 0;
    if (name == NULL || name[0] == '\0')
        name = "Image";
    length = strlen(name);
    if (length > 31)
        length = 31;
    memset(header, 0, PDB_HEADER_SIZE);
    /* Database header: no dates, attributes, app info or sort info. */
    memcpy(header, name, length);
    memcpy(header + 60, "vIMGView", 8);
    put16(header + 76, 1);
    /* The record list: the image record right after it, with the attributes
       and unique ID ImageMagick and netpbm write. */
    put16(header + 78, 0);
    put16(header + 80, 86);
    memcpy(header + 82, "\x40\x6f\x80\x00", 4);
    /* Image header: uncompressed, no note, no anchor. */
    memcpy(header + 86, name, length);
    header[119] = types[depth];
    put16(header + 136, 0xffffu);
    put16(header + 138, 0xffffu);
    put16(header + 140, padded);
    put16(header + 142, height);
    return 1;
}

size_t pdb_row_bytes(unsigned width, unsigned depth)
{
    if (!valid(width, depth))
        return 0;
    return (size_t)pdb_padded_width(width) * depth / 8u;
}

size_t pdb_encode_row(const uint8_t *rgba, unsigned width, unsigned depth,
                      uint8_t *output, size_t capacity)
{
    size_t bytes = pdb_row_bytes(width, depth);
    unsigned x, top = (1u << depth) - 1u;

    if (rgba == NULL || output == NULL || bytes == 0 || capacity < bytes)
        return 0;
    /* Zero is white, so the padding needs nothing. */
    memset(output, 0, bytes);
    for (x = 0; x < width; x++, rgba += 4) {
        unsigned level = (gray(rgba) * top + 127u) / 255u;
        unsigned bit = x * depth;
        output[bit / 8u] |= (uint8_t)((top - level) << (8u - depth - bit % 8u));
    }
    return bytes;
}
