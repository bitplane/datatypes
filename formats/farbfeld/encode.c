#include "encode.h"
#include <string.h>

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

int farbfeld_make_header(unsigned width, unsigned height, uint8_t header[16])
{
    if (header == NULL || width == 0 || height == 0 ||
        width > 65535u || height > 65535u)
        return 0;
    memcpy(header, "farbfeld", 8);
    put_be32(header + 8, width);
    put_be32(header + 12, height);
    return 1;
}

void farbfeld_encode_row(const uint8_t *rgba, unsigned width, uint8_t *output)
{
    size_t i;

    /* v * 257 spreads 0..255 exactly over 0..65535, so decoding gives v back. */
    for (i = 0; i < (size_t)width * 4u; i++) {
        output[i * 2u] = rgba[i];
        output[i * 2u + 1u] = rgba[i];
    }
}
