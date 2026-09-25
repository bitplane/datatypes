#include "../formats/msx/decode.h"
#include "../formats/msx/encode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t data[140000];
static uint8_t saved[MSX_MAX_OUTPUT];
static uint8_t rgba[512 * 424 * 4];
static struct msx_image image;
static uint8_t *vram = data + 7;
/* The eight 3-bit levels. */
static const uint8_t l3[8] = { 0, 36, 73, 109, 146, 182, 219, 255 };

/* A BSAVE header for VRAM 0 to end, with the dump zeroed. */
static void bsave(unsigned end)
{
    memset(data, 0, sizeof data);
    data[0] = 0xfe;
    data[3] = (uint8_t)end;
    data[4] = (uint8_t)(end >> 8);
}

static void expect(unsigned x, unsigned y, unsigned r, unsigned g, unsigned b)
{
    const uint8_t *p = image.rgba + ((size_t)y * image.width + x) * 4u;
    assert(x < image.width && y < image.height);
    if (p[0] != r || p[1] != g || p[2] != b || p[3] != 255) {
        fprintf(stderr, "(%u,%u) is %u %u %u, not %u %u %u\n", x, y, p[0], p[1], p[2], r, g, b);
        assert(0);
    }
}

static void expect3(unsigned x, unsigned y, unsigned r, unsigned g, unsigned b)
{
    expect(x, y, l3[r], l3[g], l3[b]);
}

static void decodes(const char *name, size_t length, unsigned width, unsigned height)
{
    enum codec_result result = msx_decode(data, length, name, NULL, 0, &image);
    if (result != CODEC_OK)
        fprintf(stderr, "%s, %zu bytes: %d\n", name, length, result);
    assert(result == CODEC_OK);
    assert(image.width == width && image.height == height);
}

static void fails(const char *name, size_t length, enum codec_result want)
{
    enum codec_result result = msx_decode(data, length, name, NULL, 0, &image);
    if (result != want)
        fprintf(stderr, "%s, %zu bytes: %d, not %d\n", name, length, result, want);
    assert(result == want && image.rgba == NULL);
}

/* Every shorter prefix fails, and doesn't read past its end. */
static void truncations(const char *name, size_t length, size_t step)
{
    size_t n;
    for (n = 0; n < length; n += (n < 16 || length - n < 16) ? 1 : step) {
        uint8_t *copy = malloc(n ? n : 1);
        enum codec_result result;
        memcpy(copy, data, n);
        result = msx_decode(copy, n, name, NULL, 0, &image);
        assert(result == CODEC_TRUNCATED || result == CODEC_INVALID);
        assert(image.rgba == NULL);
        free(copy);
    }
}

/* A VDP palette entry: 0RRR0BBB, 00000GGG. */
static void entry(uint8_t *p, unsigned r, unsigned g, unsigned b)
{
    p[0] = (uint8_t)(r << 4 | b);
    p[1] = (uint8_t)g;
}

/* Entry i is (i & 7, 7 - (i & 7), i >> 1). */
static void palette(uint8_t *p, unsigned count)
{
    unsigned i;
    for (i = 0; i < count; i++)
        entry(p + 2 * i, i & 7, 7 - (i & 7), i >> 1);
}

static void expect_entry(unsigned x, unsigned y, unsigned i)
{
    expect3(x, y, i & 7, 7 - (i & 7), i >> 1);
}

/* The MSX2 palette after reset, for the colours we check. */
static void expect_msx2(unsigned x, unsigned y, unsigned i)
{
    static const uint8_t rgb[16][3] = {
        { 0, 0, 0 }, { 0, 0, 0 }, { 1, 6, 1 }, { 3, 7, 3 }, { 1, 1, 7 }, { 2, 3, 7 },
        { 5, 1, 1 }, { 2, 6, 7 }, { 7, 1, 1 }, { 7, 3, 3 }, { 6, 6, 1 }, { 6, 6, 4 },
        { 1, 4, 1 }, { 6, 2, 5 }, { 5, 5, 5 }, { 7, 7, 7 }
    };
    expect3(x, y, rgb[i][0], rgb[i][1], rgb[i][2]);
}

static void test_names(void)
{
    bsave(0x69ff);
    decodes("pic.sc5", 7 + 0x6a00, 256, 212);
    msx_free(&image);
    decodes("PIC.SC5", 7 + 0x6a00, 256, 212);
    msx_free(&image);
    decodes("Work:a.b/PIC.Ge5", 7 + 0x6a00, 256, 212);
    msx_free(&image);
    fails("pic.sc5/x", 7 + 0x6a00, CODEC_INVALID);
    fails("pic.sc", 7 + 0x6a00, CODEC_INVALID);
    fails("pic.sc55", 7 + 0x6a00, CODEC_INVALID);
    fails("pic.png", 7 + 0x6a00, CODEC_INVALID);
    fails("sc5", 7 + 0x6a00, CODEC_INVALID);
    fails(NULL, 7 + 0x6a00, CODEC_INVALID);
    assert(msx_decode(NULL, 10, "a.sc5", NULL, 0, &image) == CODEC_TRUNCATED);
    assert(msx_decode(data, 10, "a.sc5", NULL, 0, NULL) == CODEC_INVALID);
    assert(strcmp(msx_palette_ext("a.SR5"), "pl5") == 0);
    assert(strcmp(msx_palette_ext("a.sr6"), "pl6") == 0);
    assert(strcmp(msx_palette_ext("a.sr7"), "pl7") == 0);
    assert(strcmp(msx_palette_ext("a.sri"), "pl7") == 0);
    assert(msx_palette_ext("a.sr8") == NULL);
    assert(msx_palette_ext("a.sc5") == NULL);
    assert(msx_palette_ext(NULL) == NULL);
}

