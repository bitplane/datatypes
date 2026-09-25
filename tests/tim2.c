#include "../formats/tim2/decode.h"
#include "../formats/tim2/encode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t file[1 << 20];
static size_t length;

static void put8(uint32_t v)
{
    file[length++] = (uint8_t)v;
}

static void put16(uint32_t v)
{
    put8(v);
    put8(v >> 8);
}

static void put32(uint32_t v)
{
    put16(v & 0xffffu);
    put16(v >> 16);
}

static void pad(size_t n)
{
    memset(file + length, 0, n);
    length += n;
}

static void set16(size_t at, uint32_t v)
{
    file[at] = (uint8_t)v;
    file[at + 1] = (uint8_t)(v >> 8);
}

static void set32(size_t at, uint32_t v)
{
    set16(at, v & 0xffffu);
    set16(at + 2, v >> 16);
}

/* File header; format 1 pads it to 128 bytes. */
static void header(unsigned format, unsigned pictures)
{
    length = 0;
    put8('T'); put8('I'); put8('M'); put8('2');
    put8(4);
    put8(format);
    put16(pictures);
    pad(8);
    if (format == 1)
        pad(112);
}

/* 48-byte picture header with sizes that add up; extra is user data or the
   mipmap header, which the caller writes next. */
static size_t picture(uint32_t clut_size, uint32_t image_size, unsigned extra,
                      unsigned colours, unsigned levels, unsigned clut_type,
                      unsigned image_type, unsigned width, unsigned height)
{
    size_t at = length;
    put32(48u + extra + image_size + clut_size);
    put32(clut_size);
    put32(image_size);
    put16(48u + extra);
    put16(colours);
    put8(0);
    put8(levels);
    put8(clut_type);
    put8(image_type);
    put16(width);
    put16(height);
    pad(24);
    return at;
}

static uint32_t c16(unsigned r, unsigned g, unsigned b, unsigned a)
{
    return r | (g << 5) | (b << 10) | (a << 15);
}

static void expect(const struct tim2_image *image, unsigned i,
                   unsigned r, unsigned g, unsigned b, unsigned a)
{
    const uint8_t *p = image->rgba + i * 4u;
    if (p[0] != r || p[1] != g || p[2] != b || p[3] != a) {
        fprintf(stderr, "pixel %u: %u %u %u %u, expected %u %u %u %u\n",
                i, p[0], p[1], p[2], p[3], r, g, b, a);
        abort();
    }
}

static void decode(unsigned index, struct tim2_image *image,
                   unsigned width, unsigned height)
{
    assert(tim2_decode(file, length, index, image) == CODEC_OK);
    assert(image->width == width && image->height == height);
}

/* Decoding every prefix shorter than end fails as truncated. */
static void truncations(size_t end)
{
    struct tim2_image image;
    size_t n, saved = length;
    uint8_t *copy = malloc(saved);
    assert(copy != NULL);
    memcpy(copy, file, saved);
    for (n = 0; n < end; n++) {
        /* An exact-size heap buffer, so ASan sees any overread. */
        uint8_t *prefix = malloc(n ? n : 1);
        assert(prefix != NULL);
        memcpy(prefix, copy, n);
        assert(tim2_decode(prefix, n, 0, &image) == CODEC_TRUNCATED);
        assert(image.rgba == NULL && image.width == 0);
        assert(tim2_count(prefix, n) == 0);
        free(prefix);
    }
    free(copy);
}

static void expect_result(unsigned index, enum codec_result want)
{
    struct tim2_image image;
    assert(tim2_decode(file, length, index, &image) == want);
    assert(image.rgba == NULL && image.width == 0 && image.height == 0);
}

/* 4-bit, 3x2, 16-bit CLUT. Pixels are one stream, so the second row starts
   in the middle of a byte; the image data is padded past them. */
static void four_bit(void)
{
    struct tim2_image image;
    unsigned i;
    header(0, 1);
    picture(32, 4, 0, 16, 1, 0x01, 4, 3, 2);
    put8(0x21); put8(0x03);
    put8(0xf4); put8(0x05);
    for (i = 0; i < 16; i++)
        put16(c16(i, 31u - i, i * 2u, i != 4));
    assert(tim2_count(file, length) == 1);
    decode(0, &image, 3, 2);
    expect(&image, 0, 8, 240, 16, 255);
    expect(&image, 1, 16, 232, 32, 255);
    expect(&image, 2, 24, 224, 48, 255);
    expect(&image, 3, 0, 248, 0, 255);
    expect(&image, 4, 32, 216, 64, 0);
    expect(&image, 5, 120, 128, 240, 255);
    tim2_free(&image);
    assert(image.rgba == NULL);
    truncations(length);
}

