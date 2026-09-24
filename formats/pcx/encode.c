#include "encode.h"
#include <string.h>

int pcx_make_header(unsigned width, unsigned height, uint8_t header[128])
{
    unsigned bytes_per_line;
    if (header == NULL || width == 0 || height == 0 || width > 65535u || height > 65535u)
        return 0;
    bytes_per_line = (width + 1u) & ~1u;
    if (bytes_per_line > 65535u)
        return 0;
    memset(header, 0, 128);
    header[0] = 0x0a; header[1] = 5; header[2] = 1; header[3] = 8;
    header[8] = (uint8_t)(width - 1u);
    header[9] = (uint8_t)((width - 1u) >> 8);
    header[10] = (uint8_t)(height - 1u);
    header[11] = (uint8_t)((height - 1u) >> 8);
    header[12] = header[14] = 72;
    header[65] = 3;
    header[66] = (uint8_t)bytes_per_line;
    header[67] = (uint8_t)(bytes_per_line >> 8);
    header[68] = 1;
    return 1;
}

static uint8_t channel(const uint8_t *rgba, unsigned pixel, unsigned plane)
{
    unsigned a = rgba[pixel * 4u + 3u];
    unsigned value = rgba[pixel * 4u + plane];
    return (uint8_t)((value * a + 255u * (255u - a) + 127u) / 255u);
}

size_t pcx_encode_row(const uint8_t *rgba, unsigned width, uint8_t *output, size_t capacity)
{
    unsigned plane, bytes_per_line, x;
    size_t pos = 0;
    if (rgba == NULL || output == NULL || width == 0 || width > 65534u)
        return 0;
    bytes_per_line = (width + 1u) & ~1u;
    for (plane = 0; plane < 3; plane++) {
        for (x = 0; x < bytes_per_line;) {
            uint8_t value = x < width ? channel(rgba, x, plane) : 0;
            unsigned run = 1;
            while (x + run < bytes_per_line && run < 63u &&
                   (x + run < width ? channel(rgba, x + run, plane) : 0) == value)
                run++;
            if (run > 1 || value >= 0xc0) {
                if (capacity - pos < 2)
                    return 0;
                output[pos++] = (uint8_t)(0xc0u | run);
            } else if (capacity == pos) {
                return 0;
            }
            output[pos++] = value;
            x += run;
        }
    }
    return pos;
}
