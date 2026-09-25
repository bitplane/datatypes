#include "../formats/xcursor/decode.h"
#include "../formats/xcursor/encode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMAGE_TYPE 0xfffd0002u
#define COMMENT_TYPE 0xfffe0001u

static uint8_t file[1 << 20];
static size_t file_length;
static size_t toc_at, ntoc;

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* A file header of header_size bytes with room for tocs table entries. */
static void begin(uint32_t header_size, size_t tocs)
{
    memset(file, 0xee, sizeof file);
    memcpy(file, "Xcur", 4);
    put32(file + 4, header_size);
    put32(file + 8, 0x10000u);
    put32(file + 12, 0);
    toc_at = header_size;
    ntoc = 0;
    file_length = header_size + tocs * 12u;
}

static void add_toc(uint32_t type, uint32_t subtype, uint32_t position)
{
    uint8_t *toc = file + toc_at + ntoc * 12u;
    put32(toc, type);
    put32(toc + 4, subtype);
    put32(toc + 8, position);
    ntoc++;
    put32(file + 12, (uint32_t)ntoc);
}

/* An image chunk with each pixel given as premultiplied 0xAARRGGBB. */
static size_t add_image_header(uint32_t nominal, uint32_t header, uint32_t w,
                               uint32_t h, const uint32_t *argb)
{
    size_t pos = file_length, i;
    add_toc(IMAGE_TYPE, nominal, (uint32_t)pos);
    put32(file + pos, header);
    put32(file + pos + 4, IMAGE_TYPE);
    put32(file + pos + 8, nominal);
    put32(file + pos + 12, 1);
    put32(file + pos + 16, w);
    put32(file + pos + 20, h);
    put32(file + pos + 24, 0);
    put32(file + pos + 28, 0);
    put32(file + pos + 32, 50);
    file_length = pos + header;
    if (argb != NULL)
        for (i = 0; i < (size_t)w * h; i++, file_length += 4)
            put32(file + file_length, argb[i]);
    return pos;
}

static size_t add_image(uint32_t nominal, uint32_t w, uint32_t h, const uint32_t *argb)
{
    return add_image_header(nominal, 36, w, h, argb);
}

static void add_comment(const char *text)
{
    size_t pos = file_length, length = strlen(text);
    add_toc(COMMENT_TYPE, 1, (uint32_t)pos);
    put32(file + pos, 20);
    put32(file + pos + 4, COMMENT_TYPE);
    put32(file + pos + 8, 1);
    put32(file + pos + 12, 1);
    put32(file + pos + 16, (uint32_t)length);
    memcpy(file + pos + 20, text, length);
    file_length = pos + 20 + length;
}

static enum codec_result load(long index, struct xcursor_image *image, unsigned *count)
{
    return xcursor_decode(file, file_length, index, image, count);
}

static void expect_pixel(const struct xcursor_image *image, unsigned x, unsigned y,
                         unsigned r, unsigned g, unsigned b, unsigned a)
{
    const uint8_t *p = image->rgba + ((size_t)y * image->width + x) * 4u;
    if (p[0] != r || p[1] != g || p[2] != b || p[3] != a) {
        fprintf(stderr, "pixel %u,%u is %u %u %u %u, expected %u %u %u %u\n",
                x, y, p[0], p[1], p[2], p[3], r, g, b, a);
        assert(0);
    }
}

static void expect(enum codec_result want, long index)
{
    struct xcursor_image image;
    unsigned count;
    enum codec_result got = load(index, &image, &count);
    if (got != want) {
        fprintf(stderr, "result %d, expected %d\n", got, want);
        assert(0);
    }
    if (got == CODEC_OK)
        xcursor_free(&image);
    else
        assert(image.rgba == NULL);
}

