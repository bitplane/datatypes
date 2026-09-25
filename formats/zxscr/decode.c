#include "decode.h"
#include <stdlib.h>
#include <string.h>

#define BITMAP_SIZE 6144u
#define ULAPLUS_SIZE (ZXSCR_FILE_SIZE + 64u)
#define IFL_SIZE (BITMAP_SIZE + 3072u)
#define BSC_SIZE (ZXSCR_FILE_SIZE + 4224u)
#define BMC4_SIZE (BITMAP_SIZE + 1536u + 4224u)
#define HIRES_SIZE (2u * BITMAP_SIZE + 1u)
#define ULAPLUS_HICOLOUR_SIZE (ZXSCR_TIMEX_SIZE + 64u)
#define GIGASCREEN_SIZE (2u * ZXSCR_FILE_SIZE)
#define HRG_SIZE (2u * HIRES_SIZE)
#define MG_HEADER 256u
#define MG1_SIZE (MG_HEADER + 2u * BITMAP_SIZE + 2u * 3072u + 2u * 384u)
#define SXG_HEADER 16u

/* How a screen finds the attribute byte for a pixel. */
enum attrs {
    ATTRS_ROWS,  /* linear, one row of 32 for every 1 << shift pixel rows */
    ATTRS_TIMEX, /* one per 8x1 span, interleaved like the bitmap */
    ATTRS_MG1,   /* 8x1 in columns 8-23, 8x8 outside, 16 per row each */
    ATTRS_NONE   /* bitmap only: black ink on white paper */
};

struct screen {
    const uint8_t *bitmap;
    int linear;                /* bitmap rows in order, not interleaved */
    enum attrs attrs;
    unsigned shift;            /* ATTRS_ROWS: 0 (8x1) to 3 (8x8) */
    const uint8_t *attr, *mid; /* mid: ATTRS_MG1's 8x1 columns */
};

/* 64 colours in ULAplus order: 16 per flash/bright pair, ink 0-7, paper 8-15. */
typedef uint8_t palette[64][3];

void zxscr_colour(unsigned colour, int bright, uint8_t rgb[3])
{
    /* ImageMagick's levels; emulators vary between 0xC0 and 0xD7. */
    uint8_t on = bright ? 255 : 192;

    rgb[0] = (colour & 2u) ? on : 0;
    rgb[1] = (colour & 4u) ? on : 0;
    rgb[2] = (colour & 1u) ? on : 0;
}

size_t zxscr_offset(unsigned x, unsigned y)
{
    /* Thirds of the screen, then pixel row within a cell, then cell row. */
    return ((size_t)(y & 0xC0u) << 5) | ((y & 7u) << 8) | ((y & 0x38u) << 2) | (x >> 3);
}

static int lower(char c)
{
    return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}

static int ends_with(const char *name, size_t length, const char *suffix)
{
    size_t n = strlen(suffix), i;

    if (length < n)
        return 0;
    for (i = 0; i < n; i++)
        if (lower(name[length - n + i]) != suffix[i])
            return 0;
    return 1;
}

enum zxscr_name zxscr_name_kind(const char *name)
{
    size_t length;

    if (name == NULL)
        return ZXSCR_NAME_OTHER;
    length = strlen(name);
    if (ends_with(name, length, ".mc"))
        return ZXSCR_NAME_MC;
    if (ends_with(name, length, ".mlt"))
        return ZXSCR_NAME_MLT;
    return ZXSCR_NAME_OTHER;
}

void zxscr_free(struct zxscr_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
    image->width = image->height = 0;
}

static void spectrum_palette(palette p)
{
    unsigned i;

    /* Flash doesn't change the first phase; bit 4 is bright. */
    for (i = 0; i < 64; i++)
        zxscr_colour(i & 7u, (i & 0x10u) != 0, p[i]);
}

static uint8_t level3(unsigned v)
{
    return (uint8_t)(v << 5 | v << 2 | v >> 1);
}

