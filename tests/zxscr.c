#include "../formats/zxscr/decode.h"
#include "../formats/zxscr/encode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t data[32768];
static uint8_t saved[ZXSCR_TIMEX_SIZE];
static size_t saved_length;
static uint8_t rgba[256 * 192 * 4];

/* The screen address as the ULA builds it: 010 y7 y6 y2 y1 y0 : y5 y4 y3 x7..x3. */
static size_t address(unsigned x, unsigned y)
{
    unsigned high = (y >> 6 & 3u) << 3 | (y & 7u);
    unsigned low = (y >> 3 & 7u) << 5 | x >> 3;
    return (size_t)high << 8 | low;
}

static void plot(unsigned x, unsigned y, int on)
{
    uint8_t bit = (uint8_t)(0x80u >> (x & 7u));
    if (on)
        data[address(x, y)] |= bit;
    else
        data[address(x, y)] &= (uint8_t)~bit;
}

static void attr(unsigned cx, unsigned cy, unsigned value)
{
    data[6144u + cy * 32u + cx] = (uint8_t)value;
}

static void expect(const struct zxscr_image *image, unsigned x, unsigned y,
                   unsigned r, unsigned g, unsigned b)
{
    const uint8_t *p = image->rgba + ((size_t)y * image->width + x) * 4u;
    assert(p[0] == r && p[1] == g && p[2] == b && p[3] == 255);
}

static void decode_ok(struct zxscr_image *image)
{
    assert(zxscr_decode(data, ZXSCR_FILE_SIZE, ZXSCR_NAME_OTHER, image) == CODEC_OK);
    assert(image->width == 256 && image->height == 192);
}

/* Encode rgba, decode the result, and require identical pixels. */
static void round_trip(void)
{
    struct zxscr_image image;

    assert(zxscr_encode(rgba, 256, 192, saved, &saved_length) == CODEC_OK);
    assert(zxscr_decode(saved, saved_length, ZXSCR_NAME_OTHER, &image) == CODEC_OK);
    assert(memcmp(image.rgba, rgba, sizeof rgba) == 0);
    zxscr_free(&image);
}

static void set_rgba(unsigned x, unsigned y, unsigned r, unsigned g, unsigned b, unsigned a)
{
    uint8_t *p = rgba + ((size_t)y * 256u + x) * 4u;
    p[0] = (uint8_t)r;
    p[1] = (uint8_t)g;
    p[2] = (uint8_t)b;
    p[3] = (uint8_t)a;
}

static void test_layout(void)
{
    static const unsigned points[][2] = {
        { 0, 0 }, { 255, 0 }, { 7, 1 }, { 8, 7 }, { 100, 8 }, { 3, 63 },
        { 200, 64 }, { 129, 100 }, { 0, 128 }, { 255, 191 }, { 17, 150 }
    };
    struct zxscr_image image;
    unsigned i, x, y;

    memset(data, 0, sizeof data);
    /* Blue ink on yellow paper, not bright, everywhere. */
    memset(data + 6144, 6 << 3 | 1, 768);
    for (i = 0; i < sizeof points / sizeof points[0]; i++)
        plot(points[i][0], points[i][1], 1);
    decode_ok(&image);
    for (y = 0; y < 192; y++)
        for (x = 0; x < 256; x++) {
            int on = 0;
            for (i = 0; i < sizeof points / sizeof points[0]; i++)
                on |= points[i][0] == x && points[i][1] == y;
            if (on)
                expect(&image, x, y, 0, 0, 192);
            else
                expect(&image, x, y, 192, 192, 0);
        }
    zxscr_free(&image);
    for (y = 0; y < 192; y++)
        for (x = 0; x < 256; x += 8)
            assert(zxscr_offset(x, y) == address(x, y));
}

