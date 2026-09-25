#include "../formats/neo/decode.h"
#include "../formats/neo/encode.h"
#include "common/atarist.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t data[NEO_FILE_SIZE + 16];
static uint8_t saved[NEO_FILE_SIZE];
static uint8_t rgba[640 * 400 * 4];

static void put16(size_t at, unsigned v)
{
    data[at] = (uint8_t)(v >> 8);
    data[at + 1] = (uint8_t)v;
}

static void header(unsigned resolution)
{
    memset(data, 0, sizeof data);
    put16(2, resolution);
}

/* Set pixel (x, y) to index in a file with this many planes. */
static void plot(unsigned planes, unsigned x, unsigned y, unsigned index)
{
    size_t group = 128 + (size_t)y * (planes == 1 ? 80u : 160u) + (x / 16u) * planes * 2u;
    unsigned p, bit = 15u - x % 16u;

    for (p = 0; p < planes; p++) {
        size_t at = group + p * 2u + (bit < 8u);
        data[at] = (uint8_t)(data[at] & ~(1u << bit % 8u));
        if (index >> p & 1u)
            data[at] |= (uint8_t)(1u << bit % 8u);
    }
}

static const uint8_t *pixel(const struct neo_image *image, unsigned x, unsigned y)
{
    return image->rgba + ((size_t)y * image->width + x) * 4u;
}

static void expect(const struct neo_image *image, unsigned x, unsigned y,
                   unsigned r, unsigned g, unsigned b)
{
    const uint8_t *p = pixel(image, x, y);
    assert(p[0] == r && p[1] == g && p[2] == b && p[3] == 255);
}

static void fill(unsigned width, unsigned height, const uint8_t (*colours)[3],
                 unsigned count)
{
    size_t i;

    for (i = 0; i < (size_t)width * height; i++) {
        const uint8_t *c = colours[(i * 7u + i / width) % count];
        rgba[i * 4u] = c[0];
        rgba[i * 4u + 1u] = c[1];
        rgba[i * 4u + 2u] = c[2];
        rgba[i * 4u + 3u] = 255;
    }
}

/* Encode rgba, decode the result, and require identical pixels. */
static void round_trip(unsigned width, unsigned height)
{
    struct neo_image image;

    assert(neo_encode(rgba, width, height, saved) == CODEC_OK);
    assert(neo_decode(saved, sizeof saved, &image) == CODEC_OK);
    assert(image.width == width && image.height == height);
    assert(memcmp(image.rgba, rgba, (size_t)width * height * 4u) == 0);
    neo_free(&image);
}