static void ulaplus_palette(const uint8_t *src, palette p)
{
    unsigned i;

    /* GGGRRRBB. Blue's missing low bit is the OR of the other two. */
    for (i = 0; i < 64; i++) {
        unsigned c = src[i], b = c & 3u;
        p[i][0] = level3(c >> 2 & 7u);
        p[i][1] = level3(c >> 5);
        p[i][2] = level3(b ? b << 1 | 1u : 0);
    }
}

static unsigned attribute(const struct screen *s, unsigned col, unsigned y)
{
    switch (s->attrs) {
    case ATTRS_TIMEX:
        return s->attr[zxscr_offset(col * 8u, y)];
    case ATTRS_MG1:
        if (col < 8u)
            return s->attr[(y >> 3) * 16u + col];
        if (col >= 24u)
            return s->attr[(y >> 3) * 16u + col - 16u];
        return s->mid[y * 16u + col - 8u];
    case ATTRS_NONE:
        return 7u << 3;
    default:
        return s->attr[(y >> s->shift) * 32u + col];
    }
}

/* Draw a 256x192 screen at rgba, whose rows are stride bytes apart. */
static void draw(const struct screen *s, const palette p, uint8_t *rgba, size_t stride)
{
    unsigned x, y;

    for (y = 0; y < ZXSCR_HEIGHT; y++) {
        uint8_t *dst = rgba + y * stride;
        for (x = 0; x < ZXSCR_WIDTH; x++) {
            unsigned col = x >> 3;
            unsigned bits = s->linear ? s->bitmap[y * 32u + col] : s->bitmap[zxscr_offset(x, y)];
            unsigned a = attribute(s, col, y);
            unsigned index = (a >> 2 & 0x30u) |
                ((bits >> (7u - x % 8u) & 1u) ? a & 7u : 8u | (a >> 3 & 7u));
            memcpy(dst, p[index], 3);
            dst[3] = 255;
            dst += 4;
        }
    }
}

static enum codec_result allocate(struct zxscr_image *image, unsigned width, unsigned height)
{
    image->rgba = malloc((size_t)width * height * 4u);
    if (image->rgba == NULL)
        return CODEC_NO_MEMORY;
    image->width = width;
    image->height = height;
    return CODEC_OK;
}

static enum codec_result single(const struct screen *s, const palette p,
                                struct zxscr_image *image)
{
    enum codec_result result = allocate(image, ZXSCR_WIDTH, ZXSCR_HEIGHT);

    if (result == CODEC_OK)
        draw(s, p, image->rgba, ZXSCR_WIDTH * 4u);
    return result;
}

/* Gigascreen: the eye sees the average of two alternating frames. */
static void blend(uint8_t *rgba, const uint8_t *other, size_t pixels)
{
    size_t i;

    for (i = 0; i < pixels * 4u; i++)
        rgba[i] = (uint8_t)((rgba[i] + other[i]) >> 1);
}

static enum codec_result pair(const struct screen *s1, const struct screen *s2,
                              struct zxscr_image *image)
{
    size_t pixels = (size_t)ZXSCR_WIDTH * ZXSCR_HEIGHT;
    palette p;
    uint8_t *second;
    enum codec_result result;

    second = malloc(pixels * 4u);
    if (second == NULL)
        return CODEC_NO_MEMORY;
    spectrum_palette(p);
    result = single(s1, p, image);
    if (result == CODEC_OK) {
        draw(s2, p, second, ZXSCR_WIDTH * 4u);
        blend(image->rgba, second, pixels);
    }
    free(second);
    return result;
}

static enum codec_result plain(const uint8_t *data, int linear, enum attrs attrs,
                               unsigned shift, struct zxscr_image *image)
{
    struct screen s;
    palette p;

    s.bitmap = data;
    s.linear = linear;
    s.attrs = attrs;
    s.shift = shift;
    s.attr = data + BITMAP_SIZE;
    s.mid = NULL;
    spectrum_palette(p);
    return single(&s, p, image);
}