static void test_header(void)
{
    bsave(0x69ff);
    data[0] = 0xfd;
    fails("a.sc5", 7 + 0x6a00, CODEC_INVALID);
    bsave(0x69ff);
    data[1] = 1;
    fails("a.sc5", 7 + 0x6a00, CODEC_INVALID);
    bsave(0x69ff);
    data[6] = 0x80;
    fails("a.sc5", 7 + 0x6a00, CODEC_INVALID);
    /* Too short for the lines the header claims. */
    bsave(0x69ff);
    fails("a.sc5", 7 + 0x69ff, CODEC_TRUNCATED);
    /* Less than a line. */
    bsave(0x7e);
    fails("a.sc5", 7 + 0x7f, CODEC_TRUNCATED);
    fails("a.sc5", 6, CODEC_TRUNCATED);
}

/* Screen 2's test picture: in bank k, name n, line l. */
static unsigned pat2(unsigned k, unsigned n, unsigned l) { return (n * 7 + l * 13 + k) & 255; }
static unsigned fg2(unsigned n, unsigned l) { return (n + l) % 15 + 1; }
static unsigned bg2(unsigned k, unsigned n, unsigned l) { return (n * 3 + l + k) % 16; }

static void draw2(void)
{
    unsigned k, n, l, row, col;
    for (row = 0; row < 24; row++)
        for (col = 0; col < 32; col++)
            vram[0x1800 + row * 32 + col] = (uint8_t)(((row % 8) * 32 + col) ^ 0x55);
    for (k = 0; k < 3; k++)
        for (n = 0; n < 256; n++)
            for (l = 0; l < 8; l++) {
                vram[k * 2048 + n * 8 + l] = (uint8_t)pat2(k, n, l);
                vram[0x2000 + k * 2048 + n * 8 + l] = (uint8_t)(fg2(n, l) << 4 | bg2(k, n, l));
            }
}

static unsigned index2(unsigned x, unsigned y)
{
    unsigned k = y / 64, n = (((y / 8) % 8) * 32 + x / 8) ^ 0x55, l = y % 8;
    return (pat2(k, n, l) >> (7 - x % 8) & 1) ? fg2(n, l) : bg2(k, n, l);
}

static void test_screen2(void)
{
    static const uint8_t tms[16][3] = {
        { 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00 }, { 0x3a, 0xbb, 0x43 },
        { 0x70, 0xd3, 0x77 }, { 0x54, 0x59, 0xd7 }, { 0x7b, 0x7b, 0xe8 },
        { 0xb3, 0x63, 0x4b }, { 0x61, 0xdf, 0xe7 }, { 0xd4, 0x6a, 0x53 },
        { 0xf8, 0x8e, 0x77 }, { 0xc7, 0xc7, 0x59 }, { 0xd9, 0xd4, 0x81 },
        { 0x36, 0xa5, 0x3b }, { 0xb0, 0x6b, 0xae }, { 0xc7, 0xd0, 0xc5 },
        { 0xfa, 0xff, 0xf8 }
    };
    unsigned x, y;

    /* MSX1: no palette, so the TMS9918's. */
    bsave(0x37ff);
    draw2();
    decodes("a.sc2", 7 + 0x3800, 256, 192);
    for (y = 0; y < 192; y++)
        for (x = 0; x < 256; x++) {
            const uint8_t *c = tms[index2(x, y)];
            expect(x, y, c[0], c[1], c[2]);
        }
    msx_free(&image);
    decodes("a.grp", 7 + 0x3800, 256, 192);
    msx_free(&image);
    truncations("a.sc2", 7 + 0x3800, 97);
    bsave(0x37fe);
    fails("a.sc2", 7 + 0x3800, CODEC_TRUNCATED);

    /* Saved on an MSX2, with a palette at 0x1B80. */
    bsave(0x37ff);
    draw2();
    palette(vram + 0x1b80, 16);
    decodes("a.sc2", 7 + 0x3800, 256, 192);
    for (y = 0; y < 192; y += 5)
        for (x = 0; x < 256; x += 3)
            expect_entry(x, y, index2(x, y));
    msx_free(&image);
    /* A reserved bit set means it isn't a palette. */
    vram[0x1b80 + 9] = 0x08;
    decodes("a.sc2", 7 + 0x3800, 256, 192);
    expect(0, 0, tms[index2(0, 0)][0], tms[index2(0, 0)][1], tms[index2(0, 0)][2]);
    msx_free(&image);

    /* Screen 4 is laid out the same, with the MSX2 palette by default. */
    bsave(0x37ff);
    draw2();
    decodes("a.sc4", 7 + 0x3800, 256, 192);
    for (y = 0; y < 192; y += 7)
        for (x = 0; x < 256; x += 3)
            expect_msx2(x, y, index2(x, y));
    msx_free(&image);
}

