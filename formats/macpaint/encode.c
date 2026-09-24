#include "encode.h"
#include <string.h>

#define ROW_BYTES (MACPAINT_WIDTH / 8)

void macpaint_make_header(uint8_t header[MACPAINT_HEADER_SIZE])
{
    memset(header, 0, MACPAINT_HEADER_SIZE);
}

static unsigned over_white(const uint8_t *pixel, unsigned channel)
{
    unsigned a = pixel[3];
    return (pixel[channel] * a + 255u * (255u - a) + 127u) / 255u;
}

/* Runs of three or more repeat; anything else goes in literal blocks. */
static size_t pack(const uint8_t *in, size_t length, uint8_t *out)
{
    size_t i = 0, o = 0;

    while (i < length) {
        size_t run = 1, start = i;

        while (i + run < length && run < 128 && in[i + run] == in[i])
            run++;
        if (run >= 3) {
            out[o++] = (uint8_t)(257u - run);
            out[o++] = in[i];
            i += run;
            continue;
        }
        while (i < length && i - start < 128 &&
               !(i + 2 < length && in[i] == in[i + 1] && in[i] == in[i + 2]))
            i++;
        out[o++] = (uint8_t)(i - start - 1u);
        memcpy(out + o, in + start, i - start);
        o += i - start;
    }
    return o;
}

size_t macpaint_encode_row(const uint8_t *rgba, unsigned width, uint8_t *output, size_t capacity)
{
    uint8_t bits[ROW_BYTES];
    unsigned x;

    if (output == NULL || capacity < MACPAINT_ROW_MAX || (rgba == NULL && width != 0))
        return 0;
    if (width > MACPAINT_WIDTH)
        width = MACPAINT_WIDTH;
    memset(bits, 0, sizeof bits);
    for (x = 0; x < width; x++, rgba += 4) {
        unsigned luma = 77u * over_white(rgba, 0) + 150u * over_white(rgba, 1) +
                        29u * over_white(rgba, 2);
        if (luma < 128u * 256u)
            bits[x / 8u] |= (uint8_t)(0x80u >> (x % 8u));
    }
    return pack(bits, sizeof bits, output);
}