static enum codec_result ulaplus(const uint8_t *data, enum attrs attrs,
                                 size_t palette_offset, struct zxscr_image *image)
{
    struct screen s;
    palette p;

    s.bitmap = data;
    s.linear = 0;
    s.attrs = attrs;
    s.shift = 3;
    s.attr = data + BITMAP_SIZE;
    s.mid = NULL;
    ulaplus_palette(data + palette_offset, p);
    return single(&s, p, image);
}

/* Timex 512x192 hi-res: columns alternate between two bitmaps, and the
   last byte picks the ink; paper is its complement. Rows are doubled so
   the picture keeps its shape. */
static void hires(const uint8_t *data, uint8_t *rgba)
{
    unsigned ink = data[2u * BITMAP_SIZE] >> 3 & 7u, x, y;
    uint8_t colours[2][3];

    zxscr_colour(7u - ink, 1, colours[0]);
    zxscr_colour(ink, 1, colours[1]);
    for (y = 0; y < ZXSCR_HEIGHT; y++) {
        uint8_t *dst = rgba + (size_t)y * 2u * 512u * 4u;
        for (x = 0; x < 512u; x++) {
            unsigned bits = data[(x >> 3 & 1u) * BITMAP_SIZE + zxscr_offset(x >> 4 << 3, y)];
            memcpy(dst + x * 4u, colours[bits >> (7u - x % 8u) & 1u], 3);
            dst[x * 4u + 3u] = 255;
        }
        memcpy(dst + 512u * 4u, dst, 512u * 4u);
    }
}

static enum codec_result decode_hires(const uint8_t *data, int frames,
                                      struct zxscr_image *image)
{
    size_t pixels = 512u * 384u;
    enum codec_result result = allocate(image, 512u, 384u);
    uint8_t *second;

    if (result != CODEC_OK)
        return result;
    hires(data, image->rgba);
    if (frames == 1)
        return CODEC_OK;
    second = malloc(pixels * 4u);
    if (second == NULL) {
        zxscr_free(image);
        return CODEC_NO_MEMORY;
    }
    hires(data + HIRES_SIZE, second);
    blend(image->rgba, second, pixels);
    free(second);
    return CODEC_OK;
}

/* A screen inside a 384x304 border. The border is drawn in 8-pixel spans,
   two to a byte (low bits first), never bright. */
static enum codec_result decode_bsc(const uint8_t *data, size_t length,
                                    struct zxscr_image *image)
{
    const uint8_t *border = data + length - 4224u;
    uint8_t table[1536];
    struct screen s;
    palette p;
    unsigned x, y, span = 0;
    enum codec_result result = allocate(image, 384u, 304u);

    if (result != CODEC_OK)
        return result;
    s.bitmap = data;
    s.linear = 0;
    s.attrs = ATTRS_ROWS;
    s.shift = 3;
    s.attr = data + BITMAP_SIZE;
    s.mid = NULL;
    spectrum_palette(p);
    /* BMC4 has 8x4 attributes as two 8x8 tables, the cells' top halves
       first. Interleave them into one linear table. */
    if (length == BMC4_SIZE) {
        for (y = 0; y < 48u; y++)
            memcpy(table + y * 32u, s.attr + (y & 1u ? 768u : 0) + (y >> 1) * 32u, 32);
        s.attr = table;
        s.shift = 2;
    }
    draw(&s, p, image->rgba + (64u * 384u + 64u) * 4u, 384u * 4u);
    for (y = 0; y < 304u; y++)
        for (x = 0; x < 384u; x += 8u) {
            uint8_t *dst = image->rgba + ((size_t)y * 384u + x) * 4u;
            unsigned i, colour;
            if (y >= 64u && y < 256u && x >= 64u && x < 320u)
                continue;
            colour = border[span >> 1] >> (span & 1u ? 3 : 0) & 7u;
            span++;
            for (i = 0; i < 8u; i++) {
                zxscr_colour(colour, 0, dst + i * 4u);
                dst[i * 4u + 3u] = 255;
            }
        }
    return CODEC_OK;
}

