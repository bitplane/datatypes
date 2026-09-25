#include "../formats/ftex/decode.h"
#include "../formats/ftex/encode.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t data[1 << 16];
static size_t used;

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* Header and a directory of n entries; the data starts after it. */
static void header(uint32_t w, uint32_t h, uint32_t levels, uint32_t n)
{
    memset(data, 0, sizeof data);
    memcpy(data, "FTEX", 4);
    put32(data + 4, 1);
    put32(data + 8, w);
    put32(data + 12, h);
    put32(data + 16, levels);
    put32(data + 20, n);
    used = 24 + 8 * n;
}

/* Point directory entry i at a format starting at the current end. */
static void entry(uint32_t i, uint32_t format)
{
    put32(data + 24 + 8 * i, format);
    put32(data + 28 + 8 * i, (uint32_t)used);
}

/* Append a level of size bytes whose content is filled from bytes. */
static void level(uint32_t size, const uint8_t *bytes)
{
    put32(data + used, size);
    if (bytes != NULL)
        memcpy(data + used + 4, bytes, size);
    used += 4 + size;
}

/* Append an RGB level where pixel i has colour (i, 2i, 3i) plus seed. */
static void rgb_level(unsigned w, unsigned h, unsigned seed)
{
    unsigned i;
    put32(data + used, w * h * 3);
    for (i = 0; i < w * h; i++) {
        data[used + 4 + i * 3] = (uint8_t)(i + seed);
        data[used + 5 + i * 3] = (uint8_t)(2 * i + seed);
        data[used + 6 + i * 3] = (uint8_t)(3 * i + seed);
    }
    used += 4 + w * h * 3;
}

static void pixel(const struct ftex_image *im, unsigned x, unsigned y,
                  unsigned r, unsigned g, unsigned b, unsigned a)
{
    const uint8_t *p = im->rgba + (y * im->width + x) * 4u;
    if (p[0] != r || p[1] != g || p[2] != b || p[3] != a) {
        fprintf(stderr, "pixel %u,%u is %u %u %u %u, expected %u %u %u %u\n",
                x, y, p[0], p[1], p[2], p[3], r, g, b, a);
        assert(0);
    }
}

static unsigned long count(size_t length)
{
    unsigned long n = 99;
    assert(ftex_count(data, length, &n) == CODEC_OK);
    return n;
}

static enum codec_result decode(size_t length, unsigned long index)
{
    struct ftex_image im;
    enum codec_result r = ftex_decode(data, length, index, &im);
    if (r == CODEC_OK)
        ftex_free(&im);
    else
        assert(im.rgba == NULL);
    return r;
}

static void test_rgb_mips(void)
{
    struct ftex_image im;
    header(5, 3, 3, 1);
    entry(0, FTEX_RGB);
    rgb_level(5, 3, 0);
    rgb_level(2, 1, 100);
    rgb_level(1, 1, 200);
    assert(count(used) == 3);

    assert(ftex_decode(data, used, 0, &im) == CODEC_OK);
    assert(im.width == 5 && im.height == 3);
    pixel(&im, 0, 0, 0, 0, 0, 255);
    pixel(&im, 4, 2, 14, 28, 42, 255);
    ftex_free(&im);

    assert(ftex_decode(data, used, 1, &im) == CODEC_OK);
    assert(im.width == 2 && im.height == 1);
    pixel(&im, 1, 0, 101, 102, 103, 255);
    ftex_free(&im);

    assert(ftex_decode(data, used, 2, &im) == CODEC_OK);
    assert(im.width == 1 && im.height == 1);
    pixel(&im, 0, 0, 200, 200, 200, 255);
    ftex_free(&im);

    assert(decode(used, 3) == CODEC_INVALID);
    assert(decode(used, 0xffffffffu) == CODEC_INVALID);
}