/* 8-bit, 256 entries of 32-bit colour. Without the linear flag the CLUT is
   in CSM1 order, which swaps entries 8-15 and 16-23 of every 32. */
static void eight_bit(int linear)
{
    struct tim2_image image;
    unsigned i, entry;
    header(0, 1);
    picture(1024, 256, 0, 256, 1, 0x03 | (linear ? 0x80 : 0), 5, 16, 16);
    for (i = 0; i < 256; i++)
        put8(i);
    for (i = 0; i < 256; i++) {
        put8(i); put8(255 - i); put8(i ^ 0x55); put8(i & 0xff);
    }
    decode(0, &image, 16, 16);
    for (i = 0; i < 256; i++) {
        entry = i;
        if (!linear && (i & 0x18) == 0x08) entry = i + 8;
        if (!linear && (i & 0x18) == 0x10) entry = i - 8;
        expect(&image, i, entry, 255 - entry, entry ^ 0x55,
               entry >= 128 ? 255 : entry * 2);
    }
    tim2_free(&image);
    if (!linear)
        truncations(length);
}

/* 24-bit CLUT entries, a 4-bit image with several palettes (the first is
   used), and a 16-colour CLUT is never rearranged. */
static void multi_palette(void)
{
    struct tim2_image image;
    unsigned i;
    header(0, 1);
    picture(3 * 48, 8, 0, 48, 1, 0x02, 4, 4, 4);
    for (i = 0; i < 8; i++)
        put8(((2 * i + 1) & 15u) << 4 | ((2 * i) & 15u));
    for (i = 0; i < 48; i++) {
        put8(i); put8(i + 100); put8(i + 200);
    }
    decode(0, &image, 4, 4);
    for (i = 0; i < 16; i++)
        expect(&image, i, i, i + 100, i + 200, 255);
    tim2_free(&image);
}

/* A short CLUT: missing entries are black. The colour count and the CLUT's
   byte size each limit it. */
static void short_clut(void)
{
    struct tim2_image image;
    header(0, 1);
    picture(8, 4, 0, 3, 1, 0x81, 5, 4, 1);
    put8(0); put8(1); put8(2); put8(3);
    put16(c16(31, 0, 0, 1)); put16(c16(0, 31, 0, 1));
    put16(c16(0, 0, 31, 1)); put16(c16(31, 31, 31, 1));
    decode(0, &image, 4, 1);
    expect(&image, 0, 248, 0, 0, 255);
    expect(&image, 1, 0, 248, 0, 255);
    expect(&image, 2, 0, 0, 248, 255);
    expect(&image, 3, 0, 0, 0, 255);
    tim2_free(&image);
    /* The colour count sizes the CLUT, not its byte size, which some files
       give as 0. */
    set32(16 + 4, 0);
    decode(0, &image, 4, 1);
    expect(&image, 2, 0, 0, 248, 255);
    tim2_free(&image);
    /* Colours past the end of the file are truncation. */
    set16(16 + 14, 5);
    expect_result(0, CODEC_TRUNCATED);
}

/* A 4-bit CSM1 CLUT with the compound flag holds 16-colour palettes in
   pairs, reordered like 256-colour ones: the first palette is entries 0-7
   and 16-23. CSM2 ignores the flag. */
static void compound(int csm2)
{
    struct tim2_image image;
    unsigned i;
    header(0, 1);
    picture(4 * 32, 8, 0, 32, 1, 0x43 | (csm2 ? 0x80 : 0), 4, 16, 1);
    for (i = 0; i < 8; i++)
        put8((2 * i + 1) << 4 | 2 * i);
    for (i = 0; i < 32; i++) {
        put8(i); put8(0); put8(0); put8(0x80);
    }
    decode(0, &image, 16, 1);
    for (i = 0; i < 16; i++)
        expect(&image, i, csm2 || i < 8 ? i : i + 8, 0, 0, 255);
    tim2_free(&image);
    /* Only the entries the first palette uses need be there. */
    if (!csm2) {
        set16(16 + 14, 24);
        length -= 32;
        decode(0, &image, 16, 1);
        expect(&image, 15, 23, 0, 0, 255);
        tim2_free(&image);
        length--;
        expect_result(0, CODEC_TRUNCATED);
    }
}

