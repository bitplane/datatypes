#include "../formats/sunicon/decode.h"
#include "../formats/sunicon/encode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static enum codec_result decode(const char *text, struct sunicon_image *image)
{
    return sunicon_decode((const uint8_t *)text, strlen(text), image);
}

/* rows: '#' for 1 and '.' for 0 at depth 1. */
static void expect(const char *text, const char *rows, unsigned width, unsigned height)
{
    struct sunicon_image image;
    size_t i;
    assert(decode(text, &image) == CODEC_OK);
    assert(image.width == width && image.height == height && image.depth == 1);
    for (i = 0; i < (size_t)width * height; i++)
        assert(image.pixels[i] == (rows[i] == '#'));
    sunicon_free(&image);
}

static void expect_grey(const char *text, const uint8_t *values, unsigned width, unsigned height)
{
    struct sunicon_image image;
    assert(decode(text, &image) == CODEC_OK);
    assert(image.width == width && image.height == height && image.depth == 8);
    assert(memcmp(image.pixels, values, (size_t)width * height) == 0);
    sunicon_free(&image);
}

static void fails(const char *text, enum codec_result result)
{
    struct sunicon_image image;
    assert(decode(text, &image) == result);
    assert(image.pixels == NULL && image.width == 0 && image.depth == 0);
}

/* Every prefix fails, except those cut inside the last item's digits,
   which still read as a (smaller) item. */
static void prefixes_fail(const char *text)
{
    struct sunicon_image image;
    size_t length = strlen(text), n;
    const char *last = strstr(text, "0x"), *next;

    while ((next = strstr(last + 2, "0x")) != NULL)
        last = next;
    for (n = 0; n < length; n++) {
        enum codec_result result = sunicon_decode((const uint8_t *)text, n, &image);
        if (n >= (size_t)(last - text) + 3) {
            if (result == CODEC_OK)
                sunicon_free(&image);
            continue;
        }
        assert(result == CODEC_TRUNCATED || result == CODEC_INVALID);
        assert(image.pixels == NULL);
        /* Once the header is complete, a cut is a truncation. */
        if (n > (size_t)(strstr(text, "*/") - text) + 1)
            assert(result == CODEC_TRUNCATED);
    }
}

/* Encode rgba, decode the text again, and check the set bits match dark,
   with white padding up to a multiple of 16 columns. */
static size_t round_trip(const uint8_t *rgba, const uint8_t *dark, unsigned width,
                         unsigned height, char *text, size_t capacity)
{
    struct sunicon_encoder encoder;
    struct sunicon_image image;
    size_t pos, size, row = sunicon_row_capacity(width);
    unsigned x, y, padded = (width + 15u) & ~15u;

    pos = sunicon_make_header(width, height, text, capacity);
    assert(pos != 0);
    sunicon_encoder_init(&encoder, width, height);
    for (y = 0; y < height; y++) {
        assert(capacity - pos >= row);
        size = sunicon_encode_row(&encoder, rgba + (size_t)y * width * 4u, width,
                                  text + pos, row);
        assert(size != SIZE_MAX && size <= row);
        pos += size;
    }
    assert(encoder.remaining == 0);
    assert(text[pos - 1] == '\n' && text[pos - 2] != ',');
    assert(sunicon_decode((const uint8_t *)text, pos, &image) == CODEC_OK);
    assert(image.width == padded && image.height == height && image.depth == 1);
    for (y = 0; y < height; y++)
        for (x = 0; x < padded; x++)
            assert(image.pixels[(size_t)y * padded + x] ==
                   (x < width ? dark[(size_t)y * width + x] : 0));
    sunicon_free(&image);
    return pos;
}

