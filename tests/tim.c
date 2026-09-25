#include "../formats/tim/decode.h"
#include "../formats/tim/encode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t file[4096];
static size_t length;

static void put16(uint32_t v)
{
    file[length++] = (uint8_t)v;
    file[length++] = (uint8_t)(v >> 8);
}

static void put32(uint32_t v)
{
    put16(v & 0xffffu);
    put16(v >> 16);
}

static void header(uint32_t flag)
{
    put32(0x10);
    put32(flag);
}

/* Block header; bytes is what the byte count claims. */
static void block(uint32_t bytes, unsigned w16, unsigned height)
{
    put32(bytes);
    put16(0);
    put16(0);
    put16(w16);
    put16(height);
}

static uint32_t c15(unsigned r, unsigned g, unsigned b, unsigned stp)
{
    return r | (g << 5) | (b << 10) | (stp << 15);
}

/* 5-bit to 8-bit by bit replication, as ImageMagick does. */
static uint8_t five(unsigned v)
{
    return (uint8_t)((v << 3) | (v >> 2));
}

static void expect(const struct tim_image *image, unsigned i,
                   unsigned r, unsigned g, unsigned b)
{
    const uint8_t *p = image->rgba + i * 4u;
    assert(p[0] == r && p[1] == g && p[2] == b && p[3] == 255);
}

/* Decoding every shorter prefix fails as truncated. */
static void truncations(void)
{
    struct tim_image image;
    size_t n;
    for (n = 0; n < length; n++) {
        assert(tim_decode(file, n, 0, &image) == CODEC_TRUNCATED);
        assert(image.rgba == NULL && image.width == 0);
        assert(tim_count(file, n) == 0);
    }
}

/* 4-bit, 8x2, with a CLUT of two 16-colour palettes. */
static void four_bit(uint32_t flag, int clut)
{
    unsigned i;
    header(flag | (clut ? 8u : 0u));
    if (clut) {
        block(12 + 64, 16, 2);
        for (i = 0; i < 16; i++) put16(c15(i * 2u, 31u - i * 2u, i, i & 1u));
        for (i = 0; i < 16; i++) put16(c15(31, 0, 0, 0));
    }
    block(12 + 8, 2, 2);
    for (i = 0; i < 8; i++)
        file[length++] = (uint8_t)((i * 2u) | ((i * 2u + 1u) << 4));
}