static void test_attributes(void)
{
    struct zxscr_image image;
    unsigned a, y;

    /* Cell a has attribute a; its left half is ink, right half paper. */
    memset(data, 0, sizeof data);
    for (a = 0; a < 256; a++) {
        attr(a % 32u, a / 32u, a);
        for (y = 0; y < 8; y++)
            data[address((a % 32u) * 8u, (a / 32u) * 8u + y)] = 0xF0;
    }
    decode_ok(&image);
    for (a = 0; a < 256; a++) {
        unsigned x = (a % 32u) * 8u, top = (a / 32u) * 8u;
        unsigned on = (a & 0x40u) ? 255u : 192u, ink = a & 7u, paper = a >> 3 & 7u;
        /* Flash (bit 7) is ignored: first phase, ink where bits are set. */
        for (y = top; y < top + 8u; y++) {
            expect(&image, x, y, ink & 2u ? on : 0, ink & 4u ? on : 0, ink & 1u ? on : 0);
            expect(&image, x + 3u, y, ink & 2u ? on : 0, ink & 4u ? on : 0, ink & 1u ? on : 0);
            expect(&image, x + 4u, y, paper & 2u ? on : 0, paper & 4u ? on : 0, paper & 1u ? on : 0);
            expect(&image, x + 7u, y, paper & 2u ? on : 0, paper & 4u ? on : 0, paper & 1u ? on : 0);
        }
    }
    zxscr_free(&image);
}

static enum codec_result decode(size_t length, enum zxscr_name name, struct zxscr_image *image)
{
    return zxscr_decode(data, length, name, image);
}

static void test_sizes(void)
{
    /* Shorter than a screen is a cut-off screen; other sizes are unknown. */
    static const size_t truncated[] = { 0, 1, 4, 6143, 6145, 6911 };
    static const size_t unknown[] = { 6914, 6975, 6977, 9215, 9217, 11135, 11137,
                                      11903, 12287, 12290, 12351, 12353, 13823,
                                      13825, 14080, 24577, 24579, 32768 };
    static const size_t known[] = { 6144, 6912, 6913, 6976, 9216, 11136, 11904,
                                    12288, 12289, 12352, 13824, 24578 };
    struct zxscr_image image;
    size_t i;

    memset(data, 0x55, sizeof data);
    for (i = 0; i < sizeof truncated / sizeof truncated[0]; i++) {
        assert(decode(truncated[i], ZXSCR_NAME_OTHER, &image) == CODEC_TRUNCATED);
        assert(image.rgba == NULL && image.width == 0 && image.height == 0);
    }
    for (i = 0; i < sizeof unknown / sizeof unknown[0]; i++) {
        assert(decode(unknown[i], ZXSCR_NAME_MC, &image) == CODEC_INVALID);
        assert(image.rgba == NULL);
    }
    for (i = 0; i < sizeof known / sizeof known[0]; i++) {
        assert(decode(known[i], ZXSCR_NAME_OTHER, &image) == CODEC_OK);
        zxscr_free(&image);
    }
    assert(zxscr_decode(NULL, ZXSCR_FILE_SIZE, ZXSCR_NAME_OTHER, &image) == CODEC_TRUNCATED);
    assert(zxscr_decode(data, ZXSCR_FILE_SIZE, ZXSCR_NAME_OTHER, NULL) == CODEC_INVALID);
    /* Any 6912 bytes are a valid screen. */
    decode_ok(&image);
    zxscr_free(&image);
    zxscr_free(&image);
}

static void test_names(void)
{
    assert(zxscr_name_kind(NULL) == ZXSCR_NAME_OTHER);
    assert(zxscr_name_kind("") == ZXSCR_NAME_OTHER);
    assert(zxscr_name_kind("mc") == ZXSCR_NAME_OTHER);
    assert(zxscr_name_kind(".mc") == ZXSCR_NAME_MC);
    assert(zxscr_name_kind("Work:pics/Frog.MC") == ZXSCR_NAME_MC);
    assert(zxscr_name_kind("frog.Mlt") == ZXSCR_NAME_MLT);
    assert(zxscr_name_kind("frog.mlt.scr") == ZXSCR_NAME_OTHER);
    assert(zxscr_name_kind("frog.smc") == ZXSCR_NAME_OTHER);
    assert(zxscr_name_kind("frog.mc2") == ZXSCR_NAME_OTHER);
}

static void expect_colour(const struct zxscr_image *image, unsigned x, unsigned y,
                          unsigned colour, int bright)
{
    uint8_t rgb[3];
    zxscr_colour(colour, bright, rgb);
    expect(image, x, y, rgb[0], rgb[1], rgb[2]);
}