/* A 16x16 sprite: pattern p is solid in its left half, clear on the right. */
static void solid_left(unsigned patterns, unsigned p)
{
    unsigned r;
    for (r = 0; r < 16; r++)
        vram[patterns + p * 8 + r] = 0xff;
}

static void test_sprites1(void)
{
    unsigned attr = 0x1b00, pat = 0x3800, s;

    bsave(0x3fff);
    solid_left(pat, 0);
    /* Sprite 0 at (20, 10) in colour 9; sprite 1 ends the list. */
    vram[attr + 0] = 9;
    vram[attr + 1] = 20;
    vram[attr + 2] = 0;
    vram[attr + 3] = 9;
    vram[attr + 4] = 208;
    vram[attr + 5] = 100;
    vram[attr + 7] = 15;
    decodes("a.sc2", 7 + 0x4000, 256, 192);
    expect(20, 10, 0xf8, 0x8e, 0x77);
    expect(27, 25, 0xf8, 0x8e, 0x77);
    expect(28, 10, 0, 0, 0);
    expect(19, 10, 0, 0, 0);
    expect(20, 9, 0, 0, 0);
    expect(20, 26, 0, 0, 0);
    msx_free(&image);
    /* Only a whole 16K dump has sprites. */
    decodes("a.sc2", 7 + 0x3fff, 256, 192);
    expect(20, 10, 0, 0, 0);
    msx_free(&image);
    decodes("a.sc2", 7 + 0x4001, 256, 192);
    expect(20, 10, 0, 0, 0);
    msx_free(&image);

    /* The early clock bit moves a sprite 32 pixels left; the first sprite
       on a pixel wins. */
    vram[attr + 1] = 40;
    vram[attr + 3] = 0x80 | 9;
    vram[attr + 4] = 9;
    vram[attr + 5] = 8;
    vram[attr + 7] = 15;
    vram[attr + 8] = 208;
    decodes("a.sc2", 7 + 0x4000, 256, 192);
    expect(8, 10, 0xf8, 0x8e, 0x77);
    expect(15, 10, 0xf8, 0x8e, 0x77);
    msx_free(&image);

    /* Only four sprites on a line; the fifth isn't drawn. */
    for (s = 0; s < 5; s++) {
        vram[attr + s * 4] = 9;
        vram[attr + s * 4 + 1] = (uint8_t)(s * 16);
        vram[attr + s * 4 + 2] = 0;
        vram[attr + s * 4 + 3] = 15;
    }
    vram[attr + 20] = 208;
    decodes("a.sc2", 7 + 0x4000, 256, 192);
    expect(48, 10, 0xfa, 0xff, 0xf8);
    expect(64, 10, 0, 0, 0);
    msx_free(&image);

    /* Screen 3 and 4 dumps of 16K draw their sprites too. */
    bsave(0x3fff);
    solid_left(pat, 0);
    vram[attr + 0] = 9;
    vram[attr + 1] = 20;
    vram[attr + 3] = 9;
    vram[attr + 4] = 208;
    decodes("a.sc3", 7 + 0x4000, 256, 192);
    expect(20, 10, 0xf8, 0x8e, 0x77);
    msx_free(&image);
}