static void test_single(void)
{
    static const uint32_t px[6] = {
        0xffff0000u, 0xff00ff00u, 0xff0000ffu,
        0x80800000u, 0x00000000u, 0x40102030u
    };
    struct xcursor_image image;
    unsigned count;

    begin(16, 1);
    add_image(3, 3, 2, px);
    assert(load(XCURSOR_BEST, &image, &count) == CODEC_OK);
    assert(count == 1 && image.width == 3 && image.height == 2);
    expect_pixel(&image, 0, 0, 255, 0, 0, 255);
    expect_pixel(&image, 1, 0, 0, 255, 0, 255);
    expect_pixel(&image, 2, 0, 0, 0, 255, 255);
    /* Premultiplied 0x80 red at alpha 0x80 is full red. */
    expect_pixel(&image, 0, 1, 255, 0, 0, 128);
    expect_pixel(&image, 1, 1, 0, 0, 0, 0);
    /* 0x10 * 255 / 0x40 = 63, 0x20 -> 127, 0x30 -> 191, truncating. */
    expect_pixel(&image, 2, 1, 63, 127, 191, 64);
    xcursor_free(&image);
    assert(load(0, &image, &count) == CODEC_OK);
    xcursor_free(&image);
    expect(CODEC_INVALID, 1);
    expect(CODEC_INVALID, -2);
}

static void test_bad_premultiply(void)
{
    /* Colour above alpha is clamped; colour with zero alpha is dropped. */
    static const uint32_t px[2] = {0x10ff8008u, 0x00ffffffu};
    struct xcursor_image image;
    unsigned count;

    begin(16, 1);
    add_image(2, 2, 1, px);
    assert(load(XCURSOR_BEST, &image, &count) == CODEC_OK);
    expect_pixel(&image, 0, 0, 255, 255, 127, 16);
    expect_pixel(&image, 1, 0, 0, 0, 0, 0);
    xcursor_free(&image);
}

static void test_transparent_kept(void)
{
    static const uint32_t px[4] = {0, 0, 0, 0};
    struct xcursor_image image;
    unsigned count;

    begin(16, 1);
    add_image(2, 2, 2, px);
    assert(load(XCURSOR_BEST, &image, &count) == CODEC_OK);
    expect_pixel(&image, 1, 1, 0, 0, 0, 0);
    xcursor_free(&image);
}

/* Sizes 2, 4 (two frames) and 3, with comments around them. */
static void make_multi(void)
{
    static const uint32_t a[4] = {0xff000001u, 0xff000001u, 0xff000001u, 0xff000001u};
    uint32_t b[16], c[16], d[9];
    size_t i;
    for (i = 0; i < 16; i++) {
        b[i] = 0xff000002u;
        c[i] = 0xff000003u;
    }
    for (i = 0; i < 9; i++)
        d[i] = 0xff000004u;
    begin(16, 6);
    add_comment("copyright");
    add_image(2, 2, 2, a);
    add_image(4, 4, 4, b);
    add_image(4, 4, 4, c);
    add_comment("license");
    add_image(3, 3, 3, d);
}

static void test_multi(void)
{
    static const unsigned sizes[4] = {2, 4, 4, 3}, blue[4] = {1, 2, 3, 4};
    struct xcursor_image image;
    unsigned count, i;

    make_multi();
    assert(load(XCURSOR_BEST, &image, &count) == CODEC_OK);
    assert(count == 4 && image.width == 4 && image.height == 4);
    /* The first of the two largest frames. */
    expect_pixel(&image, 3, 3, 0, 0, 2, 255);
    xcursor_free(&image);
    for (i = 0; i < 4; i++) {
        assert(load((long)i, &image, &count) == CODEC_OK);
        assert(count == 4 && image.width == sizes[i] && image.height == sizes[i]);
        expect_pixel(&image, 0, 0, 0, 0, blue[i], 255);
        xcursor_free(&image);
    }
    assert(load(4, &image, &count) == CODEC_INVALID);
    assert(count == 4 && image.rgba == NULL);
}

static void test_non_square_best(void)
{
    uint32_t px[40];
    struct xcursor_image image;
    unsigned count;
    size_t i;
    for (i = 0; i < 40; i++)
        px[i] = 0xff000000u | (uint32_t)(i < 16 ? 1 : 2);
    begin(16, 2);
    /* 4x4 is 16 pixels; 8x3 is 24, so it wins despite the smaller height. */
    add_image(4, 4, 4, px);
    add_image(8, 8, 3, px + 16);
    assert(load(XCURSOR_BEST, &image, &count) == CODEC_OK);
    assert(image.width == 8 && image.height == 3);
    xcursor_free(&image);
}

