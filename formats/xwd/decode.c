#include "decode.h"
#include <stdlib.h>

#define XWD_VERSION 7u
#define XWD_HEADER 100u
#define XWD_COLOR 12u
#define XWD_MAX_PIXELS (16u * 1024u * 1024u)

enum { XY_BITMAP, XY_PIXMAP, Z_PIXMAP };
enum { STATIC_GRAY, GRAY_SCALE, STATIC_COLOR, PSEUDO_COLOR, TRUE_COLOR, DIRECT_COLOR };
enum { LSB_FIRST, MSB_FIRST };
enum {
    H_SIZE, H_VERSION, H_FORMAT, H_DEPTH, H_WIDTH, H_HEIGHT, H_XOFFSET,
    H_BYTE_ORDER, H_UNIT, H_BIT_ORDER, H_PAD, H_BPP, H_BPL, H_VISUAL,
    H_RED, H_GREEN, H_BLUE, H_BITS_PER_RGB, H_CMAP_ENTRIES, H_NCOLORS,
    H_FIELDS = 25
};

struct layout {
    const uint8_t *raster, *colors;
    unsigned long h[H_FIELDS];
    size_t bpl, plane;
    int big;
};

static unsigned long get32(const uint8_t *p, int big)
{
    if (big)
        return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) |
               ((unsigned long)p[2] << 8) | (unsigned long)p[3];
    return ((unsigned long)p[3] << 24) | ((unsigned long)p[2] << 16) |
           ((unsigned long)p[1] << 8) | (unsigned long)p[0];
}

static unsigned get16(const uint8_t *p, int big)
{
    return big ? (unsigned)p[0] << 8 | p[1] : (unsigned)p[1] << 8 | p[0];
}

/* X colours are 16-bit; round to 8. */
static uint8_t scale16(unsigned value)
{
    return (uint8_t)((value + 128u) / 257u);
}

static int valid_unit(unsigned long unit)
{
    return unit == 8 || unit == 16 || unit == 32;
}

static uint64_t round_up(uint64_t bits, unsigned long unit)
{
    return (bits + unit - 1u) / unit * unit;
}

/* Bit b of a scanline of bitmap units. The unit's bytes are in byte_order,
   and bit_order says whether the leftmost pixel is its top or bottom bit. */
static unsigned get_bit(const struct layout *l, const uint8_t *row, uint64_t b)
{
    unsigned long unit = l->h[H_UNIT];
    size_t base = (size_t)(b / unit) * (unit / 8u);
    unsigned k = (unsigned)(b % unit);
    unsigned s = l->h[H_BIT_ORDER] == MSB_FIRST ? (unsigned)unit - 1u - k : k;
    size_t byte = l->h[H_BYTE_ORDER] == MSB_FIRST ? unit / 8u - 1u - s / 8u : s / 8u;
    return (row[base + byte] >> (s % 8u)) & 1u;
}

static unsigned long get_pixel(const struct layout *l, size_t x, size_t y)
{
    unsigned long depth = l->h[H_DEPTH], bpp = l->h[H_BPP], value = 0;
    const uint8_t *row = l->raster + y * l->bpl;
    uint64_t b = (uint64_t)x + l->h[H_XOFFSET];
    unsigned long plane;

    if (l->h[H_FORMAT] != Z_PIXMAP) {
        /* Planes run from the most significant bit down. */
        for (plane = 0; plane < depth; plane++)
            value = value << 1 | get_bit(l, row + plane * l->plane, b);
        return value;
    }
    if (bpp == 1)
        return get_bit(l, row, b);
    if (bpp == 4) {
        unsigned byte = row[x / 2u];
        int high = (x % 2u == 0) == (l->h[H_BYTE_ORDER] == MSB_FIRST);
        value = high ? byte >> 4 : byte & 15u;
    } else {
        size_t n = bpp / 8u, i;
        const uint8_t *p = row + x * n;
        for (i = 0; i < n; i++)
            value |= (unsigned long)(l->h[H_BYTE_ORDER] == MSB_FIRST ?
                                     p[i] : p[n - 1u - i]) << (8u * (n - 1u - i));
    }
    if (depth < 32)
        value &= (1ul << depth) - 1u;
    return value;
}

/* Scale the bits of value under mask to an X colour, then to 0-255. */
static uint8_t masked(unsigned long value, unsigned long mask)
{
    if (mask == 0)
        return 0;
    while ((mask & 1u) == 0) {
        mask >>= 1;
        value >>= 1;
    }
    value &= mask;
    return scale16((unsigned)((uint64_t)value * 65535u / mask));
}

static enum codec_result convert(const struct layout *l, uint8_t *rgba)
{
    unsigned long visual = l->h[H_VISUAL], ncolors = l->h[H_NCOLORS];
    unsigned long depth = l->h[H_DEPTH];
    unsigned long red = l->h[H_RED], green = l->h[H_GREEN], blue = l->h[H_BLUE];
    size_t width = l->h[H_WIDTH], height = l->h[H_HEIGHT], x, y;
    int direct = visual == TRUE_COLOR || visual == DIRECT_COLOR;