/* Sprite mode 2: colours per line, CC to OR, EC per line. */
static void test_sprites2(void)
{
    unsigned attr = 0x7600, pat = 0x7800, colors = 0x7400;

    bsave(0x7fff);
    palette(vram + 0x7680, 16);
    solid_left(pat, 0);
    solid_left(pat, 4);
    vram[attr + 0] = 9;
    vram[attr + 1] = 20;
    vram[attr + 2] = 0;
    memset(vram + colors, 5, 16);
    /* Sprite 1 ORs its colour 2 into sprite 0 on its rows. */
    vram[attr + 4] = 9;
    vram[attr + 5] = 24;
    vram[attr + 6] = 4;
    memset(vram + colors + 16, 0x40 | 2, 16);
    vram[attr + 8] = 216;
    decodes("a.sc5", 7 + 0x8000, 256, 212);
    expect_entry(20, 10, 5);
    expect_entry(24, 10, 7);
    /* A CC sprite shows alone too, once a sprite without CC is on the line. */
    expect_entry(28, 10, 2);
    expect_entry(31, 25, 2);
    expect_entry(32, 10, 0);
    msx_free(&image);

    /* EC on one line only. */
    vram[colors + 3] = 0x80 | 5;
    vram[attr + 4] = 216;
    vram[attr + 1] = 40;
    decodes("a.sc5", 7 + 0x8000, 256, 212);
    expect_entry(8, 13, 5);
    expect_entry(40, 13, 0);
    expect_entry(40, 12, 5);
    msx_free(&image);

    /* A sprite in colour 0 still shows palette entry 0 in mode 2. */
    bsave(0x7fff);
    palette(vram + 0x7680, 16);
    memset(vram, 0x33, 0x6a00);
    solid_left(pat, 0);
    vram[attr + 0] = 9;
    vram[attr + 1] = 20;
    vram[attr + 4] = 216;
    decodes("a.sc5", 7 + 0x8000, 256, 212);
    expect_entry(20, 10, 0);
    expect_entry(19, 10, 3);
    msx_free(&image);

    /* Eight to a line: the ninth isn't drawn. */
    {
        unsigned s;
        bsave(0x7fff);
        palette(vram + 0x7680, 16);
        solid_left(pat, 0);
        for (s = 0; s < 9; s++) {
            vram[attr + s * 4] = 9;
            vram[attr + s * 4 + 1] = (uint8_t)(s * 16);
            memset(vram + colors + s * 16, 6, 16);
        }
        vram[attr + 36] = 216;
        decodes("a.sc5", 7 + 0x8000, 256, 212);
        expect_entry(112, 10, 6);
        expect_entry(128, 10, 0);
        msx_free(&image);
    }
}

static void test_screen3(void)
{
    unsigned x, y;

    /* BASIC's name table: names count down the screen in columns of 4. */
    bsave(0x5ff);
    for (x = 0; x < 0x600; x++)
        vram[x] = (uint8_t)(x * 37 + 11);
    decodes("a.sc3", 7 + 0x600, 256, 192);
    for (y = 0; y < 192; y++)
        for (x = 0; x < 256; x++) {
            unsigned name = (y / 32) * 32 + x / 8, b = vram[name * 8 + (y / 4) % 8];
            unsigned c = x % 8 < 4 ? b >> 4 : b & 15;
            if (c > 1)
                assert(image.rgba[(y * 256 + x) * 4 + 3] == 255);
            if (c == 15)
                expect(x, y, 0xfa, 0xff, 0xf8);
            if (c <= 1)
                expect(x, y, 0, 0, 0);
        }
    msx_free(&image);
    truncations("a.sc3", 7 + 0x600, 13);

    /* With a name table and a palette. */
    bsave(0x2fff);
    for (x = 0; x < 0x800; x++)
        vram[x] = (uint8_t)(x * 37 + 11);
    for (x = 0; x < 768; x++)
        vram[0x800 + x] = (uint8_t)(255 - x % 256);
    palette(vram + 0x2020, 16);
    decodes("a.sc3", 7 + 0x3000, 256, 192);
    for (y = 0; y < 192; y += 3)
        for (x = 0; x < 256; x++) {
            unsigned name = vram[0x800 + (y / 8) * 32 + x / 8], b = vram[name * 8 + (y / 4) % 8];
            expect_entry(x, y, x % 8 < 4 ? b >> 4 : b & 15);
        }
    msx_free(&image);
}

static void test_screen5(void)
{
    unsigned x, y;

    bsave(0x769f);
    for (x = 0; x < 0x6a00; x++)
        vram[x] = (uint8_t)(x * 7 + x / 128);
    palette(vram + 0x7680, 16);
    decodes("a.sc5", 7 + 0x76a0, 256, 212);
    for (y = 0; y < 212; y++)
        for (x = 0; x < 256; x++) {
            unsigned b = vram[y * 128 + x / 2];
            expect_entry(x, y, x & 1 ? b & 15 : b >> 4);
        }
    msx_free(&image);
    /* Without the palette, the MSX2's. */
    decodes("a.sc5", 7 + 0x769f, 256, 212);
    expect_msx2(0, 0, vram[0] >> 4);
    expect_msx2(1, 0, vram[0] & 15);
    msx_free(&image);
    /* The header's end address sets the height, up to 212 lines. */
    data[3] = 0xff;
    data[4] = 0x0f;
    decodes("a.sc5", 7 + 0x1000, 256, 32);
    msx_free(&image);
    data[4] = 0xff;
    fails("a.sc5", 7 + 0x8000, CODEC_TRUNCATED);
    decodes("a.sc5", 7 + 0x10000, 256, 212);
    msx_free(&image);
    bsave(0x769f);
    truncations("a.sc5", 7 + 0x76a0, 211);

    /* Screen 6: four colours, 512 wide. */
    bsave(0x7687);
    for (x = 0; x < 0x6a00; x++)
        vram[x] = (uint8_t)(x * 13 + x / 128);
    palette(vram + 0x7680, 4);
    decodes("a.sc6", 7 + 0x7688, 512, 212);
    for (y = 0; y < 212; y++)
        for (x = 0; x < 512; x++)
            expect_entry(x, y, vram[y * 128 + x / 4] >> (6 - 2 * (x % 4)) & 3);
    msx_free(&image);
    decodes("a.sc6", 7 + 0x7687, 512, 212);
    for (x = 0; x < 4; x++) {
        static const uint8_t six[4][3] = {
            { 0, 0, 0 }, { 0x24, 0x92, 0x24 }, { 0x24, 0xdb, 0x24 }, { 0x6d, 0xff, 0x6d }
        };
        unsigned i = vram[0] >> (6 - 2 * x) & 3;
        expect(x, 0, six[i][0], six[i][1], six[i][2]);
    }
    msx_free(&image);

    /* Screen 6 sprites are two pixels wide, their colour split in two. */
    bsave(0x7fff);
    palette(vram + 0x7680, 4);
    solid_left(0x7800, 0);
    vram[0x7600] = 9;
    vram[0x7601] = 20;
    vram[0x7604] = 216;
    memset(vram + 0x7400, 0x9, 16);
    decodes("a.sc6", 7 + 0x8000, 512, 212);
    expect_entry(40, 10, 2);
    expect_entry(41, 10, 1);
    expect_entry(56, 10, 0);
    msx_free(&image);
}

