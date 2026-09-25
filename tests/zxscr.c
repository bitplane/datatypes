#include "../formats/zxscr/decode.h"
#include "../formats/zxscr/encode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t data[ZXSCR_FILE_SIZE * 2 + 1];
static uint8_t saved[ZXSCR_FILE_SIZE];
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
    assert(zxscr_decode(data, ZXSCR_FILE_SIZE, image) == CODEC_OK);
    assert(image->width == 256 && image->height == 192);
}

/* Encode rgba, decode the result, and require identical pixels. */
static void round_trip(void)
{
    struct zxscr_image image;

    assert(zxscr_encode(rgba, 256, 192, saved) == CODEC_OK);
    assert(zxscr_decode(saved, sizeof saved, &image) == CODEC_OK);
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

static void test_sizes(void)
{
    static const size_t truncated[] = { 0, 1, 6143, 6144, 6911 };
    static const size_t longer[] = { 6913, 6976, 12288, 12289, 13824 };
    struct zxscr_image image;
    size_t i;

    memset(data, 0x55, sizeof data);
    for (i = 0; i < sizeof truncated / sizeof truncated[0]; i++) {
        assert(zxscr_decode(data, truncated[i], &image) == CODEC_TRUNCATED);
        assert(image.rgba == NULL && image.width == 0 && image.height == 0);
    }
    for (i = 0; i < sizeof longer / sizeof longer[0]; i++) {
        assert(zxscr_decode(data, longer[i], &image) == CODEC_INVALID);
        assert(image.rgba == NULL);
    }
    assert(zxscr_decode(NULL, ZXSCR_FILE_SIZE, &image) == CODEC_TRUNCATED);
    assert(zxscr_decode(data, ZXSCR_FILE_SIZE, NULL) == CODEC_INVALID);
    /* Any 6912 bytes are a valid screen. */
    decode_ok(&image);
    zxscr_free(&image);
    zxscr_free(&image);
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
        assert(zxscr_encode(image.rgba, 256, 192, data + ZXSCR_FILE_SIZE) == CODEC_OK);
        assert(memcmp(data + ZXSCR_FILE_SIZE, saved, ZXSCR_FILE_SIZE) == 0);
        zxscr_free(&image);
    }
}

static void test_encode(void)
{
    unsigned x, y;

    /* Plain white: bright white paper, nothing set. */
    memset(rgba, 255, sizeof rgba);
    assert(zxscr_encode(rgba, 256, 192, saved) == CODEC_OK);
    for (x = 0; x < 6144; x++)
        assert(saved[x] == 0);
    for (x = 6144; x < ZXSCR_FILE_SIZE; x++)
        assert(saved[x] == (0x40 | 7 << 3 | 7));

    /* Bright red and black in the top-left cell; black goes with bright.
       The lower colour is paper, so red is the ink. */
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++)
            set_rgba(x, y, x == y ? 0 : 255, 0, 0, 255);
    assert(zxscr_encode(rgba, 256, 192, saved) == CODEC_OK);
    assert(saved[6144] == (0x40 | 0 << 3 | 2));
    assert(saved[0] == 0x7F && saved[address(0, 7)] == 0xFE);
    round_trip();

    /* Transparent pixels composite over white. */
    memset(rgba, 0, sizeof rgba);
    assert(zxscr_encode(rgba, 256, 192, saved) == CODEC_OK);
    assert(saved[6144] == (0x40 | 7 << 3 | 7));

    /* Rejected: wrong size, non-Spectrum colours, three colours in a cell,
       bright and normal colours in one cell, half-transparent colours. */
    memset(rgba, 255, sizeof rgba);
    assert(zxscr_encode(rgba, 255, 192, saved) == CODEC_INVALID);
    assert(zxscr_encode(rgba, 256, 193, saved) == CODEC_INVALID);
    assert(zxscr_encode(NULL, 256, 192, saved) == CODEC_INVALID);
    set_rgba(9, 9, 205, 0, 0, 255);
    assert(zxscr_encode(rgba, 256, 192, saved) == CODEC_INVALID);
    set_rgba(9, 9, 0, 0, 255, 255);
    assert(zxscr_encode(rgba, 256, 192, saved) == CODEC_OK);
    set_rgba(10, 9, 255, 0, 0, 255);
    assert(zxscr_encode(rgba, 256, 192, saved) == CODEC_INVALID);
    set_rgba(10, 9, 0, 0, 255, 255);
    set_rgba(250, 190, 192, 0, 0, 255);
    assert(zxscr_encode(rgba, 256, 192, saved) == CODEC_INVALID);
    set_rgba(250, 190, 255, 255, 255, 255);
    set_rgba(100, 100, 0, 0, 0, 128);
    assert(zxscr_encode(rgba, 256, 192, saved) == CODEC_INVALID);
    set_rgba(100, 100, 0, 0, 0, 255);
    assert(zxscr_encode(rgba, 256, 192, saved) == CODEC_OK);
    round_trip();
}

int main(void)
{
    test_layout();
    test_attributes();
    test_sizes();
    test_round_trips();
    test_encode();
    puts("zxscr: ok");
    return 0;
}
