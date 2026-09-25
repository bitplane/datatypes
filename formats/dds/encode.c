#include <string.h>

#include "encode.h"

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

int dds_row_has_alpha(const uint8_t *rgba, unsigned width)
{
    unsigned x;
    for (x = 0; x < width; x++)
        if (rgba[x * 4u + 3u] != 255)
            return 1;
    return 0;
}

int dds_make_header(unsigned width, unsigned height, int alpha,
                    uint8_t header[DDS_HEADER_SIZE])
{
    unsigned bytes = alpha ? 4u : 3u;

    if (width == 0 || height == 0 || width > 65535u || height > 65535u)
        return 0;
    memset(header, 0, DDS_HEADER_SIZE);
    memcpy(header, "DDS ", 4);
    put32(header + 4, 124);
    /* caps, height, width, pitch and pixel format are set */
    put32(header + 8, 0x100fu);
    put32(header + 12, height);
    put32(header + 16, width);
    put32(header + 20, width * bytes);
    put32(header + 76, 32);
    put32(header + 80, alpha ? 0x41u : 0x40u);
    put32(header + 88, bytes * 8u);
    put32(header + 92, 0xff0000u);
    put32(header + 96, 0xff00u);
    put32(header + 100, 0xffu);
    put32(header + 104, alpha ? 0xff000000u : 0);
    /* DDSCAPS_TEXTURE */
    put32(header + 108, 0x1000u);
    return 1;
}

void dds_encode_row(const uint8_t *rgba, unsigned width, int alpha, uint8_t *output)
{
    unsigned x;
    for (x = 0; x < width; x++, rgba += 4) {
        *output++ = rgba[2];
        *output++ = rgba[1];
        *output++ = rgba[0];
        if (alpha)
            *output++ = rgba[3];
    }
}