static void test_screen7(void)
{
    unsigned x, y;

    bsave(0xfa9f);
    for (x = 0; x < 0xd400; x++)
        vram[x] = (uint8_t)(x * 5 + x / 256);
    palette(vram + 0xfa80, 16);
    vram[0xfa00] = 216;
    decodes("a.sc7", 7 + 0xfaa0, 512, 212);
    for (y = 0; y < 212; y++)
        for (x = 0; x < 512; x++) {
            unsigned b = vram[y * 256 + x / 2];
            expect_entry(x, y, x & 1 ? b & 15 : b >> 4);
        }
    msx_free(&image);
    decodes("a.ge7", 7 + 0xfa9f, 512, 212);
    expect_msx2(0, 0, vram[0] >> 4);
    msx_free(&image);
    truncations("a.sc7", 7 + 0xd400, 509);
    data[4] = 0xd3;
    data[3] = 0xfe;
    fails("a.sc7", 7 + 0xd400, CODEC_TRUNCATED);

    /* Sprites are drawn two pixels wide. */
    bsave(0xfa9f);
    palette(vram + 0xfa80, 16);
    solid_left(0xf000, 0);
    vram[0xfa00] = 9;
    vram[0xfa01] = 20;
    vram[0xfa04] = 216;
    memset(vram + 0xf800, 11, 16);
    decodes("a.sc7", 7 + 0xfaa0, 512, 212);
    expect_entry(40, 10, 11);
    expect_entry(55, 10, 11);
    expect_entry(56, 10, 0);
    msx_free(&image);
}

static void expect8(unsigned x, unsigned y, unsigned b)
{
    static const uint8_t blues[4] = { 0, 2, 4, 7 };
    expect3(x, y, b >> 2 & 7, b >> 5, blues[b & 3]);
}

/* Graph Saurus RLE of n bytes of in, using every kind of code. */
static size_t pack(const uint8_t *in, size_t n, uint8_t *out)
{
    size_t i = 0, o = 0;
    while (i < n) {
        size_t run = 1;
        while (i + run < n && in[i + run] == in[i] && run < 256)
            run++;
        if (run >= 16) {
            out[o++] = 0;
            out[o++] = (uint8_t)run;
            out[o++] = in[i];
        } else if (run > 1 || in[i] < 16) {
            out[o++] = (uint8_t)run;
            out[o++] = in[i];
        } else
            out[o++] = in[i];
        i += run;
    }
    return o;
}

/* Packs vram's first 0xD400 bytes into data with an 0xFD header. */
static size_t packed(void)
{
    static uint8_t copy[0xd400];
    size_t n;
    memcpy(copy, vram, sizeof copy);
    n = pack(copy, sizeof copy, vram);
    data[0] = 0xfd;
    data[1] = data[2] = data[5] = data[6] = 0;
    data[3] = (uint8_t)n;
    data[4] = (uint8_t)(n >> 8);
    return 7 + n;
}

