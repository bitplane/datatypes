#include "encode.h"

static int same_pixel(const uint8_t *rgba, unsigned a, unsigned b,
                      unsigned bytes_per_pixel)
{
    unsigned i;
    for (i = 0; i < bytes_per_pixel; i++) {
        if (rgba[a * 4u + i] != rgba[b * 4u + i])
            return 0;
    }
    return 1;
}

static void put_pixel(uint8_t *output, const uint8_t *rgba,
                      unsigned bytes_per_pixel)
{
    output[0] = rgba[2];
    output[1] = rgba[1];
    output[2] = rgba[0];
    if (bytes_per_pixel == 4)
        output[3] = rgba[3];
}

size_t tga_encode_row(const uint8_t *rgba, unsigned width, unsigned bytes_per_pixel,
                      uint8_t *output, size_t capacity)
{
    unsigned x = 0;
    size_t pos = 0;

    if (rgba == NULL || output == NULL || (bytes_per_pixel != 3 && bytes_per_pixel != 4))
        return 0;
    while (x < width) {
        unsigned start = x;
        unsigned count;
        int repeated = x + 1 < width && same_pixel(rgba, x, x + 1,
                                                     bytes_per_pixel);
        if (repeated) {
            x += 2;
            while (x < width && x - start < 128 &&
                   same_pixel(rgba, start, x, bytes_per_pixel))
                x++;
            count = x - start;
            if (capacity - pos < 1u + bytes_per_pixel)
                return 0;
            output[pos++] = (uint8_t)(0x80u | (count - 1u));
            put_pixel(output + pos, rgba + start * 4u, bytes_per_pixel);
            pos += bytes_per_pixel;
        } else {
            x++;
            while (x < width && x - start < 128) {
                if (x + 1 < width && same_pixel(rgba, x, x + 1,
                                                bytes_per_pixel))
                    break;
                x++;
            }
            count = x - start;
            if (capacity - pos < 1u + (size_t)count * bytes_per_pixel)
                return 0;
            output[pos++] = (uint8_t)(count - 1u);
            while (count-- != 0) {
                put_pixel(output + pos, rgba + start * 4u, bytes_per_pixel);
                pos += bytes_per_pixel;
                start++;
            }
        }
    }
    return pos;
}