    if (direct && red == 0 && green == 0 && blue == 0)
        return CODEC_INVALID;
    if (!direct && ncolors == 0 && visual != STATIC_GRAY && visual != GRAY_SCALE)
        return CODEC_INVALID;
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++, rgba += 4) {
            unsigned long pixel = get_pixel(l, x, y);
            rgba[3] = 255;
            if (!direct && ncolors == 0) {
                /* A gray visual without a colormap is a ramp; 1-bit is 0 white. */
                uint8_t gray = depth == 1 ? (pixel ? 0 : 255) :
                               masked(pixel, 0xfffffffful >> (32u - depth));
                rgba[0] = rgba[1] = rgba[2] = gray;
            } else if (ncolors == 0) {
                rgba[0] = masked(pixel, red);
                rgba[1] = masked(pixel, green);
                rgba[2] = masked(pixel, blue);
            } else if (!direct) {
                const uint8_t *c;
                if (pixel >= ncolors)
                    return CODEC_INVALID;
                c = l->colors + (size_t)pixel * XWD_COLOR;
                rgba[0] = scale16(get16(c + 4, l->big));
                rgba[1] = scale16(get16(c + 6, l->big));
                rgba[2] = scale16(get16(c + 8, l->big));
            } else {
                /* Each field indexes its own colormap column. A server's colormap
                   gives the true colours of TrueColor fields too. */
                const unsigned long masks[3] = {red, green, blue};
                unsigned i;
                for (i = 0; i < 3; i++) {
                    unsigned long mask = masks[i], field = pixel & mask;
                    if (mask != 0)
                        while ((mask & 1u) == 0) {
                            mask >>= 1;
                            field >>= 1;
                        }
                    if (field >= ncolors)
                        return CODEC_INVALID;
                    rgba[i] = scale16(get16(l->colors + (size_t)field * XWD_COLOR +
                                            4u + 2u * i, l->big));
                }
            }
        }
    }
    return CODEC_OK;
}

void xwd_free(struct xwd_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
    image->width = image->height = 0;
}

enum codec_result xwd_decode(const uint8_t *data, size_t length,
                             struct xwd_image *image)
{
    struct layout l;
    unsigned long *h = l.h, depth, bpp;
    uint64_t bits, minimum, offset, size;
    enum codec_result result;
    unsigned i;

    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->rgba = NULL;
    if (data == NULL || length < 8)
        return CODEC_TRUNCATED;
    /* xwd writes the header big-endian, but some writers use their own order. */
    if (get32(data + 4, 1) == XWD_VERSION)
        l.big = 1;
    else if (get32(data + 4, 0) == XWD_VERSION)
        l.big = 0;
    else
        return CODEC_INVALID;
    if (length < XWD_HEADER)
        return CODEC_TRUNCATED;
    for (i = 0; i < H_FIELDS; i++)
        h[i] = get32(data + 4u * i, l.big);
    depth = h[H_DEPTH]; bpp = h[H_BPP];
    if (h[H_SIZE] < XWD_HEADER || h[H_FORMAT] > Z_PIXMAP ||
        h[H_VISUAL] > DIRECT_COLOR || h[H_BYTE_ORDER] > MSB_FIRST ||
        h[H_BIT_ORDER] > MSB_FIRST || !valid_unit(h[H_UNIT]) ||
        !valid_unit(h[H_PAD]) || depth == 0 || depth > 32 ||
        (h[H_FORMAT] == XY_BITMAP && depth != 1) ||
        h[H_WIDTH] == 0 || h[H_HEIGHT] == 0)
        return CODEC_INVALID;
    if (h[H_FORMAT] == Z_PIXMAP) {
        if ((bpp != 1 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 &&
             bpp != 32) || bpp < depth)
            return CODEC_INVALID;
        if (bpp != 1)
            h[H_XOFFSET] = 0;
    }
    if (h[H_WIDTH] > 65535u || h[H_HEIGHT] > 65535u ||
        (uint64_t)h[H_WIDTH] * h[H_HEIGHT] > XWD_MAX_PIXELS)
        return CODEC_TOO_LARGE;

    /* Rows must hold every pixel, padded as the header says. */
    if (h[H_FORMAT] != Z_PIXMAP || bpp == 1) {
        bits = (uint64_t)h[H_WIDTH] + h[H_XOFFSET];
        minimum = round_up(round_up(bits, h[H_UNIT]), h[H_PAD]) / 8u;
    } else {
        minimum = round_up((uint64_t)h[H_WIDTH] * bpp, h[H_PAD]) / 8u;
    }
    if (h[H_BPL] == 0)
        h[H_BPL] = (unsigned long)(minimum > 0xfffffffful ? 0 : minimum);
    if (h[H_BPL] < minimum)
        return CODEC_INVALID;

    offset = (uint64_t)h[H_SIZE] + (uint64_t)h[H_NCOLORS] * XWD_COLOR;
    size = (uint64_t)h[H_BPL] * h[H_HEIGHT];
    if (h[H_FORMAT] == XY_PIXMAP)
        size *= depth;
    if (offset > length || size > length - offset)
        return CODEC_TRUNCATED;
    l.colors = data + h[H_SIZE];
    l.raster = data + (size_t)offset;
    l.bpl = h[H_BPL];
    l.plane = l.bpl * h[H_HEIGHT];

    image->rgba = malloc((size_t)h[H_WIDTH] * h[H_HEIGHT] * 4u);
    if (image->rgba == NULL)
        return CODEC_NO_MEMORY;
    image->width = (unsigned)h[H_WIDTH];
    image->height = (unsigned)h[H_HEIGHT];
    result = convert(&l, image->rgba);
    if (result != CODEC_OK)
        xwd_free(image);
    return result;
}
