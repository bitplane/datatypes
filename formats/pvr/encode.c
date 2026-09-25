#include <string.h>

#include "encode.h"

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

int pvr_row_has_alpha(const uint8_t *rgba, unsigned width)
{
    unsigned x;
    for (x = 0; x < width; x++)
        if (rgba[x * 4u + 3u] != 255)
            return 1;
    return 0;
}

int pvr_make_header(unsigned width, unsigned height, int alpha,
                    uint8_t header[PVR_HEADER_SIZE])
{
    if (width == 0 || height == 0 || width > 65535u || height > 65535u)
        return 0;
    memset(header, 0, PVR_HEADER_SIZE);
    memcpy(header, "PVR\3", 4);
    /* Channel names, then their widths */
    memcpy(header + 8, alpha ? "rgba" : "rgb", alpha ? 4 : 3);
    memset(header + 12, 8, alpha ? 4 : 3);
    /* sRGB, unsigned normalised bytes */
    put32(header + 16, 1);
    put32(header + 24, height);
    put32(header + 28, width);
    /* depth, surfaces, faces and mip levels */
    put32(header + 32, 1);
    put32(header + 36, 1);
    put32(header + 40, 1);
    put32(header + 44, 1);
    return 1;
}

void pvr_encode_row(const uint8_t *rgba, unsigned width, int alpha, uint8_t *output)
{
    unsigned x;
    for (x = 0; x < width; x++, rgba += 4) {
        *output++ = rgba[0];
        *output++ = rgba[1];
        *output++ = rgba[2];
        if (alpha)
            *output++ = rgba[3];
    }
}