static void test_bitmap_only(void)
{
    struct zxscr_image image;

    /* No attributes: black ink on white paper, as after CLS. */
    memset(data, 0, sizeof data);
    plot(5, 70, 1);
    assert(decode(6144, ZXSCR_NAME_OTHER, &image) == CODEC_OK);
    expect_colour(&image, 5, 70, 0, 0);
    expect_colour(&image, 6, 70, 7, 0);
    zxscr_free(&image);

    /* A trailing border byte is ignored. */
    memset(data + 6144, 0x42, 769);
    assert(decode(6913, ZXSCR_NAME_OTHER, &image) == CODEC_OK);
    expect_colour(&image, 5, 70, 2, 1);
    expect_colour(&image, 6, 70, 0, 1);
    zxscr_free(&image);
}

static void test_ulaplus(void)
{
    static const uint8_t level[8] = { 0, 36, 73, 109, 146, 182, 219, 255 };
    static const uint8_t blue[4] = { 0, 109, 182, 255 };
    struct zxscr_image image;
    unsigned i, a;

    memset(data, 0, sizeof data);
    /* Palette entry i is a different GGGRRRBB pattern for every i. */
    for (i = 0; i < 64; i++)
        data[6912 + i] = (uint8_t)(i * 4u + i / 16u);
    /* Cells 0-3: CLUT 0-3 (flash, bright), ink 5, paper 2, left half ink. */
    for (a = 0; a < 4; a++) {
        attr(a, 0, a << 6 | 2u << 3 | 5u);
        data[address(a * 8u, 3)] = 0xF0;
    }
    assert(decode(6976, ZXSCR_NAME_OTHER, &image) == CODEC_OK);
    for (a = 0; a < 4; a++) {
        unsigned ink = data[6912 + a * 16u + 5u], paper = data[6912 + a * 16u + 8u + 2u];
        expect(&image, a * 8u, 3, level[ink >> 2 & 7u], level[ink >> 5], blue[ink & 3u]);
        expect(&image, a * 8u + 4u, 3, level[paper >> 2 & 7u], level[paper >> 5], blue[paper & 3u]);
        expect(&image, a * 8u, 4, level[paper >> 2 & 7u], level[paper >> 5], blue[paper & 3u]);
    }
    zxscr_free(&image);

    /* Hi-colour with a ULAplus palette: attributes per 8x1 span. */
    memset(data, 0, sizeof data);
    data[12288 + 0] = 0xFF;          /* ink 0 of CLUT 0: white */
    data[12288 + 16 + 8 + 3] = 0x1C; /* paper 3 of CLUT 1: red */
    data[6144 + address(8, 9)] = 0x40 | 3u << 3;
    data[address(8, 9)] = 0x80;
    data[address(0, 9)] = 0x80;
    assert(decode(12352, ZXSCR_NAME_OTHER, &image) == CODEC_OK);
    expect(&image, 8, 9, 0, 0, 0);
    expect(&image, 9, 9, 255, 0, 0);
    expect(&image, 0, 9, 255, 255, 255);
    zxscr_free(&image);
}

static void test_multicolour(void)
{
    struct zxscr_image image;
    unsigned y;

    /* Timex hi-colour: attribute y of a column sits where bitmap row y does. */
    memset(data, 0, sizeof data);
    for (y = 0; y < 192; y++)
        data[6144 + address(16, y)] = (uint8_t)((y & 7u) | (y & 7u) << 3 | (y & 8u ? 0x40 : 0));
    assert(decode(12288, ZXSCR_NAME_OTHER, &image) == CODEC_OK);
    for (y = 0; y < 192; y++)
        expect_colour(&image, 17, y, y & 7u, (y & 7u) && (y & 8u));
    zxscr_free(&image);

    /* .mlt: interleaved bitmap, then 192 linear rows of 32 attributes. */
    memset(data, 0, sizeof data);
    for (y = 0; y < 192; y++)
        data[6144 + y * 32u + 2u] = (uint8_t)((y % 7u + 1u) << 3);
    plot(20, 100, 1);
    data[6144 + 100 * 32 + 2] |= 4;
    assert(decode(12288, ZXSCR_NAME_MLT, &image) == CODEC_OK);
    for (y = 0; y < 192; y++)
        expect_colour(&image, 21, y, y % 7u + 1u, 0);
    expect_colour(&image, 20, 100, 4, 0);
    zxscr_free(&image);

    /* .mc: the same, but the bitmap is linear too. */
    data[address(20, 100)] = 0;
    data[100 * 32 + 2] = 0x08;
    assert(decode(12288, ZXSCR_NAME_MC, &image) == CODEC_OK);
    expect_colour(&image, 20, 100, 4, 0);
    expect_colour(&image, 19, 100, 100 % 7u + 1u, 0);
    zxscr_free(&image);

    /* IFL: one linear attribute row per two pixel rows. */
    memset(data, 0, sizeof data);
    for (y = 0; y < 96; y++)
        data[6144 + y * 32u + 31u] = (uint8_t)((y % 8u) << 3 | 0x40);
    assert(decode(9216, ZXSCR_NAME_MC, &image) == CODEC_OK);
    for (y = 0; y < 192; y++)
        expect_colour(&image, 255, y, y / 2u % 8u, 1);
    zxscr_free(&image);
}

