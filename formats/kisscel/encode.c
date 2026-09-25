#include "encode.h"
#include <string.h>

int kisscel_make_header(unsigned width, unsigned height,
                        uint8_t header[KISSCEL_HEADER_SIZE])
{
    if (width == 0 || height == 0 || width > 65535u || height > 65535u)
        return 0;
    memset(header, 0, KISSCEL_HEADER_SIZE);
    memcpy(header, "KiSS", 4);
    header[4] = 0x21; /* a colour cel */
    header[5] = 32;
    header[8] = (uint8_t)width;
    header[9] = (uint8_t)(width >> 8);
    header[10] = (uint8_t)height;
    header[11] = (uint8_t)(height >> 8);
    return 1;
}

void kisscel_encode_row(const uint8_t *rgba, unsigned width, uint8_t *out)
{
    unsigned x;
    for (x = 0; x < width; x++, rgba += 4, out += 4) {
        out[0] = rgba[2];
        out[1] = rgba[1];
        out[2] = rgba[0];
        out[3] = rgba[3];
    }
}