int main(void)
{
    static const char basic[] =
        "/* Format_version=1, Width=16, Height=2, Depth=1, Valid_bits_per_item=16\n"
        " */\n"
        "\t0x8001,0x7FFE\n";
    static const char basic_rows[] = "#..............#" ".##############.";
    struct sunicon_image image;
    char *text;
    uint8_t *rgba;
    size_t i, size;

    expect(basic, basic_rows, 16, 2);
    prefixes_fail(basic);

    /* Byte and 32-bit items hold the same bits, most significant first. */
    expect("/* Format_version=1, Width=16, Height=2, Depth=1, Valid_bits_per_item=8\n */\n"
           "\t0x80,0x01,\n\t0x7F,0xFE,\n", basic_rows, 16, 2);
    expect("/* Format_version=1, Width=32, Height=1, Depth=1, Valid_bits_per_item=32\n */\n"
           "\t0x80017FFE\n", "#..............#.##############.", 32, 1);

    /* Rows are padded to whole items: 20 pixels take two 16-bit items, and
       the padding bits are ignored. */
    expect("/* Format_version=1, Width=20, Height=2, Depth=1, Valid_bits_per_item=16\n */\n"
           "0x8000,0x1FFF,0xFFFF,0xEFFF\n",
           "#..................#" "###################.", 20, 2);
    /* 3 pixels in a 32-bit item per row; 9 pixels in two byte items. */
    expect("/* Format_version=1, Width=3, Height=2, Depth=1, Valid_bits_per_item=32 */"
           "0xA0000000, 0x5FFFFFFF", "#.#" ".#.", 3, 2);
    expect("/* Format_version=1, Width=9, Height=1, Depth=1, Valid_bits_per_item=8 */"
           "0xFF 0x80", "#########", 9, 1);

    /* Depth 8: one byte per pixel, rows padded to whole items. */
    {
        static const uint8_t grey3[] = { 0x01, 0x02, 0x03, 0xf0, 0xf1, 0xf2 };
        static const uint8_t grey4[] = { 0x00, 0x7f, 0x80, 0xff };
        expect_grey("/* Format_version=1, Width=3, Height=2, Depth=8, Valid_bits_per_item=16\n */\n"
                    "\t0x0102,0x03EE,0xF0F1,0xF2EE\n", grey3, 3, 2);
        expect_grey("/* Format_version=1, Width=3, Height=2, Depth=8, Valid_bits_per_item=32 */\n"
                    "0x010203EE,0xF0F1F2EE\n", grey3, 3, 2);
        expect_grey("/* Format_version=1, Width=2, Height=2, Depth=8, Valid_bits_per_item=8 */\n"
                    "0x00,0x7f,0x80,0xff\n", grey4, 2, 2);
    }

    /* Header forms seen in real files: other comments or SCCS text first,
       fields in any order, more comment text after the fields, and items
       separated by spaces, commas or both. */
    expect("/*\t@(#)x.cursor 85/08/19 1.1 SMI\t*/\n\n"
           "/* Format_version=1, Width=16, Height=2, Depth=1, Valid_bits_per_item=16\n */\n"
           "\t0x8001, 0x7FFE,\n", basic_rows, 16, 2);
    expect("h58322\ns 00066/00000/00000\nI 1\n"
           "/* Format_version=1, Width=16, Height=2, Depth=1, Valid_bits_per_item=16\n */\n"
           "\t0x8001,0x7FFE,\nE 1\n", basic_rows, 16, 2);
    expect("/* Valid_bits_per_item=16, Depth=1, Height=2,\n * Width = 16, Format_version=1\n"
           " *\tCopyright 1988, Width=99 Height=99 (first wins)\n */"
           "0x8001\n\n  0x7ffe", basic_rows, 16, 2);
    expect("/* Format_version=1, Width=16, Height=2, Depth=1, Valid_bits_per_item=16 */\n"
           "0x8001 /* row 1 */ ,, 0X7FFE", basic_rows, 16, 2);
    /* Short items and leading zeros. */
    expect("/* Format_version=1, Width=16, Height=2 */ 0x8001, 0x7ffe", basic_rows, 16, 2);
    expect("/* Format_version=1, Width=16, Height=1 */ 0x1", "...............#", 16, 1);
    expect("/* Format_version=1, Width=16, Height=1 */ 0x00000001", "...............#", 16, 1);

    /* XView's defaults: 64 by 64, Depth=1, 16-bit items. A field without a
       number is treated as absent. */
    size = 64 + 256 * 7;
    text = malloc(size + 1);
    assert(text != NULL);
    strcpy(text, "/* Format_version=1, Width=wide, MyDepth=8 */\n");
    for (i = 0; i < 256; i++)
        strcat(text, "0xFFFF,");
    assert(decode(text, &image) == CODEC_OK);
    assert(image.width == 64 && image.height == 64 && image.depth == 1);
    for (i = 0; i < 64u * 64u; i++)
        assert(image.pixels[i] == 1);
    sunicon_free(&image);
    text[strlen(text) - 8] = '\0';
    fails(text, CODEC_TRUNCATED);
    free(text);

    /* Not an icon, or a variant no reader knows. */
    fails("", CODEC_INVALID);
    fails("#define x_width 16\n", CODEC_INVALID);
    fails("/* XPM */\nstatic char *x[] = {};", CODEC_INVALID);
    fails("/* Width=16, Height=1 */ 0x0000", CODEC_INVALID);
    fails("/* Format_version=2, Width=16, Height=1 */ 0x0000", CODEC_INVALID);
    fails("/* Format_version=1, Width=16, Height=1, Depth=4 */ 0x0000", CODEC_INVALID);
    fails("/* Format_version=1, Width=16, Height=1, Depth=24 */ 0x0000", CODEC_INVALID);
    fails("/* Format_version=1, Width=16, Height=1, Valid_bits_per_item=12 */ 0x0000", CODEC_INVALID);
    fails("/* Format_version=1, Width=16, Height=1, Valid_bits_per_item=64 */ 0x0000", CODEC_INVALID);
    fails("/* Format_version=1, Width=0, Height=1 */ 0x0000", CODEC_INVALID);
    fails("/* Format_version=1, Width=16, Height=0 */ 0x0000", CODEC_INVALID);
    fails("/* Format_version=1, Width=65536, Height=1 */ 0x0000", CODEC_TOO_LARGE);
    fails("/* Format_version=1, Width=16, Height=99999999999999999999 */ 0x0000", CODEC_TOO_LARGE);
    fails("/* Format_version=1, Width=4097, Height=4096 */ 0x0000", CODEC_TOO_LARGE);

    /* Malformed items. */
    fails("/* Format_version=1, Width=16, Height=1 */ 0x10000", CODEC_INVALID);
    fails("/* Format_version=1, Width=16, Height=1 */ 0x0001FFFF", CODEC_INVALID);
    fails("/* Format_version=1, Width=16, Height=1, Valid_bits_per_item=8 */ 0x100, 0x00", CODEC_INVALID);
    fails("/* Format_version=1, Width=32, Height=1, Valid_bits_per_item=32 */ 0x100000000", CODEC_INVALID);
    fails("/* Format_version=1, Width=16, Height=1 */ 0x12G4", CODEC_INVALID);
    fails("/* Format_version=1, Width=16, Height=1 */ 0x, 0x0000", CODEC_INVALID);
    fails("/* Format_version=1, Width=16, Height=1 */ 1234", CODEC_INVALID);
    fails("/* Format_version=1, Width=16, Height=1 */ -0x1", CODEC_INVALID);
    fails("/* Format_version=1, Width=16, Height=2 */ 0x0000; 0x0000", CODEC_INVALID);

    /* Truncation inside the header, between items and inside comments. */
    fails("/* Format_version=1, Width=16, Height=1", CODEC_TRUNCATED);
    fails("/* Format_version=1, Width=16, Height=2 */ 0x0000,", CODEC_TRUNCATED);
    fails("/* Format_version=1, Width=16, Height=2 */ 0x0000, 0", CODEC_TRUNCATED);
    fails("/* Format_version=1, Width=16, Height=2 */ 0x0000, 0x", CODEC_TRUNCATED);
    fails("/* Format_version=1, Width=16, Height=2 */ 0x0000 /* 0x0000 ", CODEC_TRUNCATED);
    fails("/* unclosed", CODEC_INVALID);
    assert(sunicon_decode(NULL, 0, &image) == CODEC_TRUNCATED);
    assert(sunicon_decode((const uint8_t *)basic, sizeof basic, NULL) == CODEC_INVALID);

    /* Header writer. */
    {
        char header[SUNICON_HEADER_MAX];
        size = sunicon_make_header(64, 64, header, sizeof header);
        assert(size == strlen(header));
        assert(strcmp(header, "/* Format_version=1, Width=64, Height=64, Depth=1, "
                              "Valid_bits_per_item=16\n */\n") == 0);
        assert(sunicon_make_header(65520, 65535, header, sizeof header) != 0);
        assert(sunicon_make_header(65521, 1, header, sizeof header) == 0);
        size = sunicon_make_header(17, 3, header, sizeof header);
        assert(size != 0 && strstr(header, " Width=32,") != NULL);
        assert(sunicon_make_header(0, 1, header, sizeof header) == 0);
        assert(sunicon_make_header(1, 65536, header, sizeof header) == 0);
        assert(sunicon_make_header(64, 64, header, 20) == 0);
    }

    /* Writer round trips: widths inside, at and past an item boundary, dark
       and light colours, and transparency composited over white. */
    {
        static const uint8_t colours[6][5] = {
            { 0, 0, 0, 255, 1 },         /* black */
            { 255, 255, 255, 255, 0 },   /* white */
            { 0, 0, 0, 0, 0 },           /* clear black shows white */
            { 0, 0, 0, 200, 1 },         /* mostly opaque black */
            { 120, 20, 20, 255, 1 },     /* dark red */
            { 200, 200, 120, 255, 0 },   /* light yellow */
        };
        static const unsigned widths[] = { 1, 15, 16, 17, 20, 64, 199 };
        size_t w;
        for (w = 0; w < sizeof widths / sizeof widths[0]; w++) {
            unsigned width = widths[w], height = 5;
            size_t capacity = SUNICON_HEADER_MAX + sunicon_row_capacity(width) * height;
            uint8_t *dark = malloc((size_t)width * height);
            rgba = malloc((size_t)width * height * 4u);
            text = malloc(capacity + 1);
            assert(rgba != NULL && dark != NULL && text != NULL);
            for (i = 0; i < (size_t)width * height; i++) {
                const uint8_t *c = colours[(i * 7u + i / width) % 6u];
                memcpy(rgba + i * 4u, c, 4);
                dark[i] = c[4];
            }
            text[round_trip(rgba, dark, width, height, text, capacity)] = '\0';
            prefixes_fail(text);
            free(rgba);
            free(dark);
            free(text);
        }
    }

    /* The encoder refuses short buffers and extra rows. */
    {
        struct sunicon_encoder encoder;
        uint8_t black[16 * 4];
        char out[64];
        memset(black, 0, sizeof black);
        for (i = 0; i < 16; i++)
            black[i * 4 + 3] = 255;
        sunicon_encoder_init(&encoder, 16, 1);
        assert(sunicon_encode_row(&encoder, black, 16, out, sunicon_row_capacity(16) - 1) == SIZE_MAX);
        size = sunicon_encode_row(&encoder, black, 16, out, sizeof out);
        assert(size == 8 && memcmp(out, "\t0xFFFF\n", 8) == 0);
        assert(sunicon_encode_row(&encoder, black, 16, out, sizeof out) == SIZE_MAX);
    }

    puts("sunicon: all tests passed");
    return 0;
}