static void test_hires(void)
{
    struct zxscr_image image;
    unsigned y;

    memset(data, 0, sizeof data);
    /* Byte 0 of each bitmap holds columns 0-7 and 8-15. Ink is bits 3-5. */
    data[address(0, 5)] = 0x80;
    data[6144 + address(0, 5)] = 0x01;
    data[6144 + address(8, 5)] = 0x80;
    data[12288] = 1u << 3 | 6u; /* blue ink, yellow paper; mode bits ignored */
    assert(decode(12289, ZXSCR_NAME_OTHER, &image) == CODEC_OK);
    assert(image.width == 512 && image.height == 384);
    for (y = 10; y < 12; y++) {
        expect_colour(&image, 0, y, 1, 1);
        expect_colour(&image, 1, y, 6, 1);
        expect_colour(&image, 15, y, 1, 1);
        expect_colour(&image, 14, y, 6, 1);
        expect_colour(&image, 24, y, 1, 1);
    }
    expect_colour(&image, 0, 9, 6, 1);
    expect_colour(&image, 0, 12, 6, 1);
    zxscr_free(&image);

    /* HRG: two hi-res frames, averaged. */
    memset(data, 0, sizeof data);
    data[12288] = 7u << 3;         /* white ink, black paper */
    data[12289 + 12288] = 2u << 3; /* red ink, cyan paper */
    data[0] = 0x80;
    data[12289] = 0xC0;
    assert(decode(24578, ZXSCR_NAME_OTHER, &image) == CODEC_OK);
    expect(&image, 0, 0, 255, 127, 127); /* white, red */
    expect(&image, 1, 1, 127, 0, 0);     /* black, red */
    expect(&image, 2, 0, 0, 127, 127);   /* black, cyan */
    zxscr_free(&image);
}

static void test_gigascreen(void)
{
    struct zxscr_image image;

    memset(data, 0, sizeof data);
    attr(0, 0, 0x40 | 7);        /* frame 1: bright white ink, black paper */
    data[6912 + 6144] = 2u << 3; /* frame 2: red paper, black ink */
    data[0] = 0x80;
    assert(decode(13824, ZXSCR_NAME_OTHER, &image) == CODEC_OK);
    expect(&image, 0, 0, 223, 127, 127); /* bright white, red */
    expect(&image, 1, 0, 96, 0, 0);      /* black, red */
    expect(&image, 8, 0, 0, 0, 0);
    zxscr_free(&image);
}

