/* Maki-chan graphics: MAG (MAKI02) and its predecessor MKI (MAKI01A/B). */
#include <stdlib.h>
#include <string.h>

#include "japanpc.h"

#define MAG_HEADER 32u

/* Where a MAG copy flag takes its two bytes from: bytes left, lines up. */
static const uint8_t copy_x[16] = { 0, 2, 4, 8, 0, 2, 0, 2, 4, 0, 2, 4, 0, 2, 4, 0 };
static const uint8_t copy_y[16] = { 0, 0, 0, 0, 1, 1, 2, 2, 2, 4, 4, 4, 8, 8, 8, 16 };

static unsigned le16(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }
static unsigned be16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
static uint32_t le32(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
           (uint32_t)p[3] << 24;
}

/* What the header's machine code and mode say about the screen. */
enum mag_layout { MAG_INDEXED, MAG_YJK, MAG_YJK_PALETTE, MAG_SCREEN6 };

struct mag_screen {
    enum mag_layout layout;
    unsigned repeat_x, repeat_y;
};

static int mag_screen(unsigned machine, unsigned flags, unsigned mode,
                      struct mag_screen *s)
{
    s->layout = MAG_INDEXED;
    s->repeat_x = 1;
    /* The 200-line bit; in 256-colour modes that bit means something else. */
    s->repeat_y = (mode & 0x81u) == 1u ? 2 : 1;
    switch (machine) {
    case 0x03: /* MSX: the flags hold the screen mode */
        s->repeat_y = 1;
        switch (flags & 0xfcu) {
        case 0x00: case 0x14: case 0x54:
            break;
        case 0x04:
            s->repeat_y = 2;
            break;
        case 0x10: case 0x50:
            s->repeat_x = 2;
            break;
        case 0x20:
            s->layout = MAG_YJK_PALETTE;
            s->repeat_x = 2;
            break;
        case 0x24: /* screen 10 */
        case 0x34: /* screen 11 */
            s->layout = MAG_YJK_PALETTE;
            break;
        case 0x40:
            s->layout = MAG_YJK;
            s->repeat_x = 2;
            break;
        case 0x44:
            s->layout = MAG_YJK;
            break;
        case 0x60:
            s->layout = MAG_SCREEN6;
            break;
        case 0x64:
            s->layout = MAG_SCREEN6;
            s->repeat_y = 2;
            break;
        default:
            return 0;
        }
        break;
    case 0x80: /* PC-8001: always 200 lines */
        s->repeat_y = 2;
        break;
    }
    return 1;
}

/* Expand the flag and pixel streams into lines of stride bytes. */
static enum codec_result mag_unpack(const uint8_t *data, size_t size,
                                    size_t header, unsigned stride,
                                    unsigned height, uint8_t *out)
{
    struct jp_bits flag_a;
    uint32_t a = le32(data + header + 12), b = le32(data + header + 16),
             p = le32(data + header + 24);
    size_t flag_b, pixel;
    uint8_t *flags;
    unsigned x, y, units = stride / 4u;

    if (a >= size - header || b >= size - header || p >= size - header)
        return CODEC_TRUNCATED;
    jp_bits_init(&flag_a, data, header + a, size);
    flag_b = header + b;
    pixel = header + p;
    flags = calloc(units, 1);
    if (flags == NULL)
        return CODEC_NO_MEMORY;
    for (y = 0; y < height; y++) {
        uint8_t *line = out + (size_t)y * stride;
        for (x = 0; x < units; x++) {
            int bit = jp_bit(&flag_a);
            unsigned half;
            if (bit < 0)
                goto truncated;
            /* Each set flag A bit brings a flag B byte, XORed with the one
               from the line above. */
            if (bit) {
                if (flag_b >= size)
                    goto truncated;
                flags[x] ^= data[flag_b++];
            }
            for (half = 0; half < 2; half++) {
                unsigned code = half ? flags[x] & 15u : flags[x] >> 4;
                unsigned at = x * 4u + half * 2u;
                if (code == 0) {
                    if (size - pixel < 2)
                        goto truncated;
                    line[at] = data[pixel];
                    line[at + 1] = data[pixel + 1];
                    pixel += 2;
                } else {
                    const uint8_t *from;
                    if (at < copy_x[code] || y < copy_y[code]) {
                        free(flags);
                        return CODEC_INVALID;
                    }
                    from = line - (size_t)copy_y[code] * stride +
                           at - copy_x[code];
                    line[at] = from[0];
                    line[at + 1] = from[1];
                }
            }
        }
    }
    free(flags);
    return CODEC_OK;
truncated:
    free(flags);
    return CODEC_TRUNCATED;
}