/* Indexed without a CLUT is a gray ramp. */
static void no_clut(void)
{
    struct tim2_image image;
    header(0, 1);
    picture(0, 2, 0, 0, 1, 0, 4, 4, 1);
    put8(0xf0); put8(0x81);
    decode(0, &image, 4, 1);
    expect(&image, 0, 0, 0, 0, 255);
    expect(&image, 1, 255, 255, 255, 255);
    expect(&image, 2, 17, 17, 17, 255);
    expect(&image, 3, 136, 136, 136, 255);
    tim2_free(&image);
    header(0, 1);
    picture(0, 2, 0, 16, 1, 0x80, 5, 2, 1);
    put8(0); put8(255);
    decode(0, &image, 2, 1);
    expect(&image, 0, 0, 0, 0, 255);
    expect(&image, 1, 255, 255, 255, 255);
    tim2_free(&image);
}

/* Every 16-bit value: 5-bit channels padded with zeros, bit 15 is alpha. */
static void sixteen_bit(void)
{
    struct tim2_image image;
    unsigned i;
    header(0, 1);
    picture(0, 65536 * 2, 0, 0, 1, 0, 1, 256, 256);
    for (i = 0; i < 65536; i++)
        put16(i);
    decode(0, &image, 256, 256);
    for (i = 0; i < 65536; i++)
        expect(&image, i, (i & 31) << 3, (i >> 5 & 31) << 3,
               (i >> 10 & 31) << 3, i & 0x8000 ? 255 : 0);
    tim2_free(&image);
}

static void twenty_four_bit(void)
{
    struct tim2_image image;
    unsigned i;
    header(0, 1);
    picture(0, 3 * 3, 0, 0, 1, 0, 2, 3, 1);
    for (i = 0; i < 9; i++)
        put8(i * 20);
    decode(0, &image, 3, 1);
    expect(&image, 0, 0, 20, 40, 255);
    expect(&image, 2, 120, 140, 160, 255);
    tim2_free(&image);
    truncations(length);
}

/* Every alpha value: 0x80 is opaque, more saturates. A direct-colour
   picture's CLUT is skipped, whatever its type. */
static void thirty_two_bit(void)
{
    struct tim2_image image;
    unsigned i;
    header(0, 1);
    picture(6, 256 * 4, 0, 3, 1, 0x7f, 3, 16, 16);
    for (i = 0; i < 256; i++) {
        put8(i); put8(i / 2); put8(255 - i); put8(i);
    }
    pad(6);
    decode(0, &image, 16, 16);
    for (i = 0; i < 256; i++)
        expect(&image, i, i, i / 2, 255 - i, i >= 128 ? 255 : i * 2);
    tim2_free(&image);
}

/* User data between the header and the image, as in files with an
   extended header, is skipped by the header size. */
static void user_data(void)
{
    struct tim2_image image;
    header(0, 1);
    picture(0, 4, 32, 0, 1, 0, 3, 1, 1);
    put8('e'); put8('X'); put8('t'); put8(0);
    pad(28);
    put8(10); put8(20); put8(30); put8(0x80);
    decode(0, &image, 1, 1);
    expect(&image, 0, 10, 20, 30, 255);
    tim2_free(&image);
}

/* Three levels of a 4x2 8-bit picture sharing one CLUT, then a second
   picture, in a file padded for 128-byte alignment. Images count in file
   order: every level of the first picture, then the second. */