static void test_optional_structure(void)
{
    static const uint32_t px[1] = {0xff123456u};
    struct xcursor_image image;
    unsigned count;

    /* A longer file header is skipped. */
    begin(24, 1);
    add_image(1, 1, 1, px);
    assert(load(XCURSOR_BEST, &image, &count) == CODEC_OK);
    expect_pixel(&image, 0, 0, 0x12, 0x34, 0x56, 255);
    xcursor_free(&image);

    /* So is a longer image chunk header. */
    begin(16, 1);
    add_image_header(1, 40, 1, 1, NULL);
    put32(file + file_length, 0xff654321u);
    file_length += 4;
    assert(load(XCURSOR_BEST, &image, &count) == CODEC_OK);
    expect_pixel(&image, 0, 0, 0x65, 0x43, 0x21, 255);
    xcursor_free(&image);

    /* A hotspot outside the image is ignored, as is trailing data. */
    begin(16, 1);
    add_image(1, 1, 1, px);
    put32(file + 16 + 12 + 24, 99);
    file_length += 10;
    expect(CODEC_OK, XCURSOR_BEST);

    /* Unknown chunk types are skipped, even if they point nowhere. */
    begin(16, 2);
    add_toc(0x12345678u, 0, 0xffffffffu);
    add_image(1, 1, 1, px);
    assert(load(XCURSOR_BEST, &image, &count) == CODEC_OK);
    assert(count == 1);
    xcursor_free(&image);

    /* Two table entries may share a chunk. */
    begin(16, 2);
    add_image(1, 1, 1, px);
    add_toc(IMAGE_TYPE, 1, 16 + 24);
    assert(load(1, &image, &count) == CODEC_OK);
    assert(count == 2);
    xcursor_free(&image);
}

static void test_truncated(void)
{
    static const uint32_t px[4] = {0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu};
    size_t full, cut;

    begin(16, 1);
    add_image(2, 2, 2, px);
    full = file_length;
    for (cut = 0; cut < full; cut++) {
        file_length = cut;
        expect(CODEC_TRUNCATED, XCURSOR_BEST);
    }
    file_length = full;
    expect(CODEC_OK, XCURSOR_BEST);
    assert(xcursor_decode(NULL, 0, XCURSOR_BEST, &(struct xcursor_image){0, 0, NULL},
                          &(unsigned){0}) == CODEC_TRUNCATED);

    /* A truncated second image fails the file, even when the first is chosen. */
    begin(16, 2);
    add_image(2, 2, 2, px);
    add_image(2, 2, 2, px);
    file_length -= 1;
    expect(CODEC_TRUNCATED, 0);

    /* A table entry pointing past the end. */
    begin(16, 1);
    add_image(2, 2, 2, px);
    put32(file + 16 + 8, 0xfffffff0u);
    expect(CODEC_TRUNCATED, XCURSOR_BEST);

    /* A header size past the end, and a table longer than the file. */
    begin(16, 1);
    add_image(2, 2, 2, px);
    put32(file + 4, 0xfffffff0u);
    expect(CODEC_TRUNCATED, XCURSOR_BEST);
    begin(16, 1);
    add_image(2, 2, 2, px);
    put32(file + 12, 0x10000u);
    expect(CODEC_TRUNCATED, XCURSOR_BEST);

    /* A chunk header size that runs past the end. */
    begin(16, 1);
    add_image(2, 2, 2, px);
    put32(file + 28, 0xffffff00u);
    expect(CODEC_TRUNCATED, XCURSOR_BEST);
}

static void test_invalid(void)
{
    static const uint32_t px[4] = {0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu};
    struct xcursor_image image;
    unsigned count = 99;

    begin(16, 1);
    add_image(2, 2, 2, px);
    file[0] = 'x';
    expect(CODEC_INVALID, XCURSOR_BEST);
    file[0] = 'X';
    memcpy(file, "Xcu", 3);
    file_length = 3;
    expect(CODEC_TRUNCATED, XCURSOR_BEST);

#define BROKEN(offset, value) do { \
        begin(16, 1); add_image(2, 2, 2, px); \
        put32(file + (offset), (value)); \
        expect(CODEC_INVALID, XCURSOR_BEST); \
    } while (0)
    BROKEN(4, 15);              /* file header too short */
    BROKEN(12, 0x10001u);       /* too many table entries */
    BROKEN(28, 35);             /* image header too short */
    BROKEN(28 + 4, COMMENT_TYPE); /* chunk type disagrees with the table */
    BROKEN(28 + 8, 3);          /* nominal size disagrees with the table */
    BROKEN(28 + 16, 0);         /* zero width */
    BROKEN(28 + 20, 0);         /* zero height */
    BROKEN(28 + 16, 0x8000u);   /* wider than libXcursor allows */
    BROKEN(28 + 20, 0xffffffffu);