int main(void)
{
    static const uint8_t st[8] = { 0, 36, 73, 109, 146, 182, 219, 255 };
    uint8_t colours[17][3];
    struct neo_image image;
    unsigned i, x, y;
    size_t n;

    /* ST levels scale like netpbm's maxval 7; STE levels are 0..15 * 17. */
    for (i = 0; i < 8; i++) {
        assert(st_level(i, 0) == st[i]);
        assert(st_level(i | 8u, 0) == st[i]);
        assert(st_level(i, 1) == i * 34u);
        assert(st_level(i | 8u, 1) == i * 34u + 17u);
    }

    /* Low resolution: four interleaved planes, plane 0 the lowest bit. */
    header(0);
    for (i = 0; i < 16; i++)
        put16(4 + i * 2u, (i & 7u) << 8 | (7u - (i & 7u)) << 4 | (i >> 1));
    put16(4 + 15 * 2u, 0xf777); /* The top nibble is not colour. */
    for (i = 0; i < 16; i++)
        plot(4, i * 17u + 3u, i * 13u, i);
    plot(4, 319, 199, 9);
    assert(neo_decode(data, NEO_FILE_SIZE, &image) == CODEC_OK);
    assert(image.width == 320 && image.height == 200);
    for (i = 0; i < 15; i++)
        expect(&image, i * 17u + 3u, i * 13u, st[i & 7u], st[7u - (i & 7u)], st[i >> 1]);
    expect(&image, 15 * 17 + 3, 15 * 13, 255, 255, 255);
    expect(&image, 319, 199, st[1], st[6], st[4]);
    expect(&image, 0, 0, 0, 255, 0);
    expect(&image, 318, 199, 0, 255, 0);
    neo_free(&image);

    /* A fourth bit in any used colour makes the whole palette STE. */
    put16(4 + 15 * 2u, 0x0800);
    assert(neo_decode(data, NEO_FILE_SIZE, &image) == CODEC_OK);
    expect(&image, 0, 0, 0, 7 * 34, 0);
    expect(&image, 15 * 17 + 3, 15 * 13, 17, 0, 0);
    neo_free(&image);

    /* Medium resolution: two planes and four colours; unused entries with
       fourth bits (seen in real files) don't make the palette STE. */
    header(1);
    put16(4, 0x777); put16(6, 0x700); put16(8, 0x070); put16(10, 0x007);
    for (i = 4; i < 16; i++)
        put16(4 + i * 2u, 0xffff);
    for (x = 0; x < 640; x++)
        plot(2, x, x % 200u, x % 4u);
    assert(neo_decode(data, NEO_FILE_SIZE, &image) == CODEC_OK);
    assert(image.width == 640 && image.height == 200);
    for (x = 0; x < 640; x++) {
        static const uint8_t mid[4][3] = {
            { 255, 255, 255 }, { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 } };
        const uint8_t *c = mid[x % 4u];
        expect(&image, x, x % 200u, c[0], c[1], c[2]);
    }
    expect(&image, 1, 0, 255, 255, 255);
    neo_free(&image);
    put16(8, 0x078);
    assert(neo_decode(data, NEO_FILE_SIZE, &image) == CODEC_OK);
    expect(&image, 2, 2, 0, 238, 17);
    expect(&image, 0, 0, 238, 238, 238);
    neo_free(&image);

    /* High resolution: one plane, black on white whatever the palette says. */
    header(2);
    put16(4, 0x000); put16(6, 0x777);
    for (y = 0; y < 400; y++)
        plot(1, (y * 3u) % 640u, y, 1);
    assert(neo_decode(data, NEO_FILE_SIZE, &image) == CODEC_OK);
    assert(image.width == 640 && image.height == 400);
    for (y = 0; y < 400; y++) {
        expect(&image, (y * 3u) % 640u, y, 0, 0, 0);
        expect(&image, (y * 3u + 1u) % 640u, y, 255, 255, 255);
    }
    neo_free(&image);

    /* Trailing bytes are ignored. */
    assert(neo_decode(data, sizeof data, &image) == CODEC_OK);
    neo_free(&image);

    /* Truncation anywhere, header or pixels. */
    for (n = 0; n < NEO_FILE_SIZE; n++) {
        assert(neo_decode(data, n, &image) == CODEC_TRUNCATED);
        assert(image.rgba == NULL && image.width == 0);
    }
    assert(neo_decode(NULL, NEO_FILE_SIZE, &image) == CODEC_TRUNCATED);
    assert(neo_decode(data, NEO_FILE_SIZE, NULL) == CODEC_INVALID);

    /* Reserved flag values and resolutions. */
    put16(0, 1);
    assert(neo_decode(data, NEO_FILE_SIZE, &image) == CODEC_INVALID);
    assert(image.rgba == NULL);
    put16(0, 0x8000);
    assert(neo_decode(data, NEO_FILE_SIZE, &image) == CODEC_INVALID);
    put16(0, 0);
    put16(2, 3);
    assert(neo_decode(data, NEO_FILE_SIZE, &image) == CODEC_INVALID);
    put16(2, 0x0100);
    assert(neo_decode(data, NEO_FILE_SIZE, &image) == CODEC_INVALID);
    put16(2, 0xffff);
    assert(neo_decode(data, 4, &image) == CODEC_INVALID);

    /* Saving: every ST level in 16 colours, as a low resolution ST palette. */
    for (i = 0; i < 16; i++) {
        colours[i][0] = st[i & 7u];
        colours[i][1] = st[i >> 1];
        colours[i][2] = st[7u - (i & 7u)];
    }
    fill(320, 200, colours, 16);
    round_trip(320, 200);
    assert(saved[0] == 0 && saved[1] == 0 && saved[2] == 0 && saved[3] == 0);
    for (i = 0; i < 16; i++)
        assert(((saved[4 + i * 2u] << 8 | saved[5 + i * 2u]) & 0xf888u) == 0);
    assert(memcmp(saved + 36, "        .   ", 12) == 0);

    /* 17 colours don't fit. */
    colours[16][0] = 1; colours[16][1] = colours[16][2] = 0;
    fill(320, 200, colours, 17);
    assert(neo_encode(rgba, 320, 200, saved) == CODEC_INVALID);

    /* STE levels: odd ones mark the palette themselves. */
    for (i = 0; i < 16; i++) {
        colours[i][0] = (uint8_t)(i * 17u);
        colours[i][1] = (uint8_t)((15u - i) * 17u);
        colours[i][2] = 0;
    }
    fill(320, 200, colours, 16);
    round_trip(320, 200);

    /* Only even STE levels: an unused entry carries the fourth bit. */
    for (i = 0; i < 15; i++) {
        colours[i][0] = (uint8_t)((i % 8u) * 34u);
        colours[i][1] = (uint8_t)((i / 8u) * 34u);
        colours[i][2] = 34;
    }
    fill(320, 200, colours, 15);
    round_trip(320, 200);
    assert(saved[4 + 15 * 2u] == 0x08 && saved[5 + 15 * 2u] == 0x88);
    colours[15][0] = colours[15][1] = 102; colours[15][2] = 34;
    fill(320, 200, colours, 16);
    assert(neo_encode(rgba, 320, 200, saved) == CODEC_INVALID);

    /* Mixing an ST-only level with an STE-only one can't be stored. */
    colours[0][0] = 36; colours[0][1] = colours[0][2] = 0;
    colours[1][0] = 17; colours[1][1] = colours[1][2] = 0;
    fill(320, 200, colours, 2);
    assert(neo_encode(rgba, 320, 200, saved) == CODEC_INVALID);
    colours[1][0] = 100;
    fill(320, 200, colours, 2);
    assert(neo_encode(rgba, 320, 200, saved) == CODEC_INVALID);

    /* Medium resolution takes four colours, high resolution black and white. */
    for (i = 0; i < 4; i++)
        colours[i][0] = colours[i][1] = colours[i][2] = st[i * 2u + 1u];
    fill(640, 200, colours, 4);
    round_trip(640, 200);
    assert(saved[3] == 1);
    fill(640, 200, colours, 4);
    rgba[4] = 0;
    assert(neo_encode(rgba, 640, 200, saved) == CODEC_INVALID);
    colours[0][0] = colours[0][1] = colours[0][2] = 0;
    colours[1][0] = colours[1][1] = colours[1][2] = 255;
    fill(640, 400, colours, 2);
    round_trip(640, 400);
    assert(saved[3] == 2);
    memset(rgba, 0, 640u * 400u * 4u);
    for (n = 0; n < 640u * 400u; n++)
        rgba[n * 4u + 3u] = 255;
    round_trip(640, 400);
    rgba[0] = 146;
    assert(neo_encode(rgba, 640, 400, saved) == CODEC_INVALID);

    /* Transparency is composited over white. */
    memset(rgba, 0, 320u * 200u * 4u);
    rgba[3] = 255;
    assert(neo_encode(rgba, 320, 200, saved) == CODEC_OK);
    assert(neo_decode(saved, sizeof saved, &image) == CODEC_OK);
    expect(&image, 0, 0, 0, 0, 0);
    expect(&image, 1, 0, 255, 255, 255);
    neo_free(&image);

    /* Other sizes are not ST screens. */
    assert(neo_encode(rgba, 320, 199, saved) == CODEC_INVALID);
    assert(neo_encode(rgba, 640, 199, saved) == CODEC_INVALID);
    assert(neo_encode(rgba, 1, 1, saved) == CODEC_INVALID);
    assert(neo_encode(NULL, 320, 200, saved) == CODEC_INVALID);
    assert(neo_encode(rgba, 320, 200, NULL) == CODEC_INVALID);

    puts("neo: ok");
    return 0;
}