static void mipmaps(void)
{
    struct tim2_image image;
    size_t second;
    unsigned i;
    header(1, 2);
    /* The mipmap header is two registers and a size per level, padded to
       16 bytes. The sizes include any padding after a level. */
    picture(4 * 4, 16 + 16 + 16, 32, 4, 3, 0x83, 5, 4, 2);
    pad(16);
    put32(16); put32(16); put32(16); put32(0);
    for (i = 0; i < 8; i++) put8(1);
    pad(8);
    put8(2); put8(2);
    pad(14);
    put8(3);
    pad(15);
    for (i = 0; i < 4; i++) {
        put8(i * 10); put8(0); put8(0); put8(0x80);
    }
    second = length;
    picture(0, 3, 0, 0, 1, 0, 2, 1, 1);
    put8(7); put8(8); put8(9);
    assert(tim2_count(file, length) == 4);
    decode(0, &image, 4, 2);
    expect(&image, 7, 10, 0, 0, 255);
    tim2_free(&image);
    decode(1, &image, 2, 1);
    expect(&image, 1, 20, 0, 0, 255);
    tim2_free(&image);
    decode(2, &image, 1, 1);
    expect(&image, 0, 30, 0, 0, 255);
    tim2_free(&image);
    decode(3, &image, 1, 1);
    expect(&image, 0, 7, 8, 9, 255);
    tim2_free(&image);
    expect_result(4, CODEC_INVALID);
    expect_result(0xffffffffu, CODEC_INVALID);
    truncations(second);

    /* Only complete pictures count, and asking for the cut-short one says so. */
    length--;
    assert(tim2_count(file, length) == 3);
    expect_result(3, CODEC_TRUNCATED);
    expect_result(4, CODEC_INVALID);
    decode(2, &image, 1, 1);
    tim2_free(&image);
    length++;

    /* The header's picture count limits the file; later data is ignored. */
    set16(6, 1);
    assert(tim2_count(file, length) == 3);
    expect_result(3, CODEC_INVALID);
    set16(6, 0);
    assert(tim2_count(file, length) == 0);
    expect_result(0, CODEC_INVALID);
    set16(6, 2);

    /* A bad second picture hides only itself. */
    file[second + 19] = 9;
    assert(tim2_count(file, length) == 3);
    expect_result(3, CODEC_INVALID);
    file[second + 19] = 2;

    /* A level smaller than its pixels, or sizes past the image data. */
    set32(128 + 48 + 16 + 4, 1);
    expect_result(0, CODEC_INVALID);
    set32(128 + 48 + 16 + 4, 17);
    expect_result(0, CODEC_INVALID);
    set32(128 + 48 + 16 + 4, 16);
    /* A header too small to hold the level sizes. */
    set16(128 + 12, 48 + 16 + 8);
    expect_result(0, CODEC_INVALID);
    set16(128 + 12, 48 + 32);
    decode(2, &image, 1, 1);
    tim2_free(&image);
}

static void invalid(void)
{
    struct tim2_image image;
    const size_t p = 16;

    header(0, 1);
    picture(0, 4, 0, 0, 1, 0, 3, 1, 1);
    pad(4);
    decode(0, &image, 1, 1);
    tim2_free(&image);

    file[0] = 't';
    expect_result(0, CODEC_INVALID);
    assert(tim2_count(file, length) == 0);
    file[0] = 'T';
    memcpy(file, "CLT2", 4);
    expect_result(0, CODEC_INVALID);
    memcpy(file, "TIM2", 4);
    file[5] = 2;
    expect_result(0, CODEC_INVALID);
    file[5] = 0;

    /* Image types 0 and 6 up, no levels or too many. */
    file[p + 19] = 0;
    expect_result(0, CODEC_INVALID);
    file[p + 19] = 6;
    expect_result(0, CODEC_INVALID);
    file[p + 19] = 3;
    /* No levels means a CLUT-only picture, but PS3 games write it for one. */
    file[p + 17] = 0;
    decode(0, &image, 1, 1);
    tim2_free(&image);
    file[p + 17] = 8;
    expect_result(0, CODEC_INVALID);
    file[p + 17] = 1;

    /* Zero sizes, and the 16M-pixel limit. */
    set16(p + 20, 0);
    expect_result(0, CODEC_INVALID);
    set16(p + 20, 65535);
    set16(p + 22, 257);
    expect_result(0, CODEC_TOO_LARGE);
    set16(p + 22, 256);
    expect_result(0, CODEC_INVALID); /* image data too small */
    set16(p + 20, 1);
    set16(p + 22, 1);

    /* A header size below 48. */
    set16(p + 12, 47);
    expect_result(0, CODEC_INVALID);
    set16(p + 12, 48);
    /* The total size is ignored: real files hold garbage there. */
    set32(p, 0);
    decode(0, &image, 1, 1);
    tim2_free(&image);
    set32(p, 0xfffffffdu);
    decode(0, &image, 1, 1);
    tim2_free(&image);
    /* Padding after the pixels may be cut off, even a huge amount. */
    set32(p + 8, 0xfffffff0u);
    decode(0, &image, 1, 1);
    tim2_free(&image);
    /* But a CLUT after it can't be there. */
    file[p + 19] = 5;
    file[p + 18] = 3;
    set16(p + 14, 256);
    set32(p + 4, 0xfffffff0u);
    expect_result(0, CODEC_TRUNCATED);
    set32(p + 8, 4);
    set32(p + 4, 0);
    set16(p + 14, 0);

    /* Unknown CLUT formats on an indexed picture. */
    file[p + 19] = 5;
    file[p + 18] = 4;
    expect_result(0, CODEC_INVALID);
    file[p + 18] = 0x3f;
    expect_result(0, CODEC_INVALID);
    /* The compound flag only applies to 4-bit pictures. */
    file[p + 18] = 0x40;
    decode(0, &image, 1, 1);
    tim2_free(&image);
    file[p + 18] = 0;
    decode(0, &image, 1, 1);
    tim2_free(&image);

    assert(tim2_decode(file, length, 0, NULL) == CODEC_INVALID);
    assert(tim2_decode(NULL, 0, 0, &image) == CODEC_TRUNCATED);
    assert(tim2_count(NULL, 0) == 0);
}

