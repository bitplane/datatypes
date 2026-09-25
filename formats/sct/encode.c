#include "encode.h"
#include <string.h>

#define SCT_DPI 72u

static void put_digits(uint8_t *p, unsigned long long v, unsigned n)
{
    while (n-- > 0) {
        p[n] = (uint8_t)('0' + v % 10u);
        v /= 10u;
    }
}

/* A 12-character Long: "+00000000512". */
static void put_long(uint8_t *p, unsigned v)
{
    p[0] = '+';
    put_digits(p + 1, v, 11);
}

static unsigned long long power10(int n)
{
    unsigned long long v = 1;
    while (n-- > 0)
        v *= 10u;
    return v;
}

/* A 14-character Float holding pixels / SCT_DPI to eight digits:
   "+.71111111E+01". pixels is 1 to 65535, so the exponent is -1 to 3. */
static void put_inches(uint8_t *p, unsigned pixels)
{
    unsigned long long scaled = (unsigned long long)pixels * power10(9) /
                                SCT_DPI;
    unsigned long long mantissa;
    int e = 3;

    /* Find e with 10^(e-1) <= pixels / SCT_DPI < 10^e. */
    while (scaled < power10(e + 8))
        e--;
    mantissa = ((unsigned long long)pixels * power10(8 - e) + SCT_DPI / 2u) /
               SCT_DPI;
    if (mantissa == power10(8)) {
        mantissa /= 10u;
        e++;
    }
    p[0] = '+';
    p[1] = '.';
    put_digits(p + 2, mantissa, 8);
    p[10] = 'E';
    p[11] = e < 0 ? '-' : '+';
    put_digits(p + 12, (unsigned long long)(e < 0 ? -e : e), 2);
}

int sct_make_header(unsigned width, unsigned height, int gray,
                    uint8_t header[SCT_HEADER_SIZE])
{
    uint8_t *par = header + 1024;

    if (header == NULL || width == 0 || height == 0 ||
        width > 65535u || height > 65535u)
        return 0;
    memset(header, 0, SCT_HEADER_SIZE);
    /* The name is space-padded ASCII; leave it blank. */
    memset(header, ' ', 80);
    header[80] = 'C';
    header[81] = 'T';
    par[0] = 1; /* inches */
    par[1] = gray ? 1 : 3;
    par[3] = gray ? 8 : 7; /* K, or C, M and Y */
    put_inches(par + 0x04, height);
    put_inches(par + 0x12, width);
    put_long(par + 0x20, height);
    put_long(par + 0x2c, width);
    return 1;
}

size_t sct_line_size(unsigned width, int gray)
{
    return (((size_t)width + 1u) & ~(size_t)1u) * (gray ? 1u : 3u);
}

static uint8_t over_white(const uint8_t *pixel, unsigned c)
{
    unsigned a = pixel[3];
    return (uint8_t)((pixel[c] * a + 255u * (255u - a) + 127u) / 255u);
}

int sct_row_is_gray(const uint8_t *rgba, unsigned width)
{
    unsigned x;

    for (x = 0; x < width; x++) {
        const uint8_t *p = rgba + x * 4u;
        uint8_t r = over_white(p, 0);
        if (over_white(p, 1) != r || over_white(p, 2) != r)
            return 0;
    }
    return 1;
}

/* Cyan, magenta and yellow at 255 - ink are the red, green and blue light;
   black alone is the gray level. */
void sct_encode_row(const uint8_t *rgba, unsigned width, int gray,
                    uint8_t *output)
{
    size_t row = ((size_t)width + 1u) & ~(size_t)1u;
    unsigned x, c, planes = gray ? 1u : 3u;

    for (c = 0; c < planes; c++) {
        uint8_t *dst = output + row * c;
        for (x = 0; x < width; x++)
            dst[x] = over_white(rgba + x * 4u, c);
        if (row > width)
            dst[width] = 0;
    }
}
