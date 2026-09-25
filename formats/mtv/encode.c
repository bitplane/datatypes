#include "encode.h"

static size_t put_decimal(uint8_t *p, unsigned v)
{
    uint8_t digits[5];
    size_t n = 0, i;

    do {
        digits[n++] = (uint8_t)('0' + v % 10u);
        v /= 10u;
    } while (v != 0);
    for (i = 0; i < n; i++)
        p[i] = digits[n - 1 - i];
    return n;
}

size_t mtv_make_header(unsigned width, unsigned height,
                       uint8_t header[MTV_HEADER_CAPACITY])
{
    size_t n;

    if (header == NULL || width == 0 || height == 0 ||
        width > 65535u || height > 65535u)
        return 0;
    n = put_decimal(header, width);
    header[n++] = ' ';
    n += put_decimal(header + n, height);
    header[n++] = '\n';
    return n;
}

void mtv_encode_row(const uint8_t *rgba, unsigned width, uint8_t *output)
{
    size_t i;
    unsigned c, a;

    for (i = 0; i < width; i++) {
        a = rgba[i * 4u + 3u];
        for (c = 0; c < 3; c++)
            output[i * 3u + c] = (uint8_t)((rgba[i * 4u + c] * a +
                                            255u * (255u - a) + 127u) / 255u);
    }
}