/* MultiArtist Gigascreen: "MGH", version 1, attribute cell height, then
   two bitmaps and two attribute tables after a 256-byte header. */
static enum codec_result decode_mg(const uint8_t *data, size_t length,
                                   struct zxscr_image *image)
{
    struct screen s1, s2;
    unsigned shift;
    size_t size, table;

    if (data[3] != 1)
        return CODEC_INVALID;
    switch (data[4]) {
    case 1: shift = 0; break;
    case 2: shift = 1; break;
    case 4: shift = 2; break;
    case 8: shift = 3; break;
    default: return CODEC_INVALID;
    }
    table = BITMAP_SIZE >> shift;
    size = data[4] == 1 ? MG1_SIZE : MG_HEADER + 2u * BITMAP_SIZE + 2u * table;
    if (length < size)
        return CODEC_TRUNCATED;
    if (length > size)
        return CODEC_INVALID;
    s1.bitmap = data + MG_HEADER;
    s2.bitmap = s1.bitmap + BITMAP_SIZE;
    s1.linear = s2.linear = 0;
    s1.shift = s2.shift = shift;
    if (data[4] == 1) {
        /* 8x1 tables for the middle 16 columns, then 8x8 ones for the rest. */
        s1.attrs = s2.attrs = ATTRS_MG1;
        s1.mid = s2.bitmap + BITMAP_SIZE;
        s2.mid = s1.mid + 3072u;
        s1.attr = s2.mid + 3072u;
        s2.attr = s1.attr + 384u;
    } else {
        s1.attrs = s2.attrs = ATTRS_ROWS;
        s1.attr = s2.bitmap + BITMAP_SIZE;
        s2.attr = s1.attr + table;
        s1.mid = s2.mid = NULL;
    }
    return pair(&s1, &s2, image);
}

static unsigned word(const uint8_t *p)
{
    return p[0] | (unsigned)p[1] << 8;
}

static uint8_t level5(unsigned v)
{
    return (uint8_t)(v << 3 | v >> 2);
}

/* Speccy eXtended Graphics (ZX Evolution): a header, a palette of 16-bit
   entries and 4- or 8-bit indexed pixels. */
static enum codec_result decode_sxg(const uint8_t *data, size_t length,
                                    struct zxscr_image *image)
{
    uint8_t colours[256][3];
    unsigned width, height, format, count, i, x, y;
    size_t palette_offset, bitmap_offset, stride;
    enum codec_result result;

    if (length < SXG_HEADER)
        return CODEC_TRUNCATED;
    format = data[7];
    width = word(data + 8);
    height = word(data + 10);
    palette_offset = 14u + word(data + 12);
    bitmap_offset = 16u + word(data + 14);
    /* Only unpacked 16- and 256-colour pictures are defined. */
    if (data[6] != 0 || (format != 1 && format != 2))
        return CODEC_INVALID;
    if (width == 0 || height == 0 || (size_t)width * height > ZXSCR_MAX_PIXELS)
        return CODEC_INVALID;
    if (bitmap_offset < palette_offset || bitmap_offset - palette_offset > 512u ||
        (bitmap_offset - palette_offset) % 2u != 0)
        return CODEC_INVALID;
    stride = format == 1 ? (width + 1u) / 2u : width;
    if (length < bitmap_offset || length - bitmap_offset < stride * height)
        return CODEC_TRUNCATED;