int main(void)
{
    struct tim_image image;
    uint8_t out[64], head[20], rgba[5 * 4];
    unsigned i, v;

    /* 4-bit: low nibble first, rows top down, first palette only. */
    length = 0;
    four_bit(0, 1);
    assert(tim_count(file, length) == 1);
    assert(tim_decode(file, length, 0, &image) == CODEC_OK);
    assert(image.width == 8 && image.height == 2);
    for (i = 0; i < 16; i++)
        expect(&image, i, five(i * 2u), five(31u - i * 2u), five(i));
    tim_free(&image);
    truncations();

    /* Reserved flag bits and a wrong byte count are ignored. */
    length = 0;
    four_bit(0xabcd00u, 1);
    file[8] = 0xff;
    file[length - 20] = 3;
    assert(tim_decode(file, length, 0, &image) == CODEC_OK);
    expect(&image, 15, five(30), five(1), five(15));
    tim_free(&image);

    /* Without a CLUT, indexes are a gray ramp. */
    length = 0;
    four_bit(0, 0);
    assert(tim_decode(file, length, 0, &image) == CODEC_OK);
    for (i = 0; i < 16; i++)
        expect(&image, i, i * 17u, i * 17u, i * 17u);
    tim_free(&image);

    /* 8-bit with a full CLUT. */
    length = 0;
    header(1 | 8);
    block(12 + 512, 256, 1);
    for (i = 0; i < 256; i++) put16(c15(i & 31u, (i >> 3) & 31u, 31u - (i & 31u), i & 1u));
    block(12 + 6, 1, 3);
    file[length++] = 0; file[length++] = 1; file[length++] = 255;
    file[length++] = 128; file[length++] = 7; file[length++] = 200;
    assert(tim_decode(file, length, 0, &image) == CODEC_OK);
    assert(image.width == 2 && image.height == 3);
    expect(&image, 0, 0, 0, 255);
    expect(&image, 2, 255, 255, 0);
    expect(&image, 3, 0, five(16), 255);
    expect(&image, 5, five(8), five(25), five(23));
    tim_free(&image);
    truncations();

    /* 8-bit with a short CLUT: missing entries are black. */
    length = 0;
    header(1 | 8);
    block(12 + 4, 2, 1);
    put16(c15(31, 31, 31, 0)); put16(c15(0, 31, 0, 0));
    block(12 + 4, 2, 1);
    file[length++] = 0; file[length++] = 1; file[length++] = 2; file[length++] = 255;
    assert(tim_decode(file, length, 0, &image) == CODEC_OK);
    expect(&image, 0, 255, 255, 255);
    expect(&image, 1, 0, 255, 0);
    expect(&image, 2, 0, 0, 0);
    expect(&image, 3, 0, 0, 0);
    tim_free(&image);

    /* An empty CLUT block counts as a CLUT with no entries. */
    length = 0;
    header(1 | 8);
    block(12, 0, 0);
    block(12 + 2, 1, 1);
    file[length++] = 9; file[length++] = 0;
    assert(tim_decode(file, length, 0, &image) == CODEC_OK);
    expect(&image, 0, 0, 0, 0);
    tim_free(&image);

    /* 8-bit without a CLUT is gray. */
    length = 0;
    header(1);
    block(12 + 2, 1, 1);
    file[length++] = 9; file[length++] = 250;
    assert(tim_decode(file, length, 0, &image) == CODEC_OK);
    expect(&image, 0, 9, 9, 9);
    expect(&image, 1, 250, 250, 250);
    tim_free(&image);

    /* 16-bit: every value, with STP and black both opaque. */
    for (v = 0; v < 65536; v++) {
        length = 0;
        header(2);
        block(12 + 2, 1, 1);
        put16(v);
        assert(tim_decode(file, length, 0, &image) == CODEC_OK);
        expect(&image, 0, five(v & 31u), five((v >> 5) & 31u), five((v >> 10) & 31u));
        tim_free(&image);
    }
    /* Values ImageMagick 7.1.2 gives. */
    assert(five(1) == 8 && five(3) == 24 && five(10) == 82 &&
           five(20) == 165 && five(30) == 247 && five(31) == 255);

    /* 16-bit with a CLUT it doesn't use. */
    length = 0;
    header(2 | 8);
    block(12 + 32, 16, 1);
    for (i = 0; i < 16; i++) put16(c15(0, 31, 0, 0));
    block(12 + 12, 3, 2);
    for (i = 0; i < 6; i++) put16(c15(i, 2u * i, 3u * i, i & 1u));
    assert(tim_decode(file, length, 0, &image) == CODEC_OK);
    assert(image.width == 3 && image.height == 2);
    for (i = 0; i < 6; i++)
        expect(&image, i, five(i), five(2u * i), five(3u * i));
    tim_free(&image);
    truncations();

    /* 24-bit: RGB order, rows padded to 16 bits. */
    length = 0;
    header(3);
    block(12 + 20, 5, 2);
    for (i = 0; i < 2; i++) {
        file[length++] = (uint8_t)(10 + i); file[length++] = 20; file[length++] = 30;
        file[length++] = 40; file[length++] = (uint8_t)(50 + i); file[length++] = 60;
        file[length++] = 70; file[length++] = 80; file[length++] = (uint8_t)(90 + i);
        file[length++] = 0xee;
    }
    assert(tim_decode(file, length, 0, &image) == CODEC_OK);
    assert(image.width == 3 && image.height == 2);
    expect(&image, 0, 10, 20, 30);
    expect(&image, 2, 70, 80, 90);
    expect(&image, 3, 11, 20, 30);
    expect(&image, 5, 70, 80, 91);
    tim_free(&image);
    truncations();

    /* Several images back to back, then trailing padding. */
    length = 0;
    four_bit(0, 1);
    header(2);
    block(12 + 12, 3, 2);
    for (i = 0; i < 6; i++) put16(c15(31, 31, i, 0));
    header(1);
    block(12 + 2, 1, 1);
    file[length++] = 77; file[length++] = 88;
    memset(file + length, 0, 32);
    length += 32;
    assert(tim_count(file, length) == 3);
    assert(tim_decode(file, length, 1, &image) == CODEC_OK);
    assert(image.width == 3 && image.height == 2);
    expect(&image, 5, 255, 255, five(5));
    tim_free(&image);
    assert(tim_decode(file, length, 2, &image) == CODEC_OK);
    assert(image.width == 2 && image.height == 1);
    expect(&image, 1, 88, 88, 88);
    tim_free(&image);
    assert(tim_decode(file, length, 3, &image) == CODEC_INVALID);
    assert(image.rgba == NULL);
    assert(tim_decode(file, length, 99, &image) == CODEC_INVALID);
    assert(tim_decode(file, length, 0xffffffffu, &image) == CODEC_INVALID);
    /* A truncated later image is not counted and fails when asked for. */
    length -= 33;
    assert(tim_count(file, length) == 2);
    assert(tim_decode(file, length, 0, &image) == CODEC_OK);
    tim_free(&image);
    assert(tim_decode(file, length, 2, &image) == CODEC_TRUNCATED);

    /* Bad magic, mixed and undefined modes. */
    length = 0;
    header(2);
    block(12 + 2, 1, 1);
    put16(0);
    for (v = 4; v < 8; v++) {
        file[4] = (uint8_t)v;
        assert(tim_decode(file, length, 0, &image) == CODEC_INVALID);
        assert(tim_count(file, length) == 0);
    }
    file[4] = 2;
    file[1] = 1;
    assert(tim_decode(file, length, 0, &image) == CODEC_INVALID);
    file[1] = 0;
    file[0] = 0x11;
    assert(tim_decode(file, length, 0, &image) == CODEC_INVALID);
    file[0] = 0x10;

    /* Empty images, including 24-bit narrower than one pixel. */
    length = 0;
    header(2);
    block(12, 0, 1);
    assert(tim_decode(file, length, 0, &image) == CODEC_INVALID);
    length = 0;
    header(2);
    block(12, 1, 0);
    assert(tim_decode(file, length, 0, &image) == CODEC_INVALID);
    length = 0;
    header(3);
    block(12 + 2, 1, 1);
    put16(0);
    assert(tim_decode(file, length, 0, &image) == CODEC_INVALID);

    /* Size limits come before truncation. */
    length = 0;
    header(0);
    block(12, 16384, 1);
    assert(tim_decode(file, length, 0, &image) == CODEC_TOO_LARGE);
    length = 0;
    header(0);
    block(12, 16383, 1);
    assert(tim_decode(file, length, 0, &image) == CODEC_TRUNCATED);
    length = 0;
    header(2);
    block(12, 65535, 65535);
    assert(tim_decode(file, length, 0, &image) == CODEC_TOO_LARGE);
    length = 0;
    header(2);
    block(12, 4096, 4096);
    assert(tim_decode(file, length, 0, &image) == CODEC_TRUNCATED);
    /* A huge CLUT can't overflow the offset arithmetic. */
    length = 0;
    header(0 | 8);
    block(0xffffffffu, 65535, 65535);
    assert(tim_decode(file, length, 0, &image) == CODEC_TRUNCATED);
    assert(tim_decode(NULL, 0, 0, &image) == CODEC_TRUNCATED);
    assert(tim_decode(file, length, 0, NULL) == CODEC_INVALID);
    assert(tim_count(NULL, 0) == 0);

    /* Writing: 24-bit, odd rows padded, transparency over white. */
    assert(tim_row_size(1) == 4 && tim_row_size(2) == 6 && tim_row_size(5) == 16);
    assert(tim_make_header(5, 3, head));
    assert(memcmp(head, "\x10\0\0\0\x03\0\0\0", 8) == 0);
    assert(head[8] == 12 + 48 && head[9] == 0 && head[16] == 8 && head[18] == 3);
    assert(!tim_make_header(0, 1, head) && !tim_make_header(1, 0, head));
    assert(tim_make_header(43690, 1, head) && !tim_make_header(43691, 1, head));
    assert(head[16] == 0xff && head[17] == 0xff);
    assert(!tim_make_header(1, 65536, head));
    for (i = 0; i < 5; i++) {
        rgba[i * 4u] = (uint8_t)(i * 50u);
        rgba[i * 4u + 1u] = (uint8_t)(255u - i * 50u);
        rgba[i * 4u + 2u] = (uint8_t)(i * 7u);
        rgba[i * 4u + 3u] = 255;
    }
    rgba[3] = 0;
    rgba[7] = 128;
    memset(out, 0xaa, sizeof out);
    tim_encode_row(rgba, 5, out);
    assert(out[0] == 255 && out[1] == 255 && out[2] == 255);
    assert(out[3] == (50 * 128 + 255 * 127 + 127) / 255);
    assert(out[15] == 0 && out[16] == 0xaa);

    /* A written file decodes back to the opaque pixels. */
    rgba[3] = rgba[7] = 255;
    length = 0;
    assert(tim_make_header(5, 1, head));
    memcpy(file, head, 20);
    tim_encode_row(rgba, 5, file + 20);
    length = 20 + tim_row_size(5);
    assert(tim_count(file, length) == 1);
    assert(tim_decode(file, length, 0, &image) == CODEC_OK);
    assert(image.width == 5 && image.height == 1);
    assert(memcmp(image.rgba, rgba, sizeof rgba) == 0);
    tim_free(&image);

    puts("tim: ok");
    return 0;
}