static void test_border(void)
{
    struct zxscr_image image;
    unsigned y, x, span;

    /* BSC: the screen at (64,64) of 384x304; border spans of 8 pixels, two
       per byte, low bits first, skipping the screen. */
    memset(data, 0, sizeof data);
    for (x = 0; x < 4224; x++)
        data[6912 + x] = (uint8_t)(x % 8u | (x + 3u) % 8u << 3 | 0xC0);
    attr(0, 0, 0x40 | 4);
    plot(0, 0, 1);
    assert(decode(11136, ZXSCR_NAME_OTHER, &image) == CODEC_OK);
    assert(image.width == 384 && image.height == 304);
    expect_colour(&image, 64, 64, 4, 1);
    expect_colour(&image, 65, 64, 0, 1);
    span = 0;
    for (y = 0; y < 304; y++)
        for (x = 0; x < 384; x += 8) {
            unsigned byte, colour;
            if (y >= 64 && y < 256 && x >= 64 && x < 320)
                continue;
            byte = span / 2u;
            colour = span % 2u ? (byte + 3u) % 8u : byte % 8u;
            expect_colour(&image, x, y, colour, 0);
            expect_colour(&image, x + 7u, y, colour, 0);
            span++;
        }
    assert(span == 4224u * 2u);
    zxscr_free(&image);

    /* BMC4: 8x4 attributes as two tables, top halves then bottom halves. */
    memset(data, 0, sizeof data);
    attr(3, 2, 1u << 3);
    data[6144 + 768 + 2 * 32 + 3] = 5u << 3;
    assert(decode(11904, ZXSCR_NAME_OTHER, &image) == CODEC_OK);
    for (y = 16; y < 20; y++)
        expect_colour(&image, 64 + 24, 64 + y, 1, 0);
    for (y = 20; y < 24; y++)
        expect_colour(&image, 64 + 24, 64 + y, 5, 0);
    zxscr_free(&image);
}

static void mg_header(unsigned type)
{
    memset(data, 0, sizeof data);
    memcpy(data, "MGH\x01", 4);
    data[4] = (uint8_t)type;
}

static void test_multiartist(void)
{
    static const unsigned types[] = { 2, 4, 8 };
    struct zxscr_image image;
    unsigned i, y;

    for (i = 0; i < 3; i++) {
        unsigned rows = types[i];
        size_t table = 6144u / rows, size = 256u + 12288u + 2u * table;
        mg_header(rows);
        /* Frame 1: alternate attribute rows have green paper. Frame 2:
           blue ink everywhere, with its bitmap all set. */
        for (y = 0; y < 192u / rows; y++) {
            data[256 + 12288 + y * 32u] = (uint8_t)(y % 2u ? 4u << 3 : 0);
            data[256 + 12288 + table + y * 32u] = 1;
        }
        memset(data + 256 + 6144, 0xFF, 6144);
        assert(decode(size, ZXSCR_NAME_OTHER, &image) == CODEC_OK);
        for (y = 0; y < 192; y++)
            expect(&image, 3, y, 0, (y / rows) % 2u ? 96 : 0, 96);
        zxscr_free(&image);
        assert(decode(size - 1u, ZXSCR_NAME_OTHER, &image) == CODEC_TRUNCATED);
        assert(decode(size + 1u, ZXSCR_NAME_OTHER, &image) == CODEC_INVALID);
    }

    /* MG1: 8x1 attributes for columns 8-23, 8x8 ones either side. */
    mg_header(1);
    data[256 + 12288 + 50 * 16 + 0] = 2u << 3;             /* frame 1, row 50, column 8 */
    data[256 + 12288 + 3072 + 50 * 16 + 15] = 6u << 3;     /* frame 2, row 50, column 23 */
    data[256 + 12288 + 6144 + 6 * 16 + 7] = 5u << 3;       /* frame 1, cell row 6, column 7 */
    data[256 + 12288 + 6144 + 384 + 6 * 16 + 8] = 3u << 3; /* frame 2, cell row 6, column 24 */
    assert(decode(19456, ZXSCR_NAME_OTHER, &image) == CODEC_OK);
    expect(&image, 64, 50, 96, 0, 0);
    expect(&image, 64, 51, 0, 0, 0);
    expect(&image, 184, 50, 96, 96, 0);
    expect(&image, 56, 50, 0, 96, 96);
    expect(&image, 192, 55, 96, 0, 96);
    zxscr_free(&image);

    /* Bad version or type. */
    mg_header(1);
    data[3] = 2;
    assert(decode(19456, ZXSCR_NAME_OTHER, &image) == CODEC_INVALID);
    for (i = 0; i < 10; i++) {
        mg_header(i == 1 || i == 2 || i == 4 || i == 8 ? 0 : i);
        assert(decode(19456, ZXSCR_NAME_OTHER, &image) == CODEC_INVALID);
    }
    /* Cut short: a header shorter than a screen is truncated too. */
    mg_header(8);
    assert(decode(4, ZXSCR_NAME_OTHER, &image) == CODEC_TRUNCATED);
    assert(decode(256, ZXSCR_NAME_OTHER, &image) == CODEC_TRUNCATED);
    assert(decode(14079, ZXSCR_NAME_OTHER, &image) == CODEC_TRUNCATED);
    assert(decode(14080, ZXSCR_NAME_OTHER, &image) == CODEC_OK);
    zxscr_free(&image);
}