#undef BROKEN

    /* No images at all: an empty table, or comments only. */
    begin(16, 0);
    expect(CODEC_INVALID, XCURSOR_BEST);
    begin(16, 1);
    add_comment("nothing");
    assert(load(XCURSOR_BEST, &image, &count) == CODEC_INVALID);
    assert(count == 0);

    assert(xcursor_decode(file, file_length, 0, NULL, &count) == CODEC_INVALID);
    assert(xcursor_decode(file, file_length, 0, &image, NULL) == CODEC_INVALID);
}

static void test_too_large(void)
{
    /* 4097 x 4097 is over 16M pixels but inside libXcursor's side limit. */
    size_t pixels = (size_t)4097 * 4097, length = 64 + pixels * 4;
    uint8_t *big = calloc(1, length);
    struct xcursor_image image;
    unsigned count;
    assert(big != NULL);
    assert(xcursor_make_header(4097, 4097, big));
    assert(xcursor_decode(big, length, XCURSOR_BEST, &image, &count) == CODEC_TOO_LARGE);
    assert(count == 1 && image.rgba == NULL);
    free(big);
}

static void test_encode(void)
{
    static const uint8_t rgba[3 * 2 * 4] = {
        255, 0, 0, 255,   0, 255, 0, 255,   0, 0, 255, 255,
        255, 255, 255, 0, 200, 100, 50, 128, 10, 20, 30, 255
    };
    uint8_t out[64 + sizeof rgba];
    struct xcursor_image image;
    unsigned count, y;

    assert(!xcursor_make_header(0, 1, out));
    assert(!xcursor_make_header(1, 0, out));
    assert(!xcursor_make_header(0x8000u, 1, out));
    assert(!xcursor_make_header(1, 0x8000u, out));
    assert(xcursor_make_header(0x7fffu, 1, out));
    assert(xcursor_make_header(3, 2, out));
    assert(memcmp(out, "Xcur", 4) == 0);
    /* Nominal size is the larger side, in the table and the chunk. */
    assert(out[20] == 3 && out[28 + 8] == 3);
    for (y = 0; y < 2; y++)
        xcursor_encode_row(rgba + y * 12, 3, out + 64 + y * 12);
    /* 200 * 128 / 255 rounds to 100; 100 -> 50; 50 -> 25. */
    assert(out[64 + 16] == 25 && out[64 + 17] == 50 && out[64 + 18] == 100 &&
           out[64 + 19] == 128);
    assert(xcursor_decode(out, sizeof out, XCURSOR_BEST, &image, &count) == CODEC_OK);
    assert(count == 1 && image.width == 3 && image.height == 2);
    expect_pixel(&image, 0, 0, 255, 0, 0, 255);
    expect_pixel(&image, 1, 0, 0, 255, 0, 255);
    expect_pixel(&image, 2, 0, 0, 0, 255, 255);
    /* Transparent colour is lost to premultiplication. */
    expect_pixel(&image, 0, 1, 0, 0, 0, 0);
    expect_pixel(&image, 1, 1, 199, 99, 49, 128);
    expect_pixel(&image, 2, 1, 10, 20, 30, 255);
    xcursor_free(&image);
}

static void test_opaque_round_trip(void)
{
    uint8_t rgba[16 * 4], out[64 + sizeof rgba];
    struct xcursor_image image;
    unsigned count, i;
    for (i = 0; i < sizeof rgba; i++)
        rgba[i] = (uint8_t)((i & 3) == 3 ? 255 : i * 37);
    assert(xcursor_make_header(4, 4, out));
    for (i = 0; i < 4; i++)
        xcursor_encode_row(rgba + i * 16, 4, out + 64 + i * 16);
    assert(xcursor_decode(out, sizeof out, XCURSOR_BEST, &image, &count) == CODEC_OK);
    assert(memcmp(image.rgba, rgba, sizeof rgba) == 0);
    xcursor_free(&image);
}

int main(void)
{
    test_single();
    test_bad_premultiply();
    test_transparent_kept();
    test_multi();
    test_non_square_best();
    test_optional_structure();
    test_truncated();
    test_invalid();
    test_too_large();
    test_encode();
    test_opaque_round_trip();
    puts("xcursor: ok");
    return 0;
}