/* Written files decode to the source; translucent ones lose alpha's low bit. */
static void round_trip(int alpha, unsigned width, unsigned height)
{
    struct tim2_image image;
    uint8_t *rgba = malloc((size_t)width * height * 4u), *row;
    size_t size = tim2_row_size(width, alpha), i;
    unsigned y;

    assert(rgba != NULL);
    for (i = 0; i < (size_t)width * height * 4u; i++)
        rgba[i] = (uint8_t)(i * 7u + i / 5u);
    if (!alpha)
        for (i = 3; i < (size_t)width * height * 4u; i += 4)
            rgba[i] = 255;
    assert(tim2_make_header(width, height, alpha, file));
    length = TIM2_WRITE_HEADER;
    assert(file[35] == (alpha ? 3 : 2) && file[33] == 1);
    assert((file[42] >> 4 & 3) == (alpha ? 0u : 1u)); /* TEX0 PSM */
    assert((file[44] >> 2 & 1) == (unsigned)alpha);  /* TEX0 TCC */
    for (y = 0; y < height; y++) {
        row = file + length;
        tim2_encode_row(rgba + (size_t)y * width * 4u, width, alpha, row);
        length += size;
    }
    decode(0, &image, width, height);
    for (i = 0; i < (size_t)width * height * 4u; i++) {
        unsigned want = rgba[i];
        if (i % 4 == 3 && alpha)
            want = want == 255 ? 255 : (want + 1u) / 2u * 2u;
        assert(image.rgba[i] == want);
    }
    tim2_free(&image);
    free(rgba);
}

static void writer(void)
{
    uint8_t h[TIM2_WRITE_HEADER];
    round_trip(0, 1, 1);
    round_trip(0, 37, 11);
    round_trip(1, 16, 16);
    round_trip(1, 3, 100);
    assert(!tim2_make_header(0, 1, 0, h));
    assert(!tim2_make_header(1, 0, 1, h));
    assert(!tim2_make_header(65536, 1, 0, h));
    assert(!tim2_make_header(1, 65536, 0, h));
    /* The image size is 32-bit. */
    assert(tim2_make_header(65535, 20000, 0, h));
    assert(!tim2_make_header(65535, 20000, 1, h));
    assert(!tim2_make_header(65535, 65535, 0, h));
    /* TEX0's width and height fields stop at the GS's 1024. */
    assert(tim2_make_header(4096, 3, 0, h));
    assert((h[43] >> 2 & 15) == 10 && ((h[43] >> 6) | (h[44] & 3) << 2) == 2);
}

int main(void)
{
    four_bit();
    eight_bit(0);
    eight_bit(1);
    multi_palette();
    short_clut();
    compound(0);
    compound(1);
    no_clut();
    sixteen_bit();
    twenty_four_bit();
    thirty_two_bit();
    user_data();
    mipmaps();
    invalid();
    writer();
    puts("tim2: all tests passed");
    return 0;
}