static size_t sxg(unsigned format, unsigned width, unsigned height,
                  unsigned palette_gap, unsigned colours)
{
    size_t palette = 16u + palette_gap, bitmap = palette + colours * 2u;
    memset(data, 0, sizeof data);
    memcpy(data, "\x7FSXG\x01\x00\x00", 7);
    data[7] = (uint8_t)format;
    data[8] = (uint8_t)width;
    data[9] = (uint8_t)(width >> 8);
    data[10] = (uint8_t)height;
    data[11] = (uint8_t)(height >> 8);
    data[12] = (uint8_t)(palette - 14u);
    data[14] = (uint8_t)((bitmap - 16u) & 0xFFu);
    data[15] = (uint8_t)((bitmap - 16u) >> 8);
    return bitmap;
}

static void test_sxg(void)
{
    struct zxscr_image image;
    size_t bitmap;

    /* 16 colours, 3x2, a gap before the palette, only 3 entries. */
    bitmap = sxg(1, 3, 2, 4, 3);
    data[20] = 0x00; data[21] = 0xFC; /* RGB555 red 31 */
    data[22] = 0x00; data[23] = 0x03; /* green 24 of 24 */
    data[24] = 12; data[25] = 0;      /* blue 12 of 24 */
    data[bitmap] = 0x12;
    data[bitmap + 1] = 0x0F;          /* the low nibble is padding */
    data[bitmap + 2] = 0x21;
    data[bitmap + 3] = 0x00;
    assert(decode(bitmap + 4, ZXSCR_NAME_OTHER, &image) == CODEC_OK);
    assert(image.width == 3 && image.height == 2);
    expect(&image, 0, 0, 0, 255, 0);
    expect(&image, 1, 0, 0, 0, 127);
    expect(&image, 2, 0, 255, 0, 0);
    expect(&image, 0, 1, 0, 0, 127);
    expect(&image, 1, 1, 0, 255, 0);
    expect(&image, 2, 1, 255, 0, 0);
    zxscr_free(&image);
    /* Trailing data is ignored; each boundary cut short is truncated. */
    assert(decode(bitmap + 100, ZXSCR_NAME_OTHER, &image) == CODEC_OK);
    zxscr_free(&image);
    assert(decode(bitmap + 3, ZXSCR_NAME_OTHER, &image) == CODEC_TRUNCATED);
    assert(decode(bitmap, ZXSCR_NAME_OTHER, &image) == CODEC_TRUNCATED);
    assert(decode(bitmap - 1, ZXSCR_NAME_OTHER, &image) == CODEC_TRUNCATED);
    assert(decode(15, ZXSCR_NAME_OTHER, &image) == CODEC_TRUNCATED);
    assert(decode(4, ZXSCR_NAME_OTHER, &image) == CODEC_TRUNCATED);
    /* Indexes past the palette are black. */
    data[bitmap] = 0xF1;
    assert(decode(bitmap + 4, ZXSCR_NAME_OTHER, &image) == CODEC_OK);
    expect(&image, 0, 0, 0, 0, 0);
    zxscr_free(&image);
    /* A level above 24 is reserved. */
    data[24] = 25;
    assert(decode(bitmap + 4, ZXSCR_NAME_OTHER, &image) == CODEC_INVALID);

    /* 256 colours: a byte per pixel and a full palette. */
    bitmap = sxg(2, 2, 1, 0, 256);
    data[16 + 200 * 2] = 0x1F;
    data[16 + 200 * 2 + 1] = 0x80;
    data[bitmap] = 200;
    assert(decode(bitmap + 2, ZXSCR_NAME_OTHER, &image) == CODEC_OK);
    expect(&image, 0, 0, 0, 0, 255);
    expect(&image, 1, 0, 0, 0, 0);
    zxscr_free(&image);
    assert(decode(bitmap + 1, ZXSCR_NAME_OTHER, &image) == CODEC_TRUNCATED);

    /* Header errors. */
    bitmap = sxg(2, 2, 1, 0, 257);
    assert(decode(bitmap + 2, ZXSCR_NAME_OTHER, &image) == CODEC_INVALID);
    bitmap = sxg(2, 2, 1, 0, 2);
    data[14]++; /* odd palette length */
    assert(decode(bitmap + 3, ZXSCR_NAME_OTHER, &image) == CODEC_INVALID);
    bitmap = sxg(2, 2, 1, 8, 0);
    data[14] = 0; /* bitmap before the palette */
    assert(decode(bitmap + 2, ZXSCR_NAME_OTHER, &image) == CODEC_INVALID);
    bitmap = sxg(2, 2, 1, 0, 2);
    data[6] = 1; /* packed */
    assert(decode(bitmap + 2, ZXSCR_NAME_OTHER, &image) == CODEC_INVALID);
    bitmap = sxg(3, 2, 1, 0, 2);
    assert(decode(bitmap + 2, ZXSCR_NAME_OTHER, &image) == CODEC_INVALID);
    bitmap = sxg(0, 2, 1, 0, 2);
    assert(decode(bitmap + 2, ZXSCR_NAME_OTHER, &image) == CODEC_INVALID);
    bitmap = sxg(2, 0, 1, 0, 2);
    assert(decode(bitmap + 2, ZXSCR_NAME_OTHER, &image) == CODEC_INVALID);
    bitmap = sxg(2, 2, 0, 0, 2);
    assert(decode(bitmap + 2, ZXSCR_NAME_OTHER, &image) == CODEC_INVALID);
    /* Over the pixel limit is invalid, whatever the length. */
    bitmap = sxg(2, 65535, 65535, 0, 2);
    assert(decode(bitmap, ZXSCR_NAME_OTHER, &image) == CODEC_INVALID);
    bitmap = sxg(1, 65535, 257, 0, 2);
    assert(decode(bitmap, ZXSCR_NAME_OTHER, &image) == CODEC_INVALID);
    /* At the limit, only the pixels are missing. */
    bitmap = sxg(2, 4096, 4096, 0, 2);
    assert(decode(bitmap + 10, ZXSCR_NAME_OTHER, &image) == CODEC_TRUNCATED);
    /* A screen-sized file with SXG magic is read as SXG. */
    sxg(2, 2, 1, 0, 0);
    assert(decode(6912, ZXSCR_NAME_OTHER, &image) == CODEC_OK);
    assert(image.width == 2);
    zxscr_free(&image);
}

