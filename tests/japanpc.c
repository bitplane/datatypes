#include "../formats/japanpc/decode.h"
#include "../formats/japanpc/encode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t file[1 << 20];
static size_t length;
static unsigned bit_count;

static void put8(unsigned v) { file[length++] = (uint8_t)v; }
static void put16le(unsigned v) { put8(v & 255u); put8(v >> 8); }
static void put16be(unsigned v) { put8(v >> 8); put8(v & 255u); }
static void put32le(unsigned long v) { put16le(v & 0xffffu); put16le(v >> 16); }
static void puts8(const char *s) { while (*s) put8((uint8_t)*s++); }

/* Bits, most significant first, from a string of 0s and 1s; spaces are
   ignored. */
static void bits(const char *s)
{
    for (; *s; s++) {
        if (*s == ' ')
            continue;
        if (bit_count % 8u == 0)
            file[length++] = 0;
        if (*s == '1')
            file[length - 1] |= (uint8_t)(0x80u >> bit_count % 8u);
        bit_count++;
    }
}

static void value_bits(unsigned long v, unsigned n)
{
    while (n-- > 0)
        bits(v >> n & 1u ? "1" : "0");
}

static void align(void) { bit_count = 0; }

static uint32_t rgb_at(const struct japanpc_image *image, unsigned x,
                       unsigned y)
{
    const uint8_t *p = image->rgba + ((size_t)y * image->width + x) * 4u;
    assert(p[3] == 255);
    return (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
}

static void decode_ok(struct japanpc_image *image, unsigned width,
                      unsigned height)
{
    enum codec_result r = japanpc_decode(file, length, image);
    if (r != CODEC_OK)
        fprintf(stderr, "decode failed: %d\n", r);
    assert(r == CODEC_OK);
    assert(image->width == width && image->height == height);
}

static enum codec_result decode_result(void)
{
    struct japanpc_image image;
    enum codec_result r = japanpc_decode(file, length, &image);
    if (r == CODEC_OK)
        japanpc_free(&image);
    else
        assert(image.rgba == NULL);
    return r;
}

/* Every shorter prefix of the file is truncated. */
static void truncations(void)
{
    size_t full = length;
    for (length = 0; length < full; length++) {
        enum codec_result r = decode_result();
        if (r != CODEC_TRUNCATED)
            fprintf(stderr, "%zu of %zu bytes: %d\n", length, full, r);
        assert(r == CODEC_TRUNCATED);
    }
    length = full;
}

/* MAG ------------------------------------------------------------------ */

static unsigned mag_stride(unsigned x0, unsigned x1, int deep)
{
    return deep ? (x1 | 3u) - (x0 & ~3u) + 1u
                : ((x1 | 7u) - (x0 & ~7u) + 1u) / 2u;
}

/* A MAG whose pixels are all stored, unless flag_b gives changes: then
   flag A is its first byte and flag B the rest. Palette entry i is red i,
   green 255 - i, blue 7. */
static void mag(unsigned machine, unsigned flags, unsigned mode, unsigned x0,
                unsigned y0, unsigned x1, unsigned y1, const uint8_t *pixels,
                size_t pixel_size, const uint8_t *flag_b, size_t flag_b_size)
{
    int deep = (mode & 0x80u) != 0;
    unsigned colours = deep ? 256 : 16, i;
    size_t header, a_at, a_size, b_at, p_at;
    size_t lines = y1 - y0 + 1u;
    size_t a_bits = mag_stride(x0, x1, deep) / 4u * lines;

    length = 0;
    puts8("MAKI02  TEST                  \x1a");
    header = length;
    a_at = 32u + colours * 3u;
    a_size = (a_bits + 7u) / 8u;
    a_size += a_size & 1u;
    b_at = a_at + a_size;
    p_at = b_at + (flag_b_size ? flag_b_size - 1u : 0);
    put8(0); put8(machine); put8(flags); put8(mode);
    put16le(x0); put16le(y0); put16le(x1); put16le(y1);
    put32le(a_at); put32le(b_at); put32le(flag_b_size ? flag_b_size - 1u : 0);
    put32le(p_at); put32le(pixel_size);
    for (i = 0; i < colours; i++) {
        put8(255u - i); put8(i); put8(7);
    }
    assert(length == header + a_at);
    memset(file + length, 0, a_size);
    if (flag_b_size) {
        file[length] = flag_b[0];
        memcpy(file + length + a_size, flag_b + 1, flag_b_size - 1u);
    }
    length += a_size + (flag_b_size ? flag_b_size - 1u : 0);
    memcpy(file + length, pixels, pixel_size);
    length += pixel_size;
}

static uint32_t mag_colour(unsigned i) { return (uint32_t)i << 16 | (255u - i) << 8 | 7u; }

static void mag_indexed(void)
{
    struct japanpc_image image;
    uint8_t pixels[64];
    unsigned x, y;

    /* 16 colours, 8x2 */
    for (x = 0; x < 8; x++)
        pixels[x] = (uint8_t)((2 * x) << 4 | (2 * x + 1));
    mag(0, 0, 0, 0, 0, 7, 1, pixels, 8, NULL, 0);
    decode_ok(&image, 8, 2);
    for (y = 0; y < 2; y++)
        for (x = 0; x < 8; x++)
            assert(rgb_at(&image, x, y) == mag_colour(y * 8 + x));
    japanpc_free(&image);
    truncations();

    /* The left edge inside a flag unit is cropped: 16 colours from x 3 */
    mag(0, 0, 0, 3, 0, 10, 0, pixels, 8, NULL, 0);
    decode_ok(&image, 8, 1);
    for (x = 0; x < 8; x++)
        assert(rgb_at(&image, x, 0) == mag_colour(x + 3));
    japanpc_free(&image);

    /* 256 colours from x 5 to 9, stored from 4 to 11 */
    for (x = 0; x < 8; x++)
        pixels[x] = (uint8_t)(100 + x);
    mag(0, 0, 0x80, 5, 0, 9, 0, pixels, 8, NULL, 0);
    decode_ok(&image, 5, 1);
    for (x = 0; x < 5; x++)
        assert(rgb_at(&image, x, 0) == mag_colour(101 + x));
    japanpc_free(&image);
}

static void mag_aspect(void)
{
    struct japanpc_image image;
    uint8_t pixels[16];
    unsigned x;

    for (x = 0; x < 16; x++)
        pixels[x] = (uint8_t)(x * 17u);
    /* The 200-line bit doubles lines */
    mag(0, 0, 1, 0, 0, 7, 1, pixels, 8, NULL, 0);
    decode_ok(&image, 8, 4);
    assert(rgb_at(&image, 2, 0) == mag_colour(1) && rgb_at(&image, 2, 1) == mag_colour(1));
    assert(rgb_at(&image, 2, 2) == mag_colour(5) && rgb_at(&image, 2, 3) == mag_colour(5));
    japanpc_free(&image);
    /* but not in 256 colours */
    mag(0, 0, 0x81, 0, 0, 3, 1, pixels, 8, NULL, 0);
    decode_ok(&image, 4, 2);
    japanpc_free(&image);
    /* The PC-8001's lines are always doubled */
    mag(0x80, 0, 0, 0, 0, 7, 0, pixels, 4, NULL, 0);
    decode_ok(&image, 8, 2);
    japanpc_free(&image);
    /* MSX: the flags decide, not the mode */
    mag(3, 0x04, 0, 0, 0, 7, 0, pixels, 4, NULL, 0);
    decode_ok(&image, 8, 2);
    japanpc_free(&image);
    mag(3, 0x00, 1, 0, 0, 7, 0, pixels, 4, NULL, 0);
    decode_ok(&image, 8, 1);
    japanpc_free(&image);
    mag(3, 0x10, 0, 0, 0, 7, 0, pixels, 4, NULL, 0);
    decode_ok(&image, 16, 1);
    assert(rgb_at(&image, 3, 0) == mag_colour(0) && rgb_at(&image, 4, 0) == mag_colour(1));
    assert(rgb_at(&image, 5, 0) == mag_colour(1));
    japanpc_free(&image);
    mag(3, 0x08, 0, 0, 0, 7, 0, pixels, 4, NULL, 0);
    assert(decode_result() == CODEC_INVALID);
}

static void mag_msx(void)
{
    struct japanpc_image image;
    uint8_t pixels[8] = { 0x1b, 0xe4, 0, 0 };

    /* Screen 6: 2 bits per pixel, 4 pixels a byte */
    mag(3, 0x60, 0, 0, 0, 7, 0, pixels, 4, NULL, 0);
    decode_ok(&image, 16, 1);
    assert(rgb_at(&image, 0, 0) == mag_colour(0) && rgb_at(&image, 1, 0) == mag_colour(1));
    assert(rgb_at(&image, 2, 0) == mag_colour(2) && rgb_at(&image, 3, 0) == mag_colour(3));
    assert(rgb_at(&image, 4, 0) == mag_colour(3) && rgb_at(&image, 7, 0) == mag_colour(0));
    japanpc_free(&image);
    mag(3, 0x64, 0, 0, 0, 7, 0, pixels, 4, NULL, 0);
    decode_ok(&image, 16, 2);
    japanpc_free(&image);

    /* YJK: Y 16, K 1 and J -1 shared by 4 pixels */
    pixels[0] = 0x81; pixels[1] = 0x80; pixels[2] = 0x87; pixels[3] = 0x87;
    mag(3, 0x44, 0x80, 0, 0, 3, 0, pixels, 4, NULL, 0);
    decode_ok(&image, 4, 1);
    assert(rgb_at(&image, 0, 0) == 0x7b8ca5u && rgb_at(&image, 3, 0) == 0x7b8ca5u);
    japanpc_free(&image);
    /* 16 colours declared, still a byte a pixel; twice as wide */
    mag(3, 0x40, 0, 0, 0, 7, 0, pixels, 4, NULL, 0);
    decode_ok(&image, 8, 1);
    japanpc_free(&image);
    /* YJK with palette: an odd Y picks palette colour Y / 2 */
    pixels[1] = 0x88;
    mag(3, 0x24, 0x80, 0, 0, 3, 0, pixels, 4, NULL, 0);
    decode_ok(&image, 4, 1);
    assert(rgb_at(&image, 1, 0) == mag_colour(8));
    assert(rgb_at(&image, 0, 0) == 0x7b8ca5u);
    japanpc_free(&image);
    mag(3, 0x34, 0x80, 0, 0, 3, 0, pixels, 4, NULL, 0);
    decode_ok(&image, 4, 1);
    japanpc_free(&image);
}

static void mag_copies(void)
{
    struct japanpc_image image;
    /* 16 colours, 8x2: line 0 stored; line 1's first unit copies from 1
       up (code 4), its second from 2 left (code 1). */
    uint8_t pixels[4] = { 0x12, 0x34, 0x56, 0x78 };
    uint8_t flags[2] = { 0x40, 0x41 };

    mag(0, 0, 0, 0, 0, 7, 1, pixels, 4, flags, 2);
    decode_ok(&image, 8, 2);
    assert(rgb_at(&image, 0, 1) == mag_colour(1) && rgb_at(&image, 3, 1) == mag_colour(4));
    assert(rgb_at(&image, 4, 1) == mag_colour(1) && rgb_at(&image, 7, 1) == mag_colour(4));
    japanpc_free(&image);
    truncations();

    /* Copying from above the first line */
    flags[0] = 0x80;
    mag(0, 0, 0, 0, 0, 7, 1, pixels, 4, flags, 2);
    assert(decode_result() == CODEC_INVALID);
    /* or left of the first unit */
    flags[1] = 0x10;
    mag(0, 0, 0, 0, 0, 7, 1, pixels, 4, flags, 2);
    assert(decode_result() == CODEC_INVALID);
}

static void mag_invalid(void)
{
    uint8_t pixels[8] = { 0 };
    size_t header = 31;

    mag(0, 0, 0, 8, 0, 7, 0, pixels, 4, NULL, 0);
    assert(decode_result() == CODEC_INVALID);
    mag(0, 0, 0, 0, 1, 7, 0, pixels, 4, NULL, 0);
    assert(decode_result() == CODEC_INVALID);
    mag(0, 0, 0, 0, 0, 7, 0, pixels, 4, NULL, 0);
    file[header] = 1;
    assert(decode_result() == CODEC_INVALID);
    /* Offsets past the end */
    mag(0, 0, 0, 0, 0, 7, 0, pixels, 4, NULL, 0);
    file[header + 15] = 0x40;
    assert(decode_result() == CODEC_TRUNCATED);
    mag(0, 0, 0, 0, 0, 7, 0, pixels, 4, NULL, 0);
    file[header + 27] = 0x40;
    assert(decode_result() == CODEC_TRUNCATED);
    /* No end of comment */
    length = 0;
    puts8("MAKI02  TEST no end");
    assert(decode_result() == CODEC_TRUNCATED);
    /* Too big to allocate */
    mag(0, 0, 0, 0, 0, 7, 0, pixels, 4, NULL, 0);
    file[header + 8] = 0xff; file[header + 9] = 0xff;
    file[header + 10] = 0xff; file[header + 11] = 0xff;
    assert(decode_result() == CODEC_TOO_LARGE);
}

/* MKI ------------------------------------------------------------------ */

static void mki(char variant)
{
    static const uint8_t screen[8] = { 0, 0, 0, 0, 2, 0x80, 1, 0x90 };
    unsigned i;
    length = 0;
    puts8("MAKI01");
    put8((unsigned char)variant);
    puts8(" PC98                    ");
    memset(file + length, 0, 8);
    length += 8;
    memcpy(file + length, screen, 8);
    length += 8;
    for (i = 0; i < 16; i++) {
        put8(i << 4); put8(0x20); put8(0x30);
    }
    memset(file + length, 0, 1000);
    length += 1000;
}

static void mki_tests(void)
{
    struct japanpc_image image;
    unsigned y;

    /* Nothing changes: all colour 0, 4-bit levels widened */
    mki('A');
    assert(length == 1096);
    decode_ok(&image, 640, 400);
    assert(rgb_at(&image, 0, 0) == 0x220033u && rgb_at(&image, 639, 399) == 0x220033u);
    japanpc_free(&image);

    /* Block 0 has a flag B word; its bit for line 0, byte 0 brings 0x12 */
    mki('A');
    file[96] = 0x80;
    put8(0x80); put8(0x00);
    put8(0x12);
    decode_ok(&image, 640, 400);
    /* MAKI01A XORs each line with the one 2 up */
    for (y = 0; y < 8; y++) {
        uint32_t left = y % 2 == 0 ? 0x221133u : 0x220033u;
        assert(rgb_at(&image, 0, y) == left);
    }
    assert(rgb_at(&image, 1, 0) == 0x222233u && rgb_at(&image, 2, 0) == 0x220033u);
    japanpc_free(&image);
    truncations();

    /* MAKI01B XORs with the line 4 up */
    mki('B');
    file[96] = 0x80;
    put8(0x80); put8(0x00);
    put8(0x12);
    decode_ok(&image, 640, 400);
    for (y = 0; y < 8; y++)
        assert(rgb_at(&image, 0, y) == (y % 4 == 0 ? 0x221133u : 0x220033u));
    japanpc_free(&image);

    /* Only the 640x400 screen */
    mki('A');
    file[45] = 0x70;
    assert(decode_result() == CODEC_INVALID);
}

/* Pi ------------------------------------------------------------------- */

/* Header for a Pi with red palette levels (i << 4) unless mode omits it. */
static void pi(unsigned mode, unsigned n, unsigned m, unsigned depth,
               const char *machine, unsigned width, unsigned height)
{
    unsigned i;
    length = 0;
    align();
    puts8("Pi comment\x1a");
    puts8("dummy");
    put8(0);
    put8(mode); put8(n); put8(m); put8(depth);
    puts8(machine);
    put16be(3);
    puts8("xyz");
    put16be(width);
    put16be(height);
    if (!(mode & 0x80u))
        for (i = 0; i < (1u << depth); i++) {
            put8(i << 4); put8(0); put8(0);
        }
}

/* The worked example in mooncore's Pi notes: 5D 29 80 26 75 gives colours
   9 9 9 1 9 9 1 8 8 8. */
static const uint8_t pi_example[] = { 0x5d, 0x29, 0x80, 0x26, 0x75 };
static const uint8_t pi_example_colours[10] = { 9, 9, 9, 1, 9, 9, 1, 8, 8, 8 };

static void pi_example_file(unsigned mode, unsigned n, unsigned m,
                            const char *machine)
{
    pi(mode, n, m, 4, machine, 10, 1);
    memcpy(file + length, pi_example, sizeof pi_example);
    length += sizeof pi_example;
}

static void pi_tests(void)
{
    struct japanpc_image image;
    unsigned x;

    pi_example_file(0, 0, 0, "PC98");
    decode_ok(&image, 10, 1);
    for (x = 0; x < 10; x++)
        assert(rgb_at(&image, x, 0) == (uint32_t)pi_example_colours[x] * 0x110000u);
    japanpc_free(&image);
    truncations();

    /* The default palette: 9 bright blue, 1 dark blue, 8 black */
    pi_example_file(0x80, 0, 0, "PC98");
    decode_ok(&image, 10, 1);
    assert(rgb_at(&image, 0, 0) == 0x0000ffu);
    assert(rgb_at(&image, 3, 0) == 0x000077u);
    assert(rgb_at(&image, 9, 0) == 0);
    japanpc_free(&image);

    /* Lines n/m times too short: 2/1 doubles lines, 1/2 columns */
    pi_example_file(0, 2, 1, "PC98");
    decode_ok(&image, 10, 2);
    japanpc_free(&image);
    pi_example_file(0, 1, 2, "PC98");
    decode_ok(&image, 20, 1);
    assert(rgb_at(&image, 6, 0) == 0x110000u && rgb_at(&image, 7, 0) == 0x110000u);
    japanpc_free(&image);
    pi_example_file(0, 0, 0, "PC88");
    decode_ok(&image, 10, 2);
    japanpc_free(&image);
    pi_example_file(0, 2, 1, "X68K");
    decode_ok(&image, 10, 2);
    japanpc_free(&image);

    /* 256 colours with the default GGGRRRBB palette: rank 29 after colour
       0 is 227, then that colour again throughout. */
    pi(0x80, 0, 0, 8, "PC98", 4, 1);
    bits("0 1110 1101  10  01 0  01 10 10");
    decode_ok(&image, 4, 1);
    for (x = 0; x < 4; x++)
        assert(rgb_at(&image, x, 0) == 0x00ffffu);
    japanpc_free(&image);

    /* A copy running past the last pixel is clipped: 7 pairs of 4 */
    pi(0, 0, 0, 4, "PC98", 4, 1);
    bits("11 11  01 110 11");
    decode_ok(&image, 4, 1);
    for (x = 0; x < 4; x++)
        assert(rgb_at(&image, x, 0) == (x % 2 ? 0xee0000u : 0xff0000u));
    japanpc_free(&image);

    /* Unsupported and malformed */
    pi(0, 0, 0, 4, "PC98", 2, 4);
    bits("11 11  01 0");
    assert(decode_result() == CODEC_INVALID);
    pi(0, 0, 0, 5, "PC98", 4, 1);
    assert(decode_result() == CODEC_INVALID);
    pi(0, 0, 0, 4, "PC98", 0, 1);
    bits("11 11  01 0");
    assert(decode_result() == CODEC_INVALID);
    pi(0, 0, 0, 4, "PC98", 65535, 65535);
    bits("11 11  01 0");
    assert(decode_result() == CODEC_TOO_LARGE);
}

/* PIC ------------------------------------------------------------------ */

static void pic(const char *comment, unsigned model, unsigned depth,
                unsigned width, unsigned height)
{
    length = 0;
    align();
    puts8("PIC");
    puts8(comment);
    put8(0x1a);
    put8(0);
    put8(0);
    put8(model);
    put16be(depth);
    put16be(width);
    put16be(height);
}

/* A run length of at least 1: n - 1 ones, a zero, then n bits of the
   length less 2^n - 1. */
static void length_code(unsigned long n)
{
    unsigned width = 1, i;
    while (n > (2ul << width) - 2u)
        width++;
    for (i = 1; i < width; i++)
        bits("1");
    bits("0");
    value_bits(n - ((1ul << width) - 1u), width);
}

/* GGGGGRRRRRBBBBB with the intensity bit clear: 5-bit levels, then the
   top 2 bits again. */
static uint32_t x68k15(unsigned v)
{
    unsigned r = v >> 5 & 31u, g = v >> 10 & 31u, b = v & 31u;
    return (uint32_t)(r << 3 | r >> 3) << 16 | (uint32_t)(g << 3 | g >> 3) << 8 |
           (b << 3 | b >> 3);
}

static void pic_x68k(void)
{
    struct japanpc_image image;
    uint32_t white = 0xfbfbfbu, red = 0xfb0000u;
    unsigned x, i;

    assert(x68k15(0x7fff) == white && x68k15(0x03e0) == red);

    /* 4x2: white with a chain straight down, 2 more white and red, then
       a run that meets the chain and takes its colour. */
    pic("", 0, 15, 4, 2);
    length_code(1); bits("0"); value_bits(0x7fff, 15);
    bits("1 10 000");
    length_code(3); bits("0"); value_bits(0x03e0, 15); bits("0");
    length_code(5);
    decode_ok(&image, 4, 2);
    for (x = 0; x < 3; x++)
        assert(rgb_at(&image, x, 0) == white);
    assert(rgb_at(&image, 3, 0) == red);
    for (x = 0; x < 4; x++)
        assert(rgb_at(&image, x, 1) == white);
    japanpc_free(&image);
    truncations();

    /* The colour cache: 128 colours fill slots 1 to 127, then 0; using
       slot 1 makes slot 2 the oldest, which the next new colour takes. */
    pic("", 0, 15, 132, 1);
    for (i = 1; i <= 128; i++) {
        length_code(1); bits("0"); value_bits(i, 15); bits("0");
    }
    length_code(1); bits("1"); value_bits(1, 7); bits("0");
    length_code(1); bits("0"); value_bits(1000, 15); bits("0");
    length_code(1); bits("1"); value_bits(2, 7); bits("0");
    length_code(1); bits("1"); value_bits(3, 7);
    decode_ok(&image, 132, 1);
    for (x = 0; x < 128; x++)
        assert(rgb_at(&image, x, 0) == x68k15(x + 1));
    assert(rgb_at(&image, 128, 0) == x68k15(1));
    assert(rgb_at(&image, 129, 0) == x68k15(1000));
    assert(rgb_at(&image, 130, 0) == x68k15(1000));
    assert(rgb_at(&image, 131, 0) == x68k15(3));
    japanpc_free(&image);

    /* 16-bit: the intensity bit */
    pic("", 0, 16, 1, 1);
    length_code(1); bits("0"); value_bits(0xffff, 16);
    decode_ok(&image, 1, 1);
    assert(rgb_at(&image, 0, 0) == 0xffffffu);
    japanpc_free(&image);

    /* 4-bit with an X68000 palette; an MSX comment means 3-bit levels */
    for (i = 0; i < 2; i++) {
        unsigned c;
        pic(i ? "/MM/XSS/" : "comment", 0, 4, 2, 1);
        for (c = 0; c < 16; c++)
            put16be(c == 1 ? 0xffffu : c == 2 ? 0x07c1u : 0);
        length_code(1); value_bits(1, 4); bits("0");
        length_code(1); value_bits(2, 4);
        decode_ok(&image, 2, 1);
        assert(rgb_at(&image, 0, 0) == 0xffffffu);
        assert(rgb_at(&image, 1, 0) == (i ? 0xff0000u : 0xff0404u));
        japanpc_free(&image);
    }
}

static void pic_models(void)
{
    struct japanpc_image image;

    /* PC-88VA HR mode, 16-bit GGGGGGRRRRRBBBBB, square */
    pic("", 0x11, 16, 2, 1);
    length_code(1); bits("0"); value_bits(0x001f, 16); bits("0");
    length_code(1); bits("0"); value_bits(0xfc00, 16);
    decode_ok(&image, 2, 1);
    assert(rgb_at(&image, 0, 0) == 0x0000ffu && rgb_at(&image, 1, 0) == 0x00ff00u);
    japanpc_free(&image);
    /* 12-bit GGGGRRRRBBBB at 320x200 or smaller: pixels 2x2 */
    pic("", 0x01, 12, 2, 1);
    length_code(1); bits("0"); value_bits(0x0f0, 12); bits("0");
    length_code(1); bits("0"); value_bits(0xf00, 12);
    decode_ok(&image, 4, 2);
    assert(rgb_at(&image, 1, 1) == 0xff0000u && rgb_at(&image, 2, 0) == 0x00ff00u);
    japanpc_free(&image);
    /* 8-bit GGGRRRBB colours, not cached */
    pic("", 0x01, 8, 2, 1);
    length_code(1); value_bits(0xe3, 8); bits("0");
    length_code(1); value_bits(0x1c, 8);
    decode_ok(&image, 4, 2);
    assert(rgb_at(&image, 0, 0) == 0x00ffffu && rgb_at(&image, 2, 0) == 0xff0000u);
    japanpc_free(&image);
    /* 256 colours dithered in 16-bit pairs: low byte first, and each line
       holds an even line then the odd one. */
    pic("", 0x21, 16, 2, 1);
    length_code(1); bits("0"); value_bits(0xe31c, 16); bits("0");
    length_code(1); bits("0"); value_bits(0x0300, 16);
    decode_ok(&image, 4, 4);
    assert(rgb_at(&image, 0, 0) == 0xff0000u && rgb_at(&image, 2, 0) == 0x00ffffu);
    assert(rgb_at(&image, 0, 2) == 0 && rgb_at(&image, 2, 2) == 0x0000ffu);
    japanpc_free(&image);

    /* FM TOWNS: mode 0 has a screen position first */
    pic("", 0x02, 15, 1, 1);
    put16be(0); put16be(0); put16be(0);
    length_code(1); bits("0"); value_bits(0x03e0, 15);
    decode_ok(&image, 1, 1);
    assert(rgb_at(&image, 0, 0) == 0xfb0000u);
    japanpc_free(&image);
    pic("", 0xc2, 15, 1, 1);
    length_code(1); bits("0"); value_bits(0x03e0, 15);
    decode_ok(&image, 1, 1);
    japanpc_free(&image);

    /* Macintosh RRRRRGGGGGBBBBB */
    pic("", 0x03, 15, 1, 1);
    length_code(1); bits("0"); value_bits(0x7c00, 15);
    decode_ok(&image, 1, 1);
    assert(rgb_at(&image, 0, 0) == 0xff0000u);
    japanpc_free(&image);

    /* Generic: position, a 2:1 pixel ratio, a 4-bit GRB palette */
    pic("", 0xff, 4, 2, 1);
    put16be(0); put16be(0); put8(2); put8(1);
    put8(4);
    value_bits(0x000, 12);
    value_bits(0xf08, 12);
    {
        unsigned i;
        for (i = 2; i < 16; i++)
            value_bits(0, 12);
    }
    align();
    length_code(1); value_bits(1, 4); bits("0");
    length_code(1); value_bits(0, 4);
    decode_ok(&image, 4, 1);
    assert(rgb_at(&image, 0, 0) == 0x00ff88u && rgb_at(&image, 1, 0) == 0x00ff88u);
    assert(rgb_at(&image, 2, 0) == 0);
    japanpc_free(&image);
    truncations();
    /* and 24-bit GGGGGGGGRRRRRRRRBBBBBBBB */
    pic("", 0x0f, 24, 1, 1);
    put16be(0); put16be(0); put16be(0x0101);
    length_code(1); bits("0"); value_bits(0x123456, 24);
    decode_ok(&image, 1, 1);
    assert(rgb_at(&image, 0, 0) == 0x341256u);
    japanpc_free(&image);
}

static void pic_invalid(void)
{
    /* A chain off the bottom */
    pic("", 0, 15, 2, 1);
    length_code(1); bits("0"); value_bits(1, 15); bits("1 10");
    assert(decode_result() == CODEC_INVALID);
    /* A length code of 20 ones */
    pic("", 0, 15, 2, 1);
    bits("1111 1111 1111 1111 1111");
    assert(decode_result() == CODEC_INVALID);
    /* Headers */
    pic("", 0, 5, 2, 1);
    assert(decode_result() == CODEC_INVALID);
    pic("", 0x10, 15, 2, 1);
    assert(decode_result() == CODEC_INVALID);
    pic("", 0x04, 15, 2, 1);
    assert(decode_result() == CODEC_INVALID);
    pic("", 0x03, 16, 2, 1);
    assert(decode_result() == CODEC_INVALID);
    pic("", 0x11, 15, 2, 1);
    assert(decode_result() == CODEC_INVALID);
    pic("", 0x0f, 4, 2, 1);
    put16be(0); put16be(0); put16be(0x0101); put8(9);
    assert(decode_result() == CODEC_INVALID);
    pic("", 0, 15, 2, 1);
    file[length - 8] = 1; /* the reserved byte */
    assert(decode_result() == CODEC_INVALID);
    pic("", 0, 15, 0, 1);
    length_code(1);
    assert(decode_result() == CODEC_INVALID);
    pic("", 0, 15, 65535, 65535);
    length_code(1);
    assert(decode_result() == CODEC_TOO_LARGE);
    /* The palette cut short */
    pic("", 0, 8, 2, 1);
    put16be(0);
    assert(decode_result() == CODEC_TRUNCATED);
}

/* Files in general ----------------------------------------------------- */

static void containers(void)
{
    struct japanpc_image image;
    size_t size;

    /* A MacBinary header in front */
    pi_example_file(0, 0, 0, "PC98");
    size = length;
    memmove(file + 128, file, size);
    memset(file, 0, 128);
    file[1] = 4;
    memcpy(file + 2, "a.pi", 4);
    file[85] = (uint8_t)(size >> 8);
    file[86] = (uint8_t)size;
    memset(file + 128 + size, 0xee, 100); /* a resource fork */
    length = 128 + size + 100;
    decode_ok(&image, 10, 1);
    assert(rgb_at(&image, 3, 0) == 0x110000u);
    japanpc_free(&image);
    /* A fork length cutting the picture short */
    file[86] = (uint8_t)(size - 1);
    assert(decode_result() == CODEC_TRUNCATED);
    file[86] = (uint8_t)size;
    /* Not MacBinary when byte 74 isn't 0 */
    file[74] = 1;
    assert(decode_result() == CODEC_INVALID);

    length = 0;
    puts8("GIF89a..");
    assert(decode_result() == CODEC_INVALID);
    length = 4;
    assert(decode_result() == CODEC_TRUNCATED);
}

/* Writer --------------------------------------------------------------- */

static void round_trip(const uint8_t *rgba, unsigned width, unsigned height,
                       unsigned mode)
{
    size_t capacity = japanpc_encode_bound(width, height), size, i;
    uint8_t *out = malloc(capacity);
    struct japanpc_image image;

    assert(out != NULL);
    assert(japanpc_encode(rgba, width, height, out, capacity, &size) == CODEC_OK);
    assert(size <= capacity && size <= sizeof file);
    assert(memcmp(out, "MAKI02  ", 8) == 0 && out[31 + 3] == mode);
    memcpy(file, out, size);
    length = size;
    decode_ok(&image, width, height);
    for (i = 0; i < (size_t)width * height * 4u; i += 4) {
        unsigned a = rgba[i + 3], c;
        for (c = 0; c < 3; c++)
            assert(image.rgba[i + c] ==
                   (rgba[i + c] * a + 255u * (255u - a) + 127u) / 255u);
    }
    japanpc_free(&image);
    free(out);
}

static void writer(void)
{
    static uint8_t rgba[61 * 29 * 4];
    unsigned x, y, i;
    size_t size;

    /* 16 colours in blocks and stripes, so most units copy */
    for (y = 0; y < 29; y++)
        for (x = 0; x < 61; x++) {
            uint8_t *p = rgba + (y * 61u + x) * 4u;
            unsigned c = (x / 5u + y / 3u) % 16u;
            p[0] = (uint8_t)(c * 16u);
            p[1] = (uint8_t)(255u - c * 3u);
            p[2] = (uint8_t)(y % 2u ? 40u : 40u);
            p[3] = 255;
        }
    round_trip(rgba, 61, 29, 0x00);
    truncations();
    round_trip(rgba, 1, 1, 0x00);
    round_trip(rgba, 7, 3, 0x00);

    /* 256 colours */
    for (i = 0; i < 61u * 29u; i++) {
        rgba[i * 4u] = (uint8_t)(i % 256u);
        rgba[i * 4u + 1] = (uint8_t)(i % 256u / 2u);
    }
    round_trip(rgba, 61, 29, 0x80);

    /* Transparency over white */
    for (i = 0; i < 61u * 29u; i++) {
        rgba[i * 4u] = (uint8_t)(i % 3u * 100u);
        rgba[i * 4u + 1] = 10;
        rgba[i * 4u + 2] = 20;
        rgba[i * 4u + 3] = (uint8_t)(i % 3u == 2 ? 128 : i % 3u == 1 ? 0 : 255);
    }
    round_trip(rgba, 61, 29, 0x00);

    /* 257 colours won't fit */
    for (i = 0; i < 61u * 29u; i++) {
        rgba[i * 4u] = (uint8_t)(i % 257u);
        rgba[i * 4u + 1] = (uint8_t)(i % 257u == 256u);
        rgba[i * 4u + 3] = 255;
    }
    assert(japanpc_encode(rgba, 61, 29, file, sizeof file, &size) == CODEC_INVALID);
    assert(japanpc_encode(rgba, 61, 29, file, 10, &size) == CODEC_NO_MEMORY);
    assert(japanpc_encode_bound(0, 1) == 0);
    assert(japanpc_encode_bound(65536, 1) == 0);
    assert(japanpc_encode_bound(4096, 4097) == 0);
}

int main(void)
{
    mag_indexed();
    mag_aspect();
    mag_msx();
    mag_copies();
    mag_invalid();
    mki_tests();
    pi_tests();
    pic_x68k();
    pic_models();
    pic_invalid();
    containers();
    writer();
    puts("japanpc: all tests passed");
    return 0;
}