static unsigned clamp5(int v) { return v < 0 ? 0 : v > 31 ? 31 : (unsigned)v; }

static uint32_t widen5(unsigned r, unsigned g, unsigned b)
{
    r = r << 3 | r >> 2;
    g = g << 3 | g >> 2;
    b = b << 3 | b >> 2;
    return (uint32_t)r << 16 | (uint32_t)g << 8 | b;
}

/* MSX2+ YJK: every 4 pixels share J and K in their low 3 bits. */
static uint32_t yjk(const uint8_t *line, unsigned x, unsigned width,
                    const uint32_t *palette, int yae)
{
    unsigned y = line[x] >> 3;
    const uint8_t *group;
    int j, k;

    if (yae && (y & 1u))
        return palette[y >> 1];
    if ((x | 3u) >= width)
        return y * 0x010101u;
    group = line + (x & ~3u);
    k = (group[0] & 7) | (group[1] & 7) << 3;
    j = (group[2] & 7) | (group[3] & 7) << 3;
    k -= (k & 0x20) << 1;
    j -= (j & 0x20) << 1;
    return widen5(clamp5((int)y + j), clamp5((int)y + k),
                  clamp5((5 * (int)y - 2 * j - k + 2) >> 2));
}

enum codec_result jp_decode_mag(const uint8_t *data, size_t size,
                                struct jp_canvas *canvas)
{
    size_t header = 8;
    unsigned x0, y0, x1, y1, colours, left, stride, width, height, x, y;
    struct mag_screen screen;
    uint32_t palette[256];
    uint8_t *pixels;
    enum codec_result result;
    const uint8_t *h;

    while (header < size && data[header] != 0x1a)
        header++;
    header++;
    if (header >= size || size - header < MAG_HEADER + 16u * 3u)
        return CODEC_TRUNCATED;
    h = data + header;
    if (h[0] != 0)
        return CODEC_INVALID;
    colours = h[3] & 0x80u ? 256 : 16;
    if (colours == 256 && size - header < MAG_HEADER + 256u * 3u)
        return CODEC_TRUNCATED;
    x0 = le16(h + 4), y0 = le16(h + 6), x1 = le16(h + 8), y1 = le16(h + 10);
    if (x1 < x0 || y1 < y0)
        return CODEC_INVALID;
    if (!mag_screen(h[1], h[2], h[3], &screen))
        return CODEC_INVALID;
    /* Writers fill the low bits of non-zero levels with ones, so the
       values are already scaled to 8 bits. */
    jp_palette(palette, h + MAG_HEADER, colours, 1, 0, JP_DAC_8);

    /* Lines are stored in whole flag units: 8 pixels of 16 colours or 4
       of 256, from the unit holding the left edge. */
    if (colours == 16) {
        left = x0 & 7u;
        stride = ((x1 | 7u) - (x0 & ~7u) + 1u) / 2u;
    } else {
        left = x0 & 3u;
        stride = (x1 | 3u) - (x0 & ~3u) + 1u;
    }
    width = x1 - x0 + 1u;
    height = y1 - y0 + 1u;
    switch (screen.layout) {
    case MAG_INDEXED:
        break;
    case MAG_YJK:
    case MAG_YJK_PALETTE:
        /* One byte per pixel, whatever the colour count says. */
        width = colours == 16 ? (x1 + 1u - (x0 & ~7u)) / 2u
                              : x1 + 1u - (x0 & ~3u);
        left = 0;
        break;
    case MAG_SCREEN6:
        width = (colours == 16 ? (x1 + 2u - (x0 & ~7u)) / 2u
                               : x1 + 1u - (x0 & ~3u)) * 4u;
        left = 0;
        break;
    }
    if (width == 0)
        return CODEC_INVALID;
    result = jp_canvas_init(canvas, width, height, screen.repeat_x,
                            screen.repeat_y);
    if (result != CODEC_OK)
        return result;
    pixels = malloc((size_t)stride * height);
    if (pixels == NULL)
        return CODEC_NO_MEMORY;
    result = mag_unpack(data, size, header, stride, height, pixels);
    if (result != CODEC_OK) {
        free(pixels);
        return result;
    }