static void test_timex_encode(void)
{
    struct zxscr_image image;
    unsigned x;

    /* Three colours in a cell, two per 8x1 span: saved as hi-colour. */
    memset(rgba, 255, sizeof rgba);
    for (x = 0; x < 8; x++) {
        set_rgba(x, 0, 0, 0, x % 2u ? 255 : 0, 255);
        set_rgba(x, 1, 192, x % 2u ? 192 : 0, 0, 255);
    }
    assert(zxscr_encode(rgba, 256, 192, saved, &saved_length) == CODEC_OK);
    assert(saved_length == ZXSCR_TIMEX_SIZE);
    assert(saved[6144 + address(0, 0)] == (0x40 | 1));
    assert(saved[6144 + address(0, 1)] == (2u << 3 | 6u));
    assert(saved[6144 + address(8, 0)] == (0x40 | 7u << 3 | 7u));
    round_trip();
    assert(zxscr_decode(saved, saved_length, ZXSCR_NAME_OTHER, &image) == CODEC_OK);
    zxscr_free(&image);

    /* Three colours in one span can't be saved at all. */
    set_rgba(2, 1, 255, 0, 0, 255);
    assert(zxscr_encode(rgba, 256, 192, saved, &saved_length) == CODEC_INVALID);
    assert(saved_length == 0);
}