/* 6x5 DXT1: 2x2 blocks, the right and bottom ones cropped. */
static void test_dxt1(void)
{
    static const uint8_t blocks[32] = {
        /* four-colour block: c0 red > c1 blue, all index 0 */
        0x00, 0xf8, 0x1f, 0x00, 0, 0, 0, 0,
        /* three-colour block: c0 blue <= c1 red, all index 3 (transparent) */
        0x1f, 0x00, 0x00, 0xf8, 0xff, 0xff, 0xff, 0xff,
        /* all index 1: blue */
        0x00, 0xf8, 0x1f, 0x00, 0x55, 0x55, 0x55, 0x55,
        /* three-colour, index 2: midpoint of blue and red */
        0x1f, 0x00, 0x00, 0xf8, 0xaa, 0xaa, 0xaa, 0xaa,
    };
    struct ftex_image im;
    header(6, 5, 1, 1);
    entry(0, FTEX_DXT1);
    level(32, blocks);
    assert(count(used) == 1);
    assert(ftex_decode(data, used, 0, &im) == CODEC_OK);
    assert(im.width == 6 && im.height == 5);
    pixel(&im, 0, 0, 255, 0, 0, 255);
    pixel(&im, 3, 3, 255, 0, 0, 255);
    pixel(&im, 4, 0, 0, 0, 0, 0);
    pixel(&im, 5, 3, 0, 0, 0, 0);
    pixel(&im, 0, 4, 0, 0, 255, 255);
    pixel(&im, 5, 4, 127, 0, 127, 255);
    ftex_free(&im);

    /* A level may hold more than it needs; the extra is skipped. */
    header(4, 4, 2, 1);
    entry(0, FTEX_DXT1);
    level(12, blocks);
    level(8, blocks + 8);
    assert(count(used) == 2);
    assert(ftex_decode(data, used, 1, &im) == CODEC_OK);
    assert(im.width == 2 && im.height == 2);
    pixel(&im, 1, 1, 0, 0, 0, 0);
    ftex_free(&im);

    /* Too few bytes for the blocks. */
    header(8, 4, 1, 1);
    entry(0, FTEX_DXT1);
    level(8, blocks);
    assert(count(used) == 0);
    assert(decode(used, 0) == CODEC_INVALID);
}

/* Two formats, with an unknown one between them. */
static void test_formats(void)
{
    static const uint8_t green[8] = { 0xe0, 0x07, 0xe0, 0x07, 0, 0, 0, 0 };
    struct ftex_image im;
    header(2, 2, 2, 3);
    entry(0, FTEX_DXT1);
    level(8, green);
    level(8, green);
    entry(1, 7);
    level(4, NULL);
    entry(2, FTEX_RGB);
    rgb_level(2, 2, 10);
    rgb_level(1, 1, 50);
    assert(count(used) == 4);
    assert(ftex_decode(data, used, 1, &im) == CODEC_OK);
    assert(im.width == 1);
    pixel(&im, 0, 0, 0, 255, 0, 255);
    ftex_free(&im);
    assert(ftex_decode(data, used, 2, &im) == CODEC_OK);
    assert(im.width == 2);
    pixel(&im, 1, 1, 13, 16, 19, 255);
    ftex_free(&im);
    assert(ftex_decode(data, used, 3, &im) == CODEC_OK);
    pixel(&im, 0, 0, 50, 50, 50, 255);
    ftex_free(&im);
    assert(decode(used, 4) == CODEC_INVALID);

    /* Only unknown formats. */
    header(1, 1, 1, 1);
    entry(0, 2);
    level(3, NULL);
    assert(count(used) == 0);
    assert(decode(used, 0) == CODEC_INVALID);
}