    for (y = 0; y < height; y++) {
        const uint8_t *line = pixels + (size_t)y * stride;
        uint32_t *out = canvas->rgb + (size_t)y * width;
        for (x = 0; x < width; x++) {
            unsigned at = x + left;
            switch (screen.layout) {
            case MAG_INDEXED:
                out[x] = colours == 256 ? palette[line[at]]
                       : palette[at & 1u ? line[at / 2u] & 15u
                                         : line[at / 2u] >> 4];
                break;
            case MAG_YJK:
            case MAG_YJK_PALETTE:
                out[x] = yjk(line, x, width, palette,
                             screen.layout == MAG_YJK_PALETTE);
                break;
            case MAG_SCREEN6:
                out[x] = palette[line[x / 4u] >> (6u - x % 4u * 2u) & 3u];
                break;
            }
        }
    }
    free(pixels);
    return CODEC_OK;
}

#define MKI_WIDTH 640u
#define MKI_HEIGHT 400u
#define MKI_BYTES (MKI_WIDTH / 2u)
#define MKI_BLOCKS (MKI_HEIGHT / 4u * (MKI_BYTES / 4u))
#define MKI_FLAG_A 96u
#define MKI_DATA (MKI_FLAG_A + MKI_BLOCKS / 8u)

enum codec_result jp_decode_mki(const uint8_t *data, size_t size,
                                struct jp_canvas *canvas)
{
    static const uint8_t full_screen[8] = { 0, 0, 0, 0, 0x02, 0x80, 0x01, 0x90 };
    uint32_t palette[16];
    uint16_t *blocks;
    uint8_t lines[4][MKI_BYTES];
    size_t pos = MKI_DATA;
    unsigned i, x, y, repeat_y;
    unsigned up = data[6] == 'A' ? 2 : 0; /* line XORed with: 2 or 4 up */
    enum codec_result result;
    enum jp_dac dac;

    if (size < MKI_DATA)
        return CODEC_TRUNCATED;
    /* Only the 640x400 screen is known. */
    if (memcmp(data + 40, full_screen, sizeof full_screen) != 0)
        return CODEC_INVALID;
    dac = jp_machine_dac(data + 8, 0, 16, &repeat_y);
    jp_palette(palette, data + 48, 16, 1, 0, dac);
    result = jp_canvas_init(canvas, MKI_WIDTH, MKI_HEIGHT, 1, 1);
    if (result != CODEC_OK)
        return result;
    blocks = malloc(MKI_BLOCKS * sizeof *blocks);
    if (blocks == NULL)
        return CODEC_NO_MEMORY;

    /* Flag A: a bit per 4x4-byte block says whether it has a flag B word,
       whose bits say which of its 16 bytes change. */
    for (i = 0; i < MKI_BLOCKS; i++) {
        blocks[i] = 0;
        if (data[MKI_FLAG_A + i / 8u] >> (7u - i % 8u) & 1u) {
            if (size - pos < 2)
                goto truncated;
            blocks[i] = (uint16_t)be16(data + pos);
            pos += 2;
        }
    }
    memset(lines, 0, sizeof lines);
    for (y = 0; y < MKI_HEIGHT; y++) {
        unsigned row = y & 3u;
        const uint8_t *above = lines[row ^ up];
        uint32_t *out = canvas->rgb + (size_t)y * MKI_WIDTH;
        for (x = 0; x < MKI_BYTES; x++) {
            unsigned block = y / 4u * (MKI_BYTES / 4u) + x / 4u;
            uint8_t value = above[x];
            if (blocks[block] >> (15u - row * 4u - x % 4u) & 1u) {
                if (pos >= size)
                    goto truncated;
                value ^= data[pos++];
            }
            lines[row][x] = value;
            out[x * 2u] = palette[value >> 4];
            out[x * 2u + 1u] = palette[value & 15u];
        }
    }
    free(blocks);
    return CODEC_OK;
truncated:
    free(blocks);
    return CODEC_TRUNCATED;
}