static void test_screen8(void)
{
    unsigned x, y;
    size_t n;

    bsave(0xd3ff);
    for (x = 0; x < 0xd400; x++)
        vram[x] = (uint8_t)(x * 3 + x / 256);
    decodes("a.sc8", 7 + 0xd400, 256, 212);
    for (y = 0; y < 212; y++)
        for (x = 0; x < 256; x++)
            expect8(x, y, vram[y * 256 + x]);
    msx_free(&image);
    truncations("a.sc8", 7 + 0xd400, 499);

    /* Packed by Graph Saurus. */
    for (x = 0; x < 0xd400; x++)
        vram[x] = (uint8_t)(x < 300 ? x / 20 : x < 5000 ? 7 : x < 5600 ? 200 : (x * 3 + x / 256) % 60);
    n = packed();
    decodes("a.sr8", n, 256, 212);
    for (y = 0; y < 212; y++)
        for (x = 0; x < 256; x++) {
            unsigned i = y * 256 + x;
            expect8(x, y, i < 300 ? i / 20 : i < 5000 ? 7 : i < 5600 ? 200 : (i * 3 + i / 256) % 60);
        }
    msx_free(&image);
    truncations("a.sr8", n, 97);
    /* The header's end must match the file. */
    fails("a.sr8", n + 1, CODEC_INVALID);
    /* A stream that ends early. */
    data[3] = (uint8_t)(n - 10);
    data[4] = (uint8_t)((n - 10) >> 8);
    fails("a.sr8", n - 3, CODEC_TRUNCATED);
    /* A last run that overruns is clamped. */
    memset(data, 0, sizeof data);
    data[0] = 0xfd;
    for (n = 0; n < 213; n++) {
        vram[3 * n] = 0;
        vram[3 * n + 1] = 0;
        vram[3 * n + 2] = 0x44;
    }
    data[3] = (uint8_t)(3 * 213);
    data[4] = (uint8_t)((3 * 213) >> 8);
    decodes("a.sc8", 7 + 3 * 213, 256, 212);
    expect8(255, 211, 0x44);
    msx_free(&image);
    /* 1 to 15 then a value; 16 and up is itself. */
    memset(data, 0, sizeof data);
    data[0] = 0xfd;
    vram[0] = 3;
    vram[1] = 0x10;
    vram[2] = 0x20;
    vram[3] = 0;
    vram[4] = 0;
    vram[5] = 0x30;
    for (n = 6; n < 6 + 3 * 212; n += 3) {
        vram[n] = 0;
        vram[n + 1] = 0;
        vram[n + 2] = 0x50;
    }
    data[3] = (uint8_t)n;
    data[4] = (uint8_t)(n >> 8);
    decodes("a.sc8", 7 + n, 256, 212);
    expect8(0, 0, 0x10);
    expect8(2, 0, 0x10);
    expect8(3, 0, 0x20);
    expect8(4, 0, 0x30);
    expect8(3, 1, 0x30);
    expect8(4, 1, 0x50);
    msx_free(&image);

    /* A whole 64K dump has sprites, in their own palette. */
    bsave(0xfa9f);
    solid_left(0xf000, 0);
    vram[0xfa00] = 9;
    vram[0xfa01] = 20;
    vram[0xfa04] = 216;
    memset(vram + 0xf800, 8, 16);
    decodes("a.sc8", 7 + 0xfaa0, 256, 212);
    expect3(20, 10, 7, 4, 2);
    expect8(28, 10, 0);
    msx_free(&image);
}

/* The screen 12 group at x, from luma y0 to y3, chroma k and j. */
static void group(uint8_t *p, const unsigned *luma, int k, int j)
{
    unsigned uk = (unsigned)k & 63, uj = (unsigned)j & 63;
    p[0] = (uint8_t)(luma[0] << 3 | (uk & 7));
    p[1] = (uint8_t)(luma[1] << 3 | uk >> 3);
    p[2] = (uint8_t)(luma[2] << 3 | (uj & 7));
    p[3] = (uint8_t)(luma[3] << 3 | uj >> 3);
}

static unsigned clamp5(int v)
{
    v = v < 0 ? 0 : v > 31 ? 31 : v;
    return (unsigned)(v << 3 | v >> 2);
}

static void expect_yjk(unsigned x, unsigned y, int luma, int k, int j)
{
    int b = 5 * luma - 2 * j - k + 2;
    expect(x, y, clamp5(luma + j), clamp5(luma + k), clamp5(b < 0 ? -1 : b / 4));
}