static void test_levels(void)
{
    size_t full;
    unsigned i;
    /* No levels declared: the top one is still read. */
    header(2, 2, 0, 1);
    entry(0, FTEX_RGB);
    rgb_level(2, 2, 0);
    assert(count(used) == 1);
    assert(decode(used, 0) == CODEC_OK);

    /* A huge level count stops at the cap, and levels past 1x1 stay 1x1. */
    header(1, 1, 0xffffffffu, 1);
    entry(0, FTEX_RGB);
    for (i = 0; i < 20; i++)
        rgb_level(1, 1, i);
    assert(count(used) == 17);

    /* A later level cut short: earlier ones still load. */
    header(4, 2, 3, 1);
    entry(0, FTEX_RGB);
    rgb_level(4, 2, 0);
    rgb_level(2, 1, 0);
    full = used;
    rgb_level(1, 1, 0);
    assert(count(used) == 3);
    assert(count(used - 1) == 2);
    assert(count(full + 3) == 2);
    assert(decode(full + 3, 1) == CODEC_OK);
    assert(decode(full + 3, 2) == CODEC_TRUNCATED);
    assert(decode(full + 3, 3) == CODEC_INVALID);
}

static void test_malformed(void)
{
    size_t i, full;
    unsigned long n;

    header(4, 2, 1, 1);
    entry(0, FTEX_RGB);
    rgb_level(4, 2, 0);
    full = used;
    assert(decode(full, 0) == CODEC_OK);
    /* Every truncation: header, directory, level size, pixels. */
    for (i = 0; i < full; i++)
        assert(decode(i, 0) == CODEC_TRUNCATED);
    assert(ftex_count(data, 10, &n) == CODEC_TRUNCATED);

    data[0] = 'f';
    assert(decode(full, 0) == CODEC_INVALID);
    data[0] = 'F';

    put32(data + 8, 0);
    assert(decode(full, 0) == CODEC_INVALID);
    put32(data + 8, 65536);
    assert(decode(full, 0) == CODEC_TOO_LARGE);
    put32(data + 8, 0xffffffffu);
    assert(decode(full, 0) == CODEC_TOO_LARGE);
    put32(data + 8, 4097);
    put32(data + 12, 4097);
    assert(decode(full, 0) == CODEC_TOO_LARGE);
    put32(data + 8, 4);
    put32(data + 12, 2);

    put32(data + 20, 0);
    assert(decode(full, 0) == CODEC_INVALID);
    put32(data + 20, 0x20000000u);
    assert(decode(full, 0) == CODEC_TRUNCATED);
    put32(data + 20, 1);

    /* Offsets and sizes pointing past the end. */
    put32(data + 28, 0xfffffffeu);
    assert(decode(full, 0) == CODEC_TRUNCATED);
    put32(data + 28, 32);
    put32(data + 32, 0xffffffffu);
    assert(decode(full, 0) == CODEC_TRUNCATED);
    put32(data + 32, 23);
    assert(decode(full, 0) == CODEC_INVALID);
    put32(data + 32, 24);
    assert(decode(full, 0) == CODEC_OK);
}

static void test_encode(void)
{
    static const uint8_t rgba[3 * 4] = {
        10, 20, 30, 255,
        200, 100, 0, 0,
        0, 0, 0, 128,
    };
    struct ftex_image im;
    assert(!ftex_make_header(0, 1, data));
    assert(!ftex_make_header(65536, 1, data));
    assert(!ftex_make_header(4097, 4097, data));
    assert(ftex_make_header(3, 1, data));
    ftex_encode_row(rgba, 3, data + FTEX_HEADER_SIZE);
    used = FTEX_HEADER_SIZE + 9;
    assert(count(used) == 1);
    assert(ftex_decode(data, used, 0, &im) == CODEC_OK);
    assert(im.width == 3 && im.height == 1);
    pixel(&im, 0, 0, 10, 20, 30, 255);
    pixel(&im, 1, 0, 255, 255, 255, 255);
    pixel(&im, 2, 0, 127, 127, 127, 255);
    ftex_free(&im);
}

int main(void)
{
    test_rgb_mips();
    test_dxt1();
    test_formats();
    test_levels();
    test_malformed();
    test_encode();
    puts("ftex: ok");
    return 0;
}
