#include "../formats/sgi/decode.h"
#include "../formats/sgi/encode.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t data[4096];

static void put16(uint8_t *p, unsigned value)
{
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static void put32(uint8_t *p, uint32_t value)
{
    put16(p, value >> 16);
    put16(p + 2, value & 0xffffu);
}

static void header(unsigned storage, unsigned bpc, unsigned dimension,
                   unsigned width, unsigned height, unsigned channels,
                   unsigned colormap)
{
    memset(data, 0, sizeof data);
    put16(data, 474);
    data[2] = (uint8_t)storage;
    data[3] = (uint8_t)bpc;
    put16(data + 4, dimension);
    put16(data + 6, width);
    put16(data + 8, height);
    put16(data + 10, channels);
    put32(data + 16, bpc == 1 ? 255 : 65535);
    put32(data + 104, colormap);
}

/* Entry i of an RLE file with the given number of scan lines. */
static void table(unsigned rows, unsigned i, uint32_t offset, uint32_t length)
{
    put32(data + 512 + i * 4u, offset);
    put32(data + 512 + (rows + i) * 4u, length);
}

static void expect(size_t length, const uint8_t *pixels,
                   unsigned width, unsigned height)
{
    struct sgi_image image;
    assert(sgi_decode(data, length, &image) == CODEC_OK);
    assert(image.width == width && image.height == height);
    assert(memcmp(image.rgba, pixels, (size_t)width * height * 4u) == 0);
    sgi_free(&image);
}

static void expect_result(size_t length, enum codec_result expected)
{
    struct sgi_image image;
    assert(sgi_decode(data, length, &image) == expected);
    assert(image.rgba == NULL);
}

static void verbatim(void)
{
    /* Two rows of RGB, stored bottom up in planes. */
    const uint8_t rgb[16] = {
        10, 40, 70, 255, 11, 41, 71, 255,
        0, 30, 60, 255, 1, 31, 61, 255
    };
    const uint8_t gray16[8] = {0x12, 0x12, 0x12, 255, 0xfe, 0xfe, 0xfe, 255};
    const uint8_t gray_alpha[4] = {99, 99, 99, 7};
    const uint8_t rgba[4] = {1, 2, 3, 4};
    const uint8_t line[8] = {5, 5, 5, 255, 6, 6, 6, 255};
    const uint8_t palette[8] = {1, 2, 3, 255, 4, 5, 6, 255};
    unsigned i;

    header(0, 1, 3, 2, 2, 3, 0);
    for (i = 0; i < 12; i++) /* planes: bottom row then top row */
        data[512 + i] = (uint8_t)((i / 4u) * 30u + (i & 2u) * 5u + (i & 1u));
    expect(524, rgb, 2, 2);
    expect_result(523, CODEC_TRUNCATED);

    header(0, 2, 2, 2, 1, 9, 0); /* 16-bit gray; dimension 2 ignores zsize */
    data[512] = 0x12; data[513] = 0x34;
    data[514] = 0xfe; data[515] = 0xff;
    expect(516, gray16, 2, 1);

    header(0, 1, 1, 2, 9, 9, 0); /* dimension 1 ignores ysize and zsize */
    data[512] = 5; data[513] = 6;
    expect(514, line, 2, 1);

    header(0, 1, 3, 1, 1, 2, 0);
    data[512] = 99; data[513] = 7;
    expect(514, gray_alpha, 1, 1);

    header(0, 1, 3, 1, 1, 4, 0);
    data[512] = 1; data[513] = 2; data[514] = 3; data[515] = 4;
    expect(516, rgba, 1, 1);
    put16(data + 10, 6); /* planes past alpha are ignored, even if missing */
    expect(516, rgba, 1, 1);

    header(0, 1, 3, 2, 1, 3, 3); /* a colormap file is shown as RGB */
    data[512] = 1; data[513] = 4; data[514] = 2;
    data[515] = 5; data[516] = 3; data[517] = 6;
    expect(518, palette, 2, 1);
}

static void colormaps(void)
{
    const uint8_t dithered[16] = {
        255, 0, 0, 255, 0, 255, 0, 255,
        0, 0, 255, 255, 255, 255, 255, 255
    };
    const uint8_t middle[4] = {146, 36, 170, 255};
    const uint8_t screen[4] = {42, 42, 42, 255};

    header(0, 1, 2, 4, 1, 1, 1);
    data[512] = 0x07; data[513] = 0x38; data[514] = 0xc0; data[515] = 0xff;
    expect(516, dithered, 4, 1);
    header(0, 1, 2, 1, 1, 1, 1);
    data[512] = 0x8c; /* red 4, green 1, blue 2 */
    expect(513, middle, 1, 1);
    header(0, 2, 2, 1, 1, 1, 1); /* dithered pixels are 8-bit */
    expect_result(514, CODEC_INVALID);

    /* Screen images are colour indexes without their palette: show as gray. */
    header(0, 1, 3, 1, 1, 3, 2);
    data[512] = 42;
    expect(513, screen, 1, 1);
}

static void rle(void)
{
    const uint8_t runs[20] = {
        7, 7, 7, 255, 7, 7, 7, 255, 7, 7, 7, 255,
        1, 1, 1, 255, 2, 2, 2, 255
    };
    const uint8_t two_rows[8] = {9, 9, 9, 200, 3, 3, 3, 100};
    const uint8_t sixteen[8] = {0xab, 0xab, 0xab, 255, 0xcd, 0xcd, 0xcd, 255};
    const uint8_t rgb[4] = {10, 20, 30, 255};

    header(1, 1, 2, 5, 1, 1, 0);
    table(1, 0, 520, 6);
    memcpy(data + 520, "\x03\x07\x82\x01\x02\x00", 6);
    expect(526, runs, 5, 1);
    table(1, 0, 520, 5); /* a full row may omit its terminator */
    expect(525, runs, 5, 1);

    /* Gray + alpha, two rows, bottom up, rows shared through the tables. */
    header(1, 1, 3, 1, 2, 2, 0);
    table(4, 0, 560, 3); /* bottom gray */
    table(4, 1, 563, 3); /* top gray */
    table(4, 2, 566, 3); /* bottom alpha */
    table(4, 3, 569, 3); /* top alpha */
    memcpy(data + 560, "\x01\x03\x00\x01\x09\x00\x01\x64\x00\x01\xc8\x00", 12);
    expect(572, two_rows, 1, 2);

    header(1, 2, 2, 2, 1, 1, 0);
    table(1, 0, 520, 10);
    memcpy(data + 520, "\x00\x81\xab\x00\x00\x81\xcd\x00\x00\x00", 10);
    expect(530, sixteen, 2, 1);
    table(1, 0, 520, 8);
    memcpy(data + 520, "\xff\x82\xab\x00\xcd\x00\x00\x00", 8); /* top byte of count is ignored */
    expect(528, sixteen, 2, 1);
    table(1, 0, 520, 9); /* odd trailing byte, row already full */
    memcpy(data + 520, "\x00\x81\xab\x00\x00\x81\xcd\x00\x00", 9);
    expect(529, sixteen, 2, 1);

    header(1, 1, 3, 1, 1, 5, 0); /* channels past RGBA need no row data */
    table(5, 0, 560, 3);
    table(5, 1, 563, 3);
    table(5, 2, 566, 3);
    table(5, 3, 569, 3);
    table(5, 4, 0xffffffffu, 0xffffffffu);
    memcpy(data + 560, "\x01\x0a\x00\x01\x14\x00\x01\x1e\x00\x01\xff\x00", 12);
    expect(572, rgb, 1, 1);
}

static void rle_errors(void)
{
    header(1, 1, 3, 2, 2, 2, 0);
    expect_result(512 + 31, CODEC_TRUNCATED); /* tables cut short */

    header(1, 1, 2, 2, 1, 1, 0);
    table(1, 0, 600, 3);
    expect_result(520, CODEC_TRUNCATED); /* offset past the end */
    table(1, 0, 519, 3);
    expect_result(520, CODEC_TRUNCATED); /* length past the end */

    table(1, 0, 520, 4);
    memcpy(data + 520, "\x03\x07\x00\x00", 4);
    expect_result(524, CODEC_INVALID); /* run longer than the row */
    memcpy(data + 520, "\x83\x07\x00\x00", 4);
    expect_result(524, CODEC_INVALID);
    memcpy(data + 520, "\x82\x07\x00\x00", 4);
    table(1, 0, 520, 2);
    expect_result(524, CODEC_INVALID); /* literals past the row data */
    memcpy(data + 520, "\x02\x07\x00\x00", 4);
    table(1, 0, 520, 1);
    expect_result(524, CODEC_INVALID); /* run missing its value */
    table(1, 0, 520, 4);
    memcpy(data + 520, "\x01\x07\x00\x00", 4);
    expect_result(524, CODEC_INVALID); /* row ends early */
    table(1, 0, 520, 0);
    expect_result(524, CODEC_INVALID); /* empty row */

    header(1, 2, 2, 1, 1, 1, 0);
    table(1, 0, 520, 3);
    memcpy(data + 520, "\x00\x01\x00", 3);
    expect_result(523, CODEC_INVALID); /* 16-bit run value cut short */
}

static void header_errors(void)
{
    struct sgi_image image;

    header(0, 1, 2, 1, 1, 1, 0);
    data[512] = 1;
    expect_result(511, CODEC_TRUNCATED);
    assert(sgi_decode(NULL, 600, &image) == CODEC_TRUNCATED);
    assert(sgi_decode(data, 513, NULL) == CODEC_INVALID);
    sgi_free(NULL);

    data[1] = 0xdb;
    expect_result(513, CODEC_INVALID);
    header(2, 1, 2, 1, 1, 1, 0);
    expect_result(513, CODEC_INVALID);
    header(0, 0, 2, 1, 1, 1, 0);
    expect_result(513, CODEC_INVALID);
    header(0, 3, 2, 1, 1, 1, 0);
    expect_result(513, CODEC_INVALID);
    header(0, 1, 0, 1, 1, 1, 0);
    expect_result(513, CODEC_INVALID);
    header(0, 1, 4, 1, 1, 1, 0);
    expect_result(513, CODEC_INVALID);
    header(0, 1, 2, 1, 1, 1, 4);
    expect_result(513, CODEC_INVALID);
    header(0, 1, 2, 0, 1, 1, 0);
    expect_result(513, CODEC_INVALID);
    header(0, 1, 2, 1, 0, 1, 0);
    expect_result(513, CODEC_INVALID);
    header(0, 1, 3, 1, 1, 0, 0);
    expect_result(513, CODEC_INVALID);
    header(0, 1, 2, 65535, 65535, 1, 0);
    expect_result(513, CODEC_TOO_LARGE);
    header(1, 1, 3, 1, 65535, 65535, 0); /* 32 GiB of tables */
    expect_result(513, CODEC_TRUNCATED);
}

/* Build a whole RLE file the way the class does and decode it again. */
static void round_trip(const uint8_t *rgba, unsigned width, unsigned height,
                       unsigned channels)
{
    uint32_t *lengths = malloc((size_t)height * 4u * sizeof *lengths);
    size_t capacity = sgi_rle_capacity(width), pos, size;
    uint8_t *file = malloc(SGI_HEADER_SIZE + (size_t)height * 8u * 4u +
                           (size_t)height * 4u * capacity);
    struct sgi_image image;
    unsigned needs = 0, y, c;

    assert(lengths != NULL && file != NULL);
    for (y = 0; y < height; y++) {
        const uint8_t *row = rgba + (size_t)y * width * 4u;
        needs |= sgi_row_needs(row, width);
        for (c = 0; c < 4; c++) {
            size = sgi_encode_rle(row, width, c, file, capacity);
            assert(size != 0 && size <= capacity);
            lengths[y * 4u + c] = (uint32_t)size;
        }
    }
    assert(sgi_channels(needs) == channels);
    assert(sgi_make_header(width, height, channels, file));
    assert(sgi_make_tables(lengths, height, channels, file + SGI_HEADER_SIZE));
    pos = SGI_HEADER_SIZE + (size_t)height * channels * 8u;
    for (y = 0; y < height; y++) {
        for (c = 0; c < channels; c++) {
            pos += sgi_encode_rle(rgba + (size_t)y * width * 4u, width, c,
                                  file + pos, capacity);
        }
    }
    assert(sgi_decode(file, pos, &image) == CODEC_OK);
    assert(image.width == width && image.height == height);
    assert(memcmp(image.rgba, rgba, (size_t)width * height * 4u) == 0);
    sgi_free(&image);
    free(file);
    free(lengths);
}

static void encoder(void)
{
    static uint8_t pixels[300 * 3 * 4];
    const unsigned widths[] = {1, 2, 3, 126, 127, 128, 129, 254, 300};
    uint8_t out[400], head[SGI_HEADER_SIZE];
    uint32_t lengths[4] = {100, 100, 100, 100};
    unsigned w, pattern, x, i;

    for (i = 0; i < sizeof widths / sizeof widths[0]; i++) {
        w = widths[i];
        for (pattern = 0; pattern < 4; pattern++) {
            for (x = 0; x < w * 3u; x++) {
                uint8_t *p = pixels + (size_t)x * 4u;
                /* flat, all distinct, pairs, and runs broken by singles */
                unsigned v = pattern == 0 ? 7u : pattern == 1 ? x
                           : pattern == 2 ? x / 2u : (x % 5u == 4u ? x : x / 5u);
                p[0] = p[1] = p[2] = (uint8_t)v;
                p[3] = 255;
            }
            round_trip(pixels, w, 3, 1);
            pixels[3] = 0; /* gray with alpha is saved as RGBA */
            round_trip(pixels, w, 3, 4);
            pixels[3] = 255;
            pixels[1] = 1;
            round_trip(pixels, w, 3, 3);
            pixels[3] = 0;
            round_trip(pixels, w, 3, 4);
        }
    }

    {
        const uint8_t blue[8] = {5, 5, 6, 255, 5, 5, 5, 0};
        assert(sgi_row_needs(blue, 1) == SGI_NEEDS_COLOR);
        assert(sgi_row_needs(blue + 4, 1) == SGI_NEEDS_ALPHA);
    }

    /* Flat rows compress to runs; distinct rows need one header per 127. */
    memset(pixels, 7, sizeof pixels);
    assert(sgi_encode_rle(pixels, 300, 0, out, sizeof out) == 7);
    for (x = 0; x < 300; x++)
        pixels[x * 4u] = (uint8_t)x;
    assert(sgi_encode_rle(pixels, 300, 0, out, sizeof out) == 300 + 3 + 1);
    assert(sgi_rle_capacity(300) <= sizeof out);

    assert(sgi_encode_rle(pixels, 300, 0, out, sgi_rle_capacity(300) - 1) == 0);
    assert(sgi_encode_rle(NULL, 1, 0, out, sizeof out) == 0);
    assert(sgi_encode_rle(pixels, 1, 0, NULL, sizeof out) == 0);
    assert(sgi_encode_rle(pixels, 1, 4, out, sizeof out) == 0);

    assert(sgi_make_header(1, 1, 1, head));
    assert(head[0] == 1 && head[1] == 0xda && head[2] == 1 && head[3] == 1);
    assert(head[5] == 2 && head[11] == 1 && head[19] == 255);
    assert(sgi_make_header(1, 1, 4, head) && head[5] == 3 && head[11] == 4);
    assert(!sgi_make_header(0, 1, 1, head));
    assert(!sgi_make_header(1, 0, 1, head));
    assert(!sgi_make_header(65536, 1, 1, head));
    assert(!sgi_make_header(1, 65536, 1, head));
    assert(!sgi_make_header(1, 1, 0, head));
    assert(!sgi_make_header(1, 1, 2, head));
    assert(!sgi_make_header(1, 1, 5, head));

    assert(!sgi_make_tables(lengths, 1, 0, out));
    assert(!sgi_make_tables(lengths, 1, 2, out));
    assert(!sgi_make_tables(lengths, 1, 5, out));
    lengths[1] = UINT32_MAX - 600;
    assert(!sgi_make_tables(lengths, 1, 3, out));
}

int main(void)
{
    verbatim();
    colormaps();
    rle();
    rle_errors();
    header_errors();
    encoder();
    puts("sgi codec tests passed");
    return 0;
}
