#include "encode.h"

#include <string.h>

static void put16(uint8_t *p, unsigned value)
{
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static void put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

unsigned sgi_row_needs(const uint8_t *rgba, unsigned width)
{
    unsigned needs = 0, x;
    for (x = 0; x < width; x++, rgba += 4) {
        if (rgba[0] != rgba[1] || rgba[0] != rgba[2])
            needs |= SGI_NEEDS_COLOR;
        if (rgba[3] != 255)
            needs |= SGI_NEEDS_ALPHA;
    }
    return needs;
}

unsigned sgi_channels(unsigned needs)
{
    /* Gray with alpha is saved as RGBA: two-channel files are not in the
       specification and many readers reject them. */
    return needs & SGI_NEEDS_ALPHA ? 4u : needs & SGI_NEEDS_COLOR ? 3u : 1u;
}

int sgi_make_header(unsigned width, unsigned height, unsigned channels,
                    uint8_t header[SGI_HEADER_SIZE])
{
    if (width == 0 || height == 0 || width > 65535u || height > 65535u ||
        channels == 0 || channels == 2 || channels > 4)
        return 0;
    memset(header, 0, SGI_HEADER_SIZE);
    put16(header, 474);
    header[2] = 1; /* RLE */
    header[3] = 1; /* one byte per channel */
    put16(header + 4, channels == 1 ? 2u : 3u);
    put16(header + 6, width);
    put16(header + 8, height);
    put16(header + 10, channels);
    put32(header + 16, 255); /* PIXMAX; PIXMIN and COLORMAP stay zero */
    return 1;
}

size_t sgi_rle_capacity(unsigned width)
{
    /* Literal packets of up to 127 values, then a terminator. */
    return (size_t)width + width / 127u + 2u;
}

static unsigned run_at(const uint8_t *rgba, unsigned width, unsigned x)
{
    unsigned run = 1;
    while (x + run < width && run < 127u &&
           rgba[(size_t)(x + run) * 4u] == rgba[(size_t)x * 4u])
        run++;
    return run;
}

size_t sgi_encode_rle(const uint8_t *rgba, unsigned width, unsigned channel,
                      uint8_t *output, size_t capacity)
{
    size_t pos = 0;
    unsigned x = 0;

    if (rgba == NULL || output == NULL || channel > 3 ||
        capacity < sgi_rle_capacity(width))
        return 0;
    rgba += channel;
    while (x < width) {
        unsigned run = run_at(rgba, width, x);
        if (run >= 3) {
            output[pos++] = (uint8_t)run;
            output[pos++] = rgba[(size_t)x * 4u];
            x += run;
        } else {
            /* Collect literals until a run long enough to pay for itself. */
            unsigned start = x;
            do
                x++;
            while (x < width && x - start < 127u && run_at(rgba, width, x) < 3);
            output[pos++] = (uint8_t)(0x80u | (x - start));
            for (; start < x; start++)
                output[pos++] = rgba[(size_t)start * 4u];
        }
    }
    output[pos++] = 0;
    return pos;
}

int sgi_make_tables(const uint32_t *lengths, unsigned height, unsigned channels,
                    uint8_t *tables)
{
    const size_t rows = (size_t)height * channels;
    uint64_t offset = SGI_HEADER_SIZE + (uint64_t)rows * 8u;
    unsigned y, c;

    if (channels == 0 || channels == 2 || channels > 4)
        return 0;
    for (y = 0; y < height; y++) {
        for (c = 0; c < channels; c++) {
            /* Tables index scan lines bottom up, one channel after another. */
            size_t i = (size_t)c * height + (height - 1u - y);
            uint32_t length = lengths[(size_t)y * 4u + c];
            if (offset + length > UINT32_MAX)
                return 0;
            put32(tables + i * 4u, (uint32_t)offset);
            put32(tables + (rows + i) * 4u, length);
            offset += length;
        }
    }
    return 1;
}
