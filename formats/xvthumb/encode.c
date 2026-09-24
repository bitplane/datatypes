#include "encode.h"
#include <string.h>

static size_t put_decimal(char *p, unsigned v)
{
    char digits[10];
    size_t n = 0, i;

    do {
        digits[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v != 0);
    for (i = 0; i < n; i++)
        p[i] = digits[n - 1u - i];
    return n;
}

size_t xvthumb_make_header(unsigned width, unsigned height,
                           char output[XVTHUMB_HEADER_MAX])
{
    static const char start[] = "P7 332\n#END_OF_COMMENTS\n";
    size_t n = sizeof start - 1u;

    if (output == NULL || width == 0 || height == 0 ||
        width > 65535u || height > 65535u)
        return 0;
    /* netpbm needs #END_OF_COMMENTS, and a maxval of 255. */
    memcpy(output, start, n);
    n += put_decimal(output + n, width);
    output[n++] = ' ';
    n += put_decimal(output + n, height);
    memcpy(output + n, " 255\n", 5);
    return n + 5u;
}

static unsigned over_white(const uint8_t *pixel, unsigned channel)
{
    unsigned a = pixel[3];
    return (pixel[channel] * a + 255u * (255u - a) + 127u) / 255u;
}

/* The level whose decoded value, level * 255 / max rounded down, is nearest to
   v; a tie goes to the lower level. This is how pamtoxvmini picks it. */
static unsigned level(unsigned v, unsigned max)
{
    unsigned k = 0;

    while (k < max && v * 2u > k * 255u / max + (k + 1u) * 255u / max)
        k++;
    return k;
}

void xvthumb_encode_row(const uint8_t *rgba, unsigned width, uint8_t *output)
{
    unsigned x;

    for (x = 0; x < width; x++, rgba += 4) {
        unsigned r = level(over_white(rgba, 0), 7u);
        unsigned g = level(over_white(rgba, 1), 7u);
        unsigned b = level(over_white(rgba, 2), 3u);
        output[x] = (uint8_t)(r << 5 | g << 2 | b);
    }
}