static void test_yjk(void)
{
    static const unsigned luma[4] = { 10, 12, 14, 16 }, dark[4] = { 0, 0, 1, 31 };
    unsigned x, y;
    size_t n;

    bsave(0xd3ff);
    for (y = 0; y < 212; y++)
        for (x = 0; x < 256; x += 4) {
            unsigned l[4] = { (x + y) & 31, (x * 3) & 31, (y * 5) & 31, (x ^ y) & 31 };
            group(vram + y * 256 + x, l, (int)((x + y * 7) % 64) - 32, (int)((x * 5 + y) % 64) - 32);
        }
    decodes("a.scc", 7 + 0xd400, 256, 212);
    for (y = 0; y < 212; y++)
        for (x = 0; x < 256; x++) {
            unsigned l[4] = { (x / 4 * 4 + y) & 31, (x / 4 * 4 * 3) & 31, (y * 5) & 31, (x / 4 * 4 ^ y) & 31 };
            expect_yjk(x, y, (int)l[x % 4], (int)((x / 4 * 4 + y * 7) % 64) - 32,
                       (int)((x / 4 * 4 * 5 + y) % 64) - 32);
        }
    msx_free(&image);
    truncations("a.yjk", 7 + 0xd400, 1001);

    /* By hand: y 10, k 3, j -2 gives R 8, G 13, B 13. */
    bsave(0xd3ff);
    group(vram, luma, 3, -2);
    group(vram + 4, dark, 31, 31);
    group(vram + 8, dark, -32, -32);
    decodes("a.srs", 7 + 0xd400, 256, 212);
    expect(0, 0, 66, 107, 107);
    expect(4, 0, 0xff, 0xff, 0);
    expect(8, 0, 0, 0, 198);
    expect(11, 0, 0, 0, 0xff);
    msx_free(&image);

    /* 192 lines when the dump stops at 0xBFFF. */
    bsave(0xbfff);
    group(vram, luma, 3, -2);
    decodes("a.scc", 7 + 0xc000, 256, 192);
    expect(0, 0, 66, 107, 107);
    msx_free(&image);
    /* Packed, like screen 8. */
    bsave(0xd3ff);
    group(vram + 256 * 211 + 252, luma, 3, -2);
    n = packed();
    decodes("a.scc", n, 256, 212);
    expect(252, 211, 66, 107, 107);
    msx_free(&image);

    /* Screen 10: an odd luma is palette entry luma / 2. */
    bsave(0xfa9f);
    palette(vram + 0xfa80, 16);
    vram[0xfa00] = 216;
    group(vram, luma, 3, -2);
    vram[1] |= 0x08 | 7 << 4;
    decodes("a.sca", 7 + 0xfaa0, 256, 212);
    expect(0, 0, 66, 107, 107);
    expect_entry(1, 0, 7);
    msx_free(&image);
    decodes("a.scb", 7 + 0xfaa0, 256, 212);
    msx_free(&image);
    /* Screen 10 needs the palette. */
    fails("a.sca", 7 + 0xfa9f, CODEC_TRUNCATED);
    /* Screen 12 reads the same luma as a colour. */
    decodes("a.scc", 7 + 0xfaa0, 256, 212);
    expect_yjk(1, 0, 12 | 1 | 7 << 1, 3, -2);
    msx_free(&image);
}

static void test_graph_saurus(void)
{
    uint8_t pal[MSX_PALETTE_SIZE];
    unsigned x, y;
    size_t n;

    palette(pal, 16);
    bsave(0x69ff);
    for (x = 0; x < 0x6a00; x++)
        vram[x] = (uint8_t)(x * 11);
    assert(msx_decode(data, 7 + 0x6a00, "a.sr5", pal, sizeof pal, &image) == CODEC_OK);
    assert(image.width == 256 && image.height == 212);
    for (y = 0; y < 212; y += 3)
        for (x = 0; x < 256; x++) {
            unsigned b = vram[y * 128 + x / 2];
            expect_entry(x, y, x & 1 ? b & 15 : b >> 4);
        }
    msx_free(&image);
    /* Without a palette file, or with a short one, the MSX2's. */
    decodes("a.sr5", 7 + 0x6a00, 256, 212);
    expect_msx2(0, 0, 0);
    expect_msx2(3, 0, 11);
    msx_free(&image);
    assert(msx_decode(data, 7 + 0x6a00, "a.sr5", pal, 31, &image) == CODEC_OK);
    expect_msx2(3, 0, 11);
    msx_free(&image);
    /* Graph Saurus ignores a palette in the dump. */
    palette(vram + 0x7680, 16);
    assert(msx_decode(data, 7 + 0x6a00, "a.sr5", NULL, 0, &image) == CODEC_OK);
    expect_msx2(3, 0, 11);
    msx_free(&image);

    /* Screen 6: four colours from an 8-byte file. */
    assert(msx_decode(data, 7 + 0x6a00, "a.sr6", pal, 8, &image) == CODEC_OK);
    assert(image.width == 512 && image.height == 212);
    for (x = 0; x < 512; x++)
        expect_entry(x, 5, vram[5 * 128 + x / 4] >> (6 - 2 * (x % 4)) & 3);
    msx_free(&image);
    assert(msx_decode(data, 7 + 0x6a00, "a.sr6", pal, 7, &image) == CODEC_OK);
    expect(0, 0, 0, 0, 0);
    msx_free(&image);

    /* Screen 7, raw and packed. */
    bsave(0xd3ff);
    for (x = 0; x < 0xd400; x++)
        vram[x] = (uint8_t)(x < 20000 ? 0x21 : x * 7);
    assert(msx_decode(data, 7 + 0xd400, "a.sr7", pal, sizeof pal, &image) == CODEC_OK);
    assert(image.width == 512 && image.height == 212);
    expect_entry(0, 0, 2);
    expect_entry(1, 0, 1);
    msx_free(&image);
    n = packed();
    assert(msx_decode(data, n, "a.sr7", pal, sizeof pal, &image) == CODEC_OK);
    for (y = 0; y < 212; y += 7)
        for (x = 0; x < 512; x++) {
            unsigned i = y * 256 + x / 2, b = i < 20000 ? 0x21 : (i * 7) & 255;
            expect_entry(x, y, x & 1 ? b & 15 : b >> 4);
        }
    msx_free(&image);

    /* Interlaced screen 7: 424 lines and no header. */
    for (x = 0; x < 424 * 256; x++)
        data[x] = (uint8_t)(x / 256);
    assert(msx_decode(data, 424 * 256, "a.sri", pal, sizeof pal, &image) == CODEC_OK);
    assert(image.width == 512 && image.height == 424);
    expect_entry(0, 17, 1);
    expect_entry(1, 17, 1);
    expect_entry(0, 423, (423 & 255) >> 4);
    msx_free(&image);
    fails("a.sri", 424 * 256 - 1, CODEC_TRUNCATED);
    fails("a.sri", 424 * 256 + 1, CODEC_INVALID);
}