static void test_round_trips(void)
{
    struct zxscr_image image;
    unsigned seed, i;

    /* Every decoded screen can be saved again, pixel for pixel. */
    for (seed = 1; seed <= 20; seed++) {
        srand(seed);
        for (i = 0; i < ZXSCR_FILE_SIZE; i++)
            data[i] = (uint8_t)(rand() >> 4);
        if (seed == 1)
            memset(data, 0, 6144);
        if (seed == 2)
            memset(data, 0xFF, 6144);
        decode_ok(&image);
        memcpy(rgba, image.rgba, sizeof rgba);
        zxscr_free(&image);
        round_trip();
        /* Bytes are canonical: saving the reloaded screen changes nothing. */
        memcpy(data, saved, ZXSCR_FILE_SIZE);
        decode_ok(&image);
        assert(zxscr_encode(image.rgba, 256, 192, data + ZXSCR_FILE_SIZE, &saved_length) == CODEC_OK);
        assert(saved_length == ZXSCR_FILE_SIZE);
        assert(memcmp(data + ZXSCR_FILE_SIZE, saved, ZXSCR_FILE_SIZE) == 0);
        zxscr_free(&image);
    }
}

static void test_encode(void)
{
    unsigned x, y;

    /* Plain white: bright white paper, nothing set. */
    memset(rgba, 255, sizeof rgba);
    assert(zxscr_encode(rgba, 256, 192, saved, &saved_length) == CODEC_OK);
    for (x = 0; x < 6144; x++)
        assert(saved[x] == 0);
    for (x = 6144; x < ZXSCR_FILE_SIZE; x++)
        assert(saved[x] == (0x40 | 7 << 3 | 7));

    /* Bright red and black in the top-left cell; black goes with bright.
       The lower colour is paper, so red is the ink. */
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++)
            set_rgba(x, y, x == y ? 0 : 255, 0, 0, 255);
    assert(zxscr_encode(rgba, 256, 192, saved, &saved_length) == CODEC_OK);
    assert(saved[6144] == (0x40 | 0 << 3 | 2));
    assert(saved[0] == 0x7F && saved[address(0, 7)] == 0xFE);
    round_trip();

    /* Transparent pixels composite over white. */
    memset(rgba, 0, sizeof rgba);
    assert(zxscr_encode(rgba, 256, 192, saved, &saved_length) == CODEC_OK);
    assert(saved[6144] == (0x40 | 7 << 3 | 7));

    /* Rejected: wrong size, non-Spectrum colours, three colours in a cell,
       bright and normal colours in one cell, half-transparent colours. */
    memset(rgba, 255, sizeof rgba);
    assert(zxscr_encode(rgba, 255, 192, saved, &saved_length) == CODEC_INVALID);
    assert(zxscr_encode(rgba, 256, 193, saved, &saved_length) == CODEC_INVALID);
    assert(zxscr_encode(NULL, 256, 192, saved, &saved_length) == CODEC_INVALID);
    set_rgba(9, 9, 205, 0, 0, 255);
    assert(zxscr_encode(rgba, 256, 192, saved, &saved_length) == CODEC_INVALID);
    set_rgba(9, 9, 0, 0, 255, 255);
    assert(zxscr_encode(rgba, 256, 192, saved, &saved_length) == CODEC_OK);
    set_rgba(10, 9, 255, 0, 0, 255);
    assert(zxscr_encode(rgba, 256, 192, saved, &saved_length) == CODEC_INVALID);
    set_rgba(10, 9, 0, 0, 255, 255);
    set_rgba(250, 190, 192, 0, 0, 255);
    assert(zxscr_encode(rgba, 256, 192, saved, &saved_length) == CODEC_INVALID);
    set_rgba(250, 190, 255, 255, 255, 255);
    set_rgba(100, 100, 0, 0, 0, 128);
    assert(zxscr_encode(rgba, 256, 192, saved, &saved_length) == CODEC_INVALID);
    set_rgba(100, 100, 0, 0, 0, 255);
    assert(zxscr_encode(rgba, 256, 192, saved, &saved_length) == CODEC_OK);
    round_trip();
}

int main(void)
{
    test_layout();
    test_attributes();
    test_sizes();
    test_names();
    test_bitmap_only();
    test_ulaplus();
    test_multicolour();
    test_hires();
    test_gigascreen();
    test_border();
    test_multiartist();
    test_sxg();
    test_round_trips();
    test_encode();
    test_timex_encode();
    puts("zxscr: ok");
    return 0;
}
