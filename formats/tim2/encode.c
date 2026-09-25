#include "encode.h"
#include <string.h>

/* The GS's pixel storage modes. */
#define PSMCT32 0u
#define PSMCT24 1u

static void put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, v & 0xffffu);
    put16(p + 2, v >> 16);
}

/* Texture sizes are powers of two up to 1024 on the GS. */
static uint32_t log2_up(unsigned n)
{
    uint32_t bits = 0;
    while (bits < 10u && (1u << bits) < n)
        bits++;
    return bits;
}

size_t tim2_row_size(unsigned width, int alpha)
{
    return (size_t)width * (alpha ? 4u : 3u);
}

int tim2_make_header(unsigned width, unsigned height, int alpha,
                     uint8_t header[TIM2_WRITE_HEADER])
{
    size_t row = tim2_row_size(width, alpha);
    uint32_t image, tbw, tex0_low, tex0_high;

    if (width == 0 || height == 0 || width > 65535u || height > 65535u ||
        row > (0xffffffffu - 48u) / height)
        return 0;
    image = (uint32_t)(row * height);
    memset(header, 0, TIM2_WRITE_HEADER);
    memcpy(header, "TIM2", 4);
    header[4] = 4; /* format version */
    header[5] = 0; /* 16-byte alignment */
    put16(header + 6, 1);
    put32(header + 16, 48u + image);
    put32(header + 20, 0); /* no CLUT */
    put32(header + 24, image);
    put16(header + 28, 48);
    put16(header + 30, 0);
    header[32] = 0; /* picture format */
    header[33] = 1; /* one level */
    header[34] = 0;
    header[35] = alpha ? 3 : 2;
    put16(header + 36, width);
    put16(header + 38, height);
    /* TEX0: buffer width in 64-pixel units, storage mode, log2 sizes, and
       whether the texture's alpha is used. */
    tbw = (width + 63u) / 64u;
    if (tbw > 63u)
        tbw = 63u;
    tex0_low = tbw << 14 | (alpha ? PSMCT32 : PSMCT24) << 20 |
               log2_up(width) << 26 | (log2_up(height) & 3u) << 30;
    tex0_high = log2_up(height) >> 2 | (alpha ? 1u : 0u) << 2;
    put32(header + 40, tex0_low);
    put32(header + 44, tex0_high);
    return 1;
}

void tim2_encode_row(const uint8_t *rgba, unsigned width, int alpha,
                     uint8_t *output)
{
    unsigned x;
    for (x = 0; x < width; x++, rgba += 4) {
        *output++ = rgba[0];
        *output++ = rgba[1];
        *output++ = rgba[2];
        /* 0x80 is opaque; halving rounds up so 255 stays opaque. */
        if (alpha)
            *output++ = (uint8_t)((rgba[3] + 1u) / 2u);
    }
}