    /* Missing entries are black. */
    memset(colours, 0, sizeof colours);
    count = (unsigned)(bitmap_offset - palette_offset) / 2u;
    for (i = 0; i < count; i++) {
        unsigned c = word(data + palette_offset + i * 2u);
        unsigned r = c >> 10 & 0x1Fu, g = c >> 5 & 0x1Fu, b = c & 0x1Fu;
        if (c & 0x8000u) {
            colours[i][0] = level5(r);
            colours[i][1] = level5(g);
            colours[i][2] = level5(b);
        } else {
            /* The ZX Evolution's own format: 25 levels per component. */
            if (r > 24u || g > 24u || b > 24u)
                return CODEC_INVALID;
            colours[i][0] = (uint8_t)(r * 255u / 24u);
            colours[i][1] = (uint8_t)(g * 255u / 24u);
            colours[i][2] = (uint8_t)(b * 255u / 24u);
        }
    }

    result = allocate(image, width, height);
    if (result != CODEC_OK)
        return result;
    for (y = 0; y < height; y++) {
        const uint8_t *src = data + bitmap_offset + (size_t)y * stride;
        uint8_t *dst = image->rgba + (size_t)y * width * 4u;
        for (x = 0; x < width; x++) {
            unsigned index = format == 1 ? src[x / 2u] >> (x % 2u ? 0 : 4) & 15u : src[x];
            memcpy(dst, colours[index], 3);
            dst[3] = 255;
            dst += 4;
        }
    }
    return CODEC_OK;
}

enum codec_result zxscr_decode(const uint8_t *data, size_t length,
                               enum zxscr_name name, struct zxscr_image *image)
{
    struct screen s1, s2;

    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->rgba = NULL;
    if (data == NULL)
        return CODEC_TRUNCATED;
    if (length >= 4 && memcmp(data, "\x7FSXG", 4) == 0)
        return decode_sxg(data, length, image);

    switch (length) {
    case BITMAP_SIZE:
        return plain(data, 0, ATTRS_NONE, 3, image);
    case ZXSCR_FILE_SIZE:
    case ZXSCR_FILE_SIZE + 1u: /* a border colour, which isn't shown */
        return plain(data, 0, ATTRS_ROWS, 3, image);
    case ULAPLUS_SIZE:
        return ulaplus(data, ATTRS_ROWS, ZXSCR_FILE_SIZE, image);
    case IFL_SIZE:
        return plain(data, 0, ATTRS_ROWS, 1, image);
    case BSC_SIZE:
    case BMC4_SIZE:
        return decode_bsc(data, length, image);
    case ZXSCR_TIMEX_SIZE:
        if (name == ZXSCR_NAME_MC || name == ZXSCR_NAME_MLT)
            return plain(data, name == ZXSCR_NAME_MC, ATTRS_ROWS, 0, image);
        return plain(data, 0, ATTRS_TIMEX, 0, image);
    case HIRES_SIZE:
        return decode_hires(data, 1, image);
    case ULAPLUS_HICOLOUR_SIZE:
        return ulaplus(data, ATTRS_TIMEX, ZXSCR_TIMEX_SIZE, image);
    case GIGASCREEN_SIZE:
        s1.bitmap = data;
        s2.bitmap = data + ZXSCR_FILE_SIZE;
        s1.linear = s2.linear = 0;
        s1.attrs = s2.attrs = ATTRS_ROWS;
        s1.shift = s2.shift = 3;
        s1.attr = s1.bitmap + BITMAP_SIZE;
        s2.attr = s2.bitmap + BITMAP_SIZE;
        s1.mid = s2.mid = NULL;
        return pair(&s1, &s2, image);
    case HRG_SIZE:
        return decode_hires(data, 2, image);
    default:
        break;
    }
    if (length >= 5 && memcmp(data, "MGH", 3) == 0)
        return decode_mg(data, length, image);
    /* Shorter than a whole screen: most likely a cut-off one. */
    if (length < ZXSCR_FILE_SIZE)
        return CODEC_TRUNCATED;
    return CODEC_INVALID;
}