static void fill(unsigned width, unsigned height, const uint8_t (*colours)[4],
                 unsigned count)
{
    unsigned x, y;
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++)
            memcpy(rgba + (y * width + x) * 4, colours[(x * 7 + y * 3 + x / 16) % count], 4);
}

/* What was saved decodes to what was filled, with alpha over white. */
static void round_trip(const char *name, unsigned width, size_t size)
{
    size_t n = 0, i;
    assert(msx_encode(rgba, width, 212, saved, &n) == CODEC_OK);
    assert(n == size);
    memcpy(data, saved, n);
    decodes(name, n, width, 212);
    for (i = 0; i < (size_t)width * 212; i++) {
        const uint8_t *in = rgba + i * 4, *out = image.rgba + i * 4;
        unsigned c;
        for (c = 0; c < 3; c++)
            assert(out[c] == (in[c] * in[3] + 255 * (255 - in[3]) + 127) / 255);
    }
    msx_free(&image);
}

static void test_encode(void)
{
    uint8_t colours[256][4];
    unsigned i;
    size_t n;

    for (i = 0; i < 16; i++) {
        colours[i][0] = l3[i & 7];
        colours[i][1] = l3[7 - (i & 7)];
        colours[i][2] = l3[(i * 3) & 7];
        colours[i][3] = 255;
    }
    fill(256, 212, (const uint8_t (*)[4])colours, 16);
    round_trip("a.sc5", 256, 7 + 0x76a0);
    assert(saved[0] == 0xfe && saved[3] == 0x9f && saved[4] == 0x76);
    fill(512, 212, (const uint8_t (*)[4])colours, 16);
    round_trip("a.sc7", 512, 7 + 0xfaa0);
    fill(512, 212, (const uint8_t (*)[4])colours, 3);
    round_trip("a.sc7", 512, 7 + 0xfaa0);

    /* More than 16 colours on screen 8's levels. */
    for (i = 0; i < 256; i++) {
        static const uint8_t blues[4] = { 0, 2, 4, 7 };
        colours[i][0] = l3[i >> 2 & 7];
        colours[i][1] = l3[i >> 5];
        colours[i][2] = l3[blues[i & 3]];
        colours[i][3] = 255;
    }
    fill(256, 212, (const uint8_t (*)[4])colours, 256);
    round_trip("a.sc8", 256, 7 + 0xd400);
    assert(msx_encode(rgba, 512, 212, saved, &n) == CODEC_INVALID);

    /* Transparent is white, which screen 8 has. */
    fill(256, 212, (const uint8_t (*)[4])colours, 256);
    rgba[3] = 0;
    rgba[7] = 0;
    round_trip("a.sc8", 256, 7 + 0xd400);
    /* A blue screen 8 can't show. */
    rgba[3] = 255;
    rgba[0] = rgba[1] = 0;
    rgba[2] = l3[1];
    assert(msx_encode(rgba, 256, 212, saved, &n) == CODEC_INVALID);
    /* A level that isn't one of the eight. */
    fill(256, 212, (const uint8_t (*)[4])colours, 4);
    rgba[0] = 1;
    assert(msx_encode(rgba, 256, 212, saved, &n) == CODEC_INVALID);
    /* Half-transparent over white, onto the levels. */
    fill(256, 212, (const uint8_t (*)[4])colours, 4);
    rgba[0] = rgba[1] = rgba[2] = 0;
    rgba[3] = 0;
    round_trip("a.sc5", 256, 7 + 0x76a0);

    assert(msx_encode(rgba, 256, 211, saved, &n) == CODEC_INVALID);
    assert(msx_encode(rgba, 320, 212, saved, &n) == CODEC_INVALID);
    assert(msx_encode(NULL, 256, 212, saved, &n) == CODEC_INVALID);
}

int main(void)
{
    test_names();
    test_header();
    test_screen2();
    test_sprites1();
    test_screen3();
    test_screen5();
    test_sprites2();
    test_screen7();
    test_screen8();
    test_yjk();
    test_graph_saurus();
    test_encode();
    puts("msx ok");
    return 0;
}
