#include "../formats/icns/decode.h"
#include "../formats/icns/encode.h"
#include "../common/png.h"
#include "../common/zlib.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static uint8_t file[1 << 20];
static size_t file_length;

static void put32(uint8_t *p, size_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void begin(void)
{
    memcpy(file, "icns", 4);
    file_length = 8;
    put32(file + 4, file_length);
}

static void add(const char *type, const void *data, size_t length)
{
    memcpy(file + file_length, type, 4);
    put32(file + file_length + 4, 8u + length);
    memcpy(file + file_length + 8, data, length);
    file_length += 8u + length;
    put32(file + 4, file_length);
}

static enum codec_result load(long index, struct icns_image *image, unsigned *count)
{
    return icns_decode(file, file_length, index, image, count);
}

static void expect_pixel(const struct icns_image *image, unsigned x, unsigned y,
                         unsigned r, unsigned g, unsigned b, unsigned a)
{
    const uint8_t *p = image->rgba + ((size_t)y * image->width + x) * 4u;
    if (p[0] != r || p[1] != g || p[2] != b || p[3] != a) {
        fprintf(stderr, "pixel %u,%u is %u %u %u %u, expected %u %u %u %u\n",
                x, y, p[0], p[1], p[2], p[3], r, g, b, a);
        assert(0);
    }
}

/* A PNG from filtered scanlines, with optional extra chunks before IDAT. */
static size_t make_png(uint8_t *out, unsigned w, unsigned h, unsigned depth,
                       unsigned type, unsigned interlace, const uint8_t *raw,
                       size_t raw_length, const uint8_t *extra, size_t extra_length)
{
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    uLongf z = compressBound((uLong)raw_length);
    size_t pos = 8;
    memcpy(out, sig, 8);
    put32(out + pos, 13);
    memcpy(out + pos + 4, "IHDR", 4);
    put32(out + pos + 8, w);
    put32(out + pos + 12, h);
    out[pos + 16] = (uint8_t)depth;
    out[pos + 17] = (uint8_t)type;
    out[pos + 18] = out[pos + 19] = 0;
    out[pos + 20] = (uint8_t)interlace;
    put32(out + pos + 21, crc32(0, out + pos + 4, 17));
    pos += 25;
    if (extra_length > 0)
        memcpy(out + pos, extra, extra_length);
    pos += extra_length;
    assert(compress2(out + pos + 8, &z, raw, (uLong)raw_length, 9) == Z_OK);
    put32(out + pos, z);
    memcpy(out + pos + 4, "IDAT", 4);
    put32(out + pos + 8 + z, crc32(0, out + pos + 4, (uInt)z + 4));
    pos += 12 + z;
    put32(out + pos, 0);
    memcpy(out + pos + 4, "IEND", 4);
    put32(out + pos + 8, crc32(0, out + pos + 4, 4));
    return pos + 12;
}

static size_t chunk(uint8_t *out, const char *type, const uint8_t *data, size_t length)
{
    put32(out, length);
    memcpy(out + 4, type, 4);
    if (length > 0)
        memcpy(out + 8, data, length);
    put32(out + 8 + length, crc32(0, out + 4, (uInt)length + 4));
    return length + 12;
}

static void load_png(const uint8_t *png, size_t length, struct icns_image *image)
{
    unsigned count;
    begin();
    add("ic07", png, length);
    assert(load(0, image, &count) == CODEC_OK && count == 1);
}

static void test_container(void)
{
    struct icns_image image;
    unsigned count = 99;
    uint8_t mono[64];

    begin();
    file[0] = 'x';
    assert(load(ICNS_BEST, &image, &count) == CODEC_INVALID && count == 0);
    assert(icns_decode(file, 3, ICNS_BEST, &image, &count) == CODEC_INVALID);
    begin();
    assert(icns_decode(file, 6, ICNS_BEST, &image, &count) == CODEC_TRUNCATED);
    /* No images at all. */
    assert(load(ICNS_BEST, &image, &count) == CODEC_INVALID && count == 0);
    put32(file + 4, 7);
    assert(load(ICNS_BEST, &image, &count) == CODEC_INVALID);

    memset(mono, 0xff, sizeof mono);
    begin();
    add("ics#", mono, sizeof mono);
    assert(load(ICNS_BEST, &image, &count) == CODEC_OK && count == 1);
    icns_free(&image);
    /* The header's length is past the end of the data. */
    put32(file + 4, file_length + 1);
    assert(load(ICNS_BEST, &image, &count) == CODEC_TRUNCATED);
    /* Bytes after the header's length are ignored. */
    put32(file + 4, file_length);
    file_length += 5;
    assert(load(ICNS_BEST, &image, &count) == CODEC_OK);
    icns_free(&image);
    file_length -= 5;
    /* An entry header cut short, an entry shorter than its header, and an
       entry past the end. */
    put32(file + 4, file_length + 4);
    file_length += 4;
    assert(load(ICNS_BEST, &image, &count) == CODEC_TRUNCATED);
    file_length -= 4;
    put32(file + 4, file_length);
    put32(file + 12, 7);
    assert(load(ICNS_BEST, &image, &count) == CODEC_INVALID);
    put32(file + 12, 0xffffffffu);
    assert(load(ICNS_BEST, &image, &count) == CODEC_TRUNCATED);
    put32(file + 12, 8u + sizeof mono);

    /* Unknown and metadata entries are skipped. */
    begin();
    add("TOC ", "ics#\0\0\0\x48", 8);
    add("icnV", "\x42\x4a\0\0", 4);
    add("zzzz", "", 0);
    add("ics#", mono, sizeof mono);
    assert(load(ICNS_BEST, &image, &count) == CODEC_OK && count == 1);
    icns_free(&image);
}

static void test_legacy(void)
{
    struct icns_image image;
    unsigned count;
    uint8_t mono[256], icl8[1024], icl4[512], icm[48];
    size_t i;

    /* ICN#: the left half set (black), mask covering the top half. */
    memset(mono, 0, sizeof mono);
    for (i = 0; i < 32; i++)
        mono[i * 4] = mono[i * 4 + 1] = 0xff;
    memset(mono + 128, 0xff, 64);
    begin();
    add("ICN#", mono, sizeof mono);
    assert(load(0, &image, &count) == CODEC_OK && count == 1);
    assert(image.width == 32 && image.height == 32);
    expect_pixel(&image, 0, 0, 0, 0, 0, 255);
    expect_pixel(&image, 16, 0, 255, 255, 255, 255);
    expect_pixel(&image, 0, 16, 0, 0, 0, 0);
    icns_free(&image);

    /* icl8 takes its mask from ICN#; palette entries across the table. */
    for (i = 0; i < sizeof icl8; i++)
        icl8[i] = (uint8_t)i;
    add("icl8", icl8, sizeof icl8);
    assert(load(1, &image, &count) == CODEC_OK && count == 2);
    expect_pixel(&image, 0, 0, 255, 255, 255, 255);
    expect_pixel(&image, 1, 0, 255, 255, 204, 255);
    expect_pixel(&image, 6, 0, 255, 204, 255, 255);
    expect_pixel(&image, 22, 6, 0, 0, 51, 255);      /* index 214 */
    expect_pixel(&image, 23, 6, 238, 0, 0, 255);     /* 215: red ramp */
    expect_pixel(&image, 1, 7, 0, 238, 0, 255);      /* 225: green ramp */
    expect_pixel(&image, 11, 7, 0, 0, 238, 255);     /* 235: blue ramp */
    expect_pixel(&image, 21, 7, 238, 238, 238, 255); /* 245: grey ramp */
    expect_pixel(&image, 30, 7, 17, 17, 17, 255);    /* 254 */
    expect_pixel(&image, 31, 7, 0, 0, 0, 255);       /* 255 */
    expect_pixel(&image, 0, 16, 255, 255, 255, 0);
    icns_free(&image);
    /* The biggest image wins, then the deepest. */
    assert(load(ICNS_BEST, &image, &count) == CODEC_OK);
    expect_pixel(&image, 31, 7, 0, 0, 0, 255);
    icns_free(&image);

    /* icl4 packs two pixels a byte, high nibble first. */
    for (i = 0; i < sizeof icl4; i++)
        icl4[i] = (uint8_t)(i % 8u * 0x22u + 0x01u);
    begin();
    add("icl4", icl4, sizeof icl4);
    assert(load(0, &image, &count) == CODEC_OK);
    expect_pixel(&image, 0, 0, 255, 255, 255, 255);    /* entry 0 is white... */
    expect_pixel(&image, 1, 0, 0xfc, 0xf3, 0x05, 255); /* ...then 1 */
    expect_pixel(&image, 15, 0, 0, 0, 0, 255);         /* 15 */
    icns_free(&image);
    /* A short image is truncated; a short mask is ignored. */
    begin();
    add("icl4", icl4, sizeof icl4 - 1);
    assert(load(0, &image, &count) == CODEC_TRUNCATED && count == 1);
    begin();
    add("ICN#", mono, 200);
    add("icl8", icl8, sizeof icl8);
    assert(load(0, &image, &count) == CODEC_TRUNCATED && count == 2);
    assert(load(1, &image, &count) == CODEC_OK);
    expect_pixel(&image, 0, 31, 17, 0, 0, 255); /* index 224 */
    icns_free(&image);

    /* ICON has no mask; icm# is 16x12. */
    begin();
    add("ICON", mono, 128);
    assert(load(0, &image, &count) == CODEC_OK);
    expect_pixel(&image, 0, 31, 0, 0, 0, 255);
    icns_free(&image);
    memset(icm, 0, sizeof icm);
    icm[0] = 0x80;
    icm[24] = 0x80;
    begin();
    add("icm#", icm, sizeof icm);
    assert(load(0, &image, &count) == CODEC_OK);
    assert(image.width == 16 && image.height == 12);
    expect_pixel(&image, 0, 0, 0, 0, 0, 255);
    expect_pixel(&image, 1, 0, 255, 255, 255, 0);
    icns_free(&image);
    /* A mask that hides everything remains transparent. */
    memset(icm + 24, 0, 24);
    begin();
    add("icm#", icm, sizeof icm);
    assert(load(0, &image, &count) == CODEC_OK);
    expect_pixel(&image, 1, 0, 255, 255, 255, 0);
    icns_free(&image);
}

static void test_rgb(void)
{
    struct icns_image image;
    unsigned count;
    uint8_t rle[1024], mask[256], raw[768];
    size_t n = 0, i;

    /* Red: a run of 256 split as 130 + 126. Green: 4 literals, then two runs
       of 130 that overshoot and are clamped. Blue: a run of 130, then a
       literal of 128 that overshoots. */
    rle[n++] = 0xff; rle[n++] = 10;
    rle[n++] = 0xfb; rle[n++] = 10;
    rle[n++] = 3; rle[n++] = 1; rle[n++] = 2; rle[n++] = 3; rle[n++] = 4;
    rle[n++] = 0xff; rle[n++] = 20;
    rle[n++] = 0xff; rle[n++] = 20;
    rle[n++] = 0xff; rle[n++] = 30;
    rle[n++] = 127;
    for (i = 0; i < 128; i++)
        rle[n++] = 40;
    for (i = 0; i < 256; i++)
        mask[i] = (uint8_t)i;
    begin();
    add("is32", rle, n);
    add("s8mk", mask, sizeof mask);
    assert(load(0, &image, &count) == CODEC_OK && count == 1);
    expect_pixel(&image, 0, 0, 10, 1, 30, 0);
    expect_pixel(&image, 3, 0, 10, 4, 30, 3);
    expect_pixel(&image, 4, 0, 10, 20, 30, 4);
    expect_pixel(&image, 1, 8, 10, 20, 30, 129);
    expect_pixel(&image, 2, 8, 10, 20, 40, 130);
    expect_pixel(&image, 15, 15, 10, 20, 40, 255);
    icns_free(&image);

    /* Without a mask, or with a short one, the icon is opaque. */
    begin();
    add("is32", rle, n);
    add("s8mk", mask, 255);
    assert(load(0, &image, &count) == CODEC_OK);
    expect_pixel(&image, 0, 0, 10, 1, 30, 255);
    icns_free(&image);
    /* Packed data that ends early. */
    begin();
    add("is32", rle, n - 1);
    assert(load(0, &image, &count) == CODEC_TRUNCATED);
    begin();
    add("is32", rle, 1);
    assert(load(0, &image, &count) == CODEC_TRUNCATED);

    /* Exactly three bytes a pixel is uncompressed RGB. */
    for (i = 0; i < sizeof raw; i++)
        raw[i] = (uint8_t)i;
    begin();
    add("is32", raw, sizeof raw);
    assert(load(0, &image, &count) == CODEC_OK);
    expect_pixel(&image, 1, 0, 3, 4, 5, 255);
    icns_free(&image);

    /* it32 skips four bytes first; without data after them it's short. */
    begin();
    add("it32", "\0\0\0\0", 4);
    assert(load(0, &image, &count) == CODEC_TRUNCATED);
    begin();
    add("it32", "\0\0", 2);
    assert(load(0, &image, &count) == CODEC_TRUNCATED);
    {
        static uint8_t big[4 + 3 * 256];
        size_t m = 4, c, k;
        memset(big, 0xaa, 4);
        for (c = 0; c < 3; c++) {
            for (k = 0; k < 126; k++) {
                big[m++] = 0xff;
                big[m++] = (uint8_t)(50 + c);
            }
            big[m++] = 0x80 + 4; /* 126 * 130 + 7 = 16387 > 16384: clamped */
            big[m++] = (uint8_t)(50 + c);
        }
        begin();
        add("it32", big, m);
        assert(load(0, &image, &count) == CODEC_OK);
        assert(image.width == 128);
        expect_pixel(&image, 127, 127, 50, 51, 52, 255);
        icns_free(&image);
    }
    /* icp4 that isn't PNG or ARGB holds 24-bit RLE. */
    begin();
    add("icp4", rle, n);
    assert(load(0, &image, &count) == CODEC_OK);
    expect_pixel(&image, 0, 0, 10, 1, 30, 255);
    icns_free(&image);
    /* ic07 that isn't PNG, JPEG 2000 or ARGB isn't an image. */
    begin();
    add("ic07", rle, n);
    assert(load(ICNS_BEST, &image, &count) == CODEC_INVALID && count == 0);
}

static void test_argb(void)
{
    struct icns_image image;
    unsigned count;
    uint8_t data[4 + 16];
    size_t n = 4, c;

    /* Two runs of 130 a channel overshoot 256 pixels and are clamped. */
    memcpy(data, "ARGB", 4);
    for (c = 0; c < 4; c++) {
        static const uint8_t values[4] = {200, 1, 2, 3};
        data[n++] = 0xff; data[n++] = values[c];
        data[n++] = 0xff; data[n++] = values[c];
    }
    begin();
    add("ic04", data, n);
    assert(load(0, &image, &count) == CODEC_OK);
    assert(image.width == 16 && image.height == 16);
    expect_pixel(&image, 15, 15, 1, 2, 3, 200);
    icns_free(&image);
    begin();
    add("ic04", data, n - 1);
    assert(load(0, &image, &count) == CODEC_TRUNCATED);
    /* icsb is 18x18, so 260 pixels a channel isn't enough. */
    begin();
    add("icsb", data, n);
    assert(load(0, &image, &count) == CODEC_TRUNCATED);
}

static void test_png(void)
{
    static uint8_t png[200000], raw[100000], extra[1000];
    struct icns_image image;
    unsigned count, x, y, w, h;
    size_t n, e;

    /* 8-bit RGBA with each filter type on its own row. */
    {
        uint8_t rows[5][1 + 8] = {
            {0, 10, 20, 30, 40, 50, 60, 70, 80},
            {1, 10, 20, 30, 40, 5, 5, 5, 5},
            {2, 1, 1, 1, 1, 2, 2, 2, 2},
            {3, 10, 10, 10, 10, 0, 0, 0, 0},
            {4, 0, 0, 0, 0, 1, 1, 1, 1}
        };
        n = make_png(png, 2, 5, 8, 6, 0, &rows[0][0], sizeof rows, NULL, 0);
        load_png(png, n, &image);
        expect_pixel(&image, 1, 0, 50, 60, 70, 80);
        expect_pixel(&image, 1, 1, 15, 25, 35, 45);  /* sub */
        expect_pixel(&image, 0, 2, 11, 21, 31, 41);  /* up */
        expect_pixel(&image, 1, 2, 17, 27, 37, 47);
        expect_pixel(&image, 0, 3, 15, 20, 25, 30);  /* average of 0 and up */
        expect_pixel(&image, 1, 3, 16, 23, 31, 38);  /* average of left and up */
        expect_pixel(&image, 0, 4, 15, 20, 25, 30);  /* paeth picks up */
        expect_pixel(&image, 1, 4, 17, 24, 32, 39);
        icns_free(&image);
        rows[2][0] = 5;
        n = make_png(png, 2, 5, 8, 6, 0, &rows[0][0], sizeof rows, NULL, 0);
        begin();
        add("ic07", png, n);
        assert(load(0, &image, &count) == CODEC_INVALID);
    }

    /* Greyscale at every depth, with tRNS on the 2-bit image. */
    {
        static const unsigned depths[5] = {1, 2, 4, 8, 16};
        unsigned i;
        for (i = 0; i < 5; i++) {
            unsigned d = depths[i], max = d == 16 ? 65535u : (1u << d) - 1u;
            size_t rb = (8u * d + 7u) / 8u;
            memset(raw, 0, 2 * (rb + 1));
            /* Row 0: sample max at x=0; row 1: sample 1 at x=7. */
            if (d == 16) {
                raw[1] = raw[2] = 0xff;
                raw[rb + 1 + 1 + 14] = 0;
                raw[rb + 1 + 1 + 15] = 1;
            } else if (d == 8) {
                raw[1] = 0xff;
                raw[rb + 1 + 1 + 7] = 1;
            } else {
                raw[1] = (uint8_t)(max << (8 - d));
                raw[rb + 1 + 1 + (7 * d) / 8] = (uint8_t)(1u << (8 - d - (7 * d) % 8));
            }
            e = 0;
            if (d == 2)
                e = chunk(extra, "tRNS", (const uint8_t *)"\0\1", 2);
            n = make_png(png, 8, 2, d, 0, 0, raw, 2 * (rb + 1), extra, e);
            load_png(png, n, &image);
            expect_pixel(&image, 0, 0, 255, 255, 255, 255);
            x = d == 16 ? 0 : 255u / max;
            expect_pixel(&image, 7, 1, x, x, x, d == 2 ? 0 : 255);
            expect_pixel(&image, 1, 0, 0, 0, 0, 255);
            icns_free(&image);
        }
    }

    /* 16-bit channels round to the nearest 8-bit value. */
    {
        uint8_t row[1 + 12] = {0, 0x80, 0x80, 0x00, 0x7f, 0xff, 0xff, 0x12, 0x34, 0, 0, 0, 0};
        n = make_png(png, 1, 1, 16, 2, 0, row, 7, NULL, 0);
        load_png(png, n, &image);
        expect_pixel(&image, 0, 0, 128, 0, 255, 255);
        icns_free(&image);
        e = chunk(extra, "tRNS", (const uint8_t *)"\x80\x80\x00\x7f\xff\xff", 6);
        n = make_png(png, 1, 1, 16, 2, 0, row, 7, extra, e);
        load_png(png, n, &image);
        /* PNG tRNS declares this pixel transparent. */
        expect_pixel(&image, 0, 0, 128, 0, 255, 0);
        icns_free(&image);
        n = make_png(png, 1, 1, 16, 6, 0, row, 9, NULL, 0);
        load_png(png, n, &image);
        expect_pixel(&image, 0, 0, 128, 0, 255, 18);
        icns_free(&image);
        n = make_png(png, 1, 1, 16, 4, 0, row, 5, NULL, 0);
        load_png(png, n, &image);
        expect_pixel(&image, 0, 0, 128, 128, 128, 0); /* alpha rounds to 0 */
        icns_free(&image);
    }

    /* 8-bit RGB with tRNS, and grey with alpha. */
    {
        uint8_t row[1 + 6] = {0, 1, 2, 3, 4, 5, 6};
        e = chunk(extra, "tRNS", (const uint8_t *)"\0\1\0\2\0\3", 6);
        n = make_png(png, 2, 1, 8, 2, 0, row, 7, extra, e);
        load_png(png, n, &image);
        expect_pixel(&image, 0, 0, 1, 2, 3, 0);
        expect_pixel(&image, 1, 0, 4, 5, 6, 255);
        icns_free(&image);
        /* Malformed tRNS is ignored. */
        e = chunk(extra, "tRNS", (const uint8_t *)"\0\1\0\2", 4);
        n = make_png(png, 2, 1, 8, 2, 0, row, 7, extra, e);
        load_png(png, n, &image);
        expect_pixel(&image, 0, 0, 1, 2, 3, 255);
        icns_free(&image);
        n = make_png(png, 2, 1, 8, 4, 0, row, 5, NULL, 0);
        load_png(png, n, &image);
        expect_pixel(&image, 1, 0, 3, 3, 3, 4);
        icns_free(&image);
    }

    /* Palettes at 1, 2, 4 and 8 bits, with partial tRNS and an index past
       the palette. */
    {
        static const unsigned depths[4] = {1, 2, 4, 8};
        unsigned i;
        for (i = 0; i < 4; i++) {
            unsigned d = depths[i];
            uint8_t row[1 + 2] = {0, 0, 0};
            row[1] = (uint8_t)(1u << (8 - d)); /* index 1 at x=0 */
            if (d == 8)
                row[2] = 7; /* past the palette at x=1 */
            e = chunk(extra, "PLTE", (const uint8_t *)"\x10\x20\x30\x40\x50\x60", 6);
            e += chunk(extra + e, "tRNS", (const uint8_t *)"\x80", 1);
            n = make_png(png, 2, 1, d, 3, 0, row, d == 8 ? 3 : 2, extra, e);
            load_png(png, n, &image);
            expect_pixel(&image, 0, 0, 0x40, 0x50, 0x60, 255);
            if (d == 8)
                expect_pixel(&image, 1, 0, 0, 0, 0, 255);
            else
                expect_pixel(&image, 1, 0, 0x10, 0x20, 0x30, 0x80);
            icns_free(&image);
        }
        /* No palette, or a broken one, for a palette image. */
        n = make_png(png, 1, 1, 8, 3, 0, (const uint8_t *)"\0\0", 2, NULL, 0);
        begin();
        add("ic07", png, n);
        assert(load(0, &image, &count) == CODEC_INVALID);
        e = chunk(extra, "PLTE", (const uint8_t *)"\x10\x20", 2);
        n = make_png(png, 1, 1, 8, 3, 0, (const uint8_t *)"\0\0", 2, extra, e);
        begin();
        add("ic07", png, n);
        assert(load(0, &image, &count) == CODEC_INVALID);
    }

    /* Adam7: 9x9 greyscale where each pixel is x + 10y. */
    {
        static const unsigned px[7] = {0, 4, 0, 2, 0, 1, 0}, py[7] = {0, 0, 4, 0, 2, 0, 1};
        static const unsigned dx[7] = {8, 8, 4, 4, 2, 2, 1}, dy[7] = {8, 8, 8, 4, 4, 2, 2};
        unsigned p;
        size_t m = 0;
        for (p = 0; p < 7; p++)
            for (y = py[p]; y < 9; y += dy[p]) {
                raw[m++] = 0;
                for (x = px[p]; x < 9; x += dx[p])
                    raw[m++] = (uint8_t)(x + 10 * y);
            }
        n = make_png(png, 9, 9, 8, 0, 1, raw, m, NULL, 0);
        load_png(png, n, &image);
        for (y = 0; y < 9; y++)
            for (x = 0; x < 9; x++)
                expect_pixel(&image, x, y, x + 10 * y, x + 10 * y, x + 10 * y, 255);
        icns_free(&image);
        /* Too little image data, and extra data after it. */
        n = make_png(png, 9, 9, 8, 0, 1, raw, m - 1, NULL, 0);
        begin();
        add("ic07", png, n);
        assert(load(0, &image, &count) == CODEC_INVALID);
        raw[m] = 0;
        n = make_png(png, 9, 9, 8, 0, 1, raw, m + 1, NULL, 0);
        load_png(png, n, &image);
        icns_free(&image);
    }

    /* The PNG's own size counts, and chooses the default. */
    {
        uint8_t row[1 + 3 * 3];
        memset(row, 7, sizeof row);
        row[0] = 0;
        memset(raw, 0, 1 + 3 * 40);
        for (y = 0; y < 40; y++)
            memcpy(raw + y * (1 + 3 * 40) + 1, row + 1, 9);
        n = make_png(png, 40, 40, 8, 2, 0, raw, 40 * (1 + 3 * 40), NULL, 0);
        begin();
        add("il32", (const uint8_t *)"\xff\x01\xff\x01\xff\x01", 6); /* bad */
        add("ic11", png, n);
        assert(load(ICNS_BEST, &image, &count) == CODEC_OK && count == 2);
        assert(image.width == 40 && image.height == 40);
        icns_free(&image);
    }

    /* Broken PNGs: signature only, bad IHDR, truncated chunks, no IDAT,
       corrupt deflate, a huge size. */
    {
        uint8_t row[2] = {0, 9};
        n = make_png(png, 1, 1, 8, 0, 0, row, 2, NULL, 0);
        begin();
        add("ic07", png, 8);
        assert(load(ICNS_BEST, &image, &count) == CODEC_TRUNCATED && count == 1);
        assert(load(0, &image, &count) == CODEC_TRUNCATED);
        png[24] = 3; /* depth 3 */
        begin();
        add("ic07", png, n);
        assert(load(0, &image, &count) == CODEC_INVALID);
        png[24] = 8;
        png[25] = 1; /* colour type 1 */
        begin();
        add("ic07", png, n);
        assert(load(0, &image, &count) == CODEC_INVALID);
        png[25] = 0;
        png[28] = 2; /* interlace 2 */
        begin();
        add("ic07", png, n);
        assert(load(0, &image, &count) == CODEC_INVALID);
        png[28] = 0;
        begin();
        add("ic07", png, n - 12); /* no IEND is fine */
        assert(load(0, &image, &count) == CODEC_OK);
        expect_pixel(&image, 0, 0, 9, 9, 9, 255);
        icns_free(&image);
        begin();
        add("ic07", png, n - 13); /* IDAT cut short */
        assert(load(0, &image, &count) == CODEC_TRUNCATED);
        begin();
        add("ic07", png, 33 + 12 + 5); /* IDAT length past the end */
        assert(load(0, &image, &count) == CODEC_TRUNCATED);
        e = chunk(png + 33, "IEND", NULL, 0);
        begin();
        add("ic07", png, 33 + e);
        assert(load(0, &image, &count) == CODEC_INVALID);
        begin();
        add("ic07", png, 33); /* nothing after IHDR */
        assert(load(0, &image, &count) == CODEC_TRUNCATED);
        n = make_png(png, 1, 1, 8, 0, 0, row, 2, NULL, 0);
        png[33 + 8 + 2] ^= 0xff; /* deflate data */
        begin();
        add("ic07", png, n);
        assert(load(0, &image, &count) == CODEC_INVALID);
        n = make_png(png, 1, 1, 8, 0, 0, row, 2, NULL, 0);
        put32(png + 33, 0x80000000u); /* chunk length */
        begin();
        add("ic07", png, n);
        assert(load(0, &image, &count) == CODEC_INVALID);
        n = make_png(png, 1, 1, 8, 0, 0, row, 2, NULL, 0);
        put32(png + 16, 65536);
        assert(png_info(png, n, &w, &h) == CODEC_TOO_LARGE);
        put32(png + 16, 4097);
        put32(png + 20, 4096);
        assert(png_info(png, n, &w, &h) == CODEC_TOO_LARGE);
        put32(png + 16, 0);
        assert(png_info(png, n, &w, &h) == CODEC_INVALID);
    }

    /* JPEG 2000 entries aren't counted. */
    begin();
    add("ic08", "\0\0\0\x0cjP  \r\n\x87\n", 12);
    add("ic09", "\xff\x4f\xff\x51", 4);
    assert(load(ICNS_BEST, &image, &count) == CODEC_INVALID && count == 0);
}

static size_t solid_png(uint8_t *png, unsigned size, uint8_t value)
{
    static uint8_t raw[64 * (1 + 64 * 3)];
    size_t row = 1u + size * 3u, y;
    memset(raw, value, row * size);
    for (y = 0; y < size; y++)
        raw[y * row] = 0;
    return make_png(png, size, size, 8, 2, 0, raw, row * size, NULL, 0);
}

static void test_select(void)
{
    struct icns_image image;
    unsigned count;
    uint8_t mono[64], mask[256];
    uint8_t rle[12] = {0xff, 1, 0xff, 1, 0xff, 2, 0xff, 2, 0xff, 3, 0xff, 3};
    static uint8_t png2x[8192], png1x[8192], bad[8192];
    size_t n2, n1, nb;

    memset(mono, 0, sizeof mono);
    memset(mask, 255, sizeof mask);
    n2 = solid_png(png2x, 32, 0x44);
    n1 = solid_png(png1x, 32, 0x55);
    nb = solid_png(bad, 32, 0x66);
    bad[33 + 8 + 2] ^= 0x55;

    begin();
    add("ics#", mono, sizeof mono);
    add("is32", rle, sizeof rle);
    add("s8mk", mask, sizeof mask);
    add("ic11", png2x, n2);
    add("icp5", png1x, n1);
    assert(load(ICNS_BEST, &image, &count) == CODEC_OK && count == 4);
    /* At equal size and depth, the 1x slot beats the 2x one. */
    assert(image.width == 32);
    expect_pixel(&image, 0, 0, 0x55, 0x55, 0x55, 255);
    icns_free(&image);
    assert(load(3, &image, &count) == CODEC_OK);
    expect_pixel(&image, 0, 0, 0x55, 0x55, 0x55, 255);
    icns_free(&image);
    assert(load(2, &image, &count) == CODEC_OK);
    expect_pixel(&image, 0, 0, 0x44, 0x44, 0x44, 255);
    icns_free(&image);
    assert(load(1, &image, &count) == CODEC_OK);
    expect_pixel(&image, 0, 0, 1, 2, 3, 255);
    icns_free(&image);
    assert(load(0, &image, &count) == CODEC_OK);
    expect_pixel(&image, 0, 0, 255, 255, 255, 0); /* mask all clear */
    icns_free(&image);
    assert(load(4, &image, &count) == CODEC_INVALID && count == 4);
    assert(load(-2, &image, &count) == CODEC_INVALID);

    /* A later entry that ties doesn't replace the first; a broken header
       counts but is never preferred. */
    begin();
    add("ic12", png1x, 8);
    add("icp5", png1x, n1);
    add("icp5", bad, nb);
    assert(load(ICNS_BEST, &image, &count) == CODEC_OK && count == 3);
    expect_pixel(&image, 0, 0, 0x55, 0x55, 0x55, 255);
    icns_free(&image);
    assert(load(2, &image, &count) == CODEC_INVALID);
    assert(load(0, &image, &count) == CODEC_TRUNCATED);

    /* Deeper wins at equal size. */
    begin();
    add("ics#", mono, sizeof mono);
    add("ics8", mask, sizeof mask);
    add("ics4", mask, 128);
    assert(load(ICNS_BEST, &image, &count) == CODEC_OK);
    expect_pixel(&image, 0, 0, 0, 0, 0, 0); /* index 255 is black; mask is clear */
    icns_free(&image);
}

static void roundtrip(unsigned size, int alpha)
{
    struct icns_image image;
    unsigned count;
    size_t pixels = (size_t)size * size, i, length;
    uint8_t *rgba = malloc(pixels * 4u), *out;

    assert(rgba != NULL);
    for (i = 0; i < pixels; i++) {
        rgba[i * 4u] = (uint8_t)(i * 7u);
        rgba[i * 4u + 1u] = (uint8_t)(i / size);
        rgba[i * 4u + 2u] = (uint8_t)(i % 3u == 0 ? 9 : i);
        rgba[i * 4u + 3u] = alpha ? (uint8_t)(i * 13u) : 255;
    }
    assert(icns_can_encode(size, size));
    assert(icns_encode(rgba, size, size, &out, &length) == CODEC_OK);
    assert(icns_decode(out, length, ICNS_BEST, &image, &count) == CODEC_OK && count == 1);
    assert(image.width == size && image.height == size);
    assert(memcmp(image.rgba, rgba, pixels * 4u) == 0);
    icns_free(&image);
    free(out);
    free(rgba);
}

static void test_encode(void)
{
    static const unsigned sizes[8] = {16, 32, 48, 128, 64, 256, 512, 1024};
    uint8_t rgba[16 * 16 * 4], *out;
    size_t length, i;
    unsigned s, count;
    struct icns_image image;

    for (s = 0; s < 8; s++) {
        roundtrip(sizes[s], 0);
        roundtrip(sizes[s], 1);
    }
    assert(!icns_can_encode(16, 32));
    assert(!icns_can_encode(100, 100));
    assert(icns_encode(rgba, 100, 100, &out, &length) == CODEC_INVALID);
    assert(icns_encode(rgba, 16, 32, &out, &length) == CODEC_INVALID);

    /* Red and green don't repeat; blue starts with a run of 8. The packed
       data would be exactly 768 bytes, so it's written uncompressed. */
    for (i = 0; i < 256; i++) {
        rgba[i * 4u] = (uint8_t)i;
        rgba[i * 4u + 1u] = (uint8_t)(255u - i);
        rgba[i * 4u + 2u] = i < 8 ? 0 : (uint8_t)i;
        rgba[i * 4u + 3u] = 255;
    }
    assert(icns_encode(rgba, 16, 16, &out, &length) == CODEC_OK);
    assert(length == 8 + 8 + 768 + 8 + 256);
    assert(memcmp(out + 8, "is32", 4) == 0 && out[16 + 3] == 1 && out[16 + 4] == 254);
    assert(memcmp(out + 16 + 768, "s8mk", 4) == 0);
    assert(icns_decode(out, length, ICNS_BEST, &image, &count) == CODEC_OK);
    assert(memcmp(image.rgba, rgba, sizeof rgba) == 0);
    icns_free(&image);
    free(out);
}

static void test_png_encode(void)
{
    uint8_t rgba[3 * 2 * 4], *png, *back;
    size_t length, i;
    unsigned w, h;

    for (i = 0; i < sizeof rgba; i++)
        rgba[i] = (uint8_t)(i * 5u);
    for (i = 0; i < 6; i++)
        rgba[i * 4u + 3u] = 255;
    assert(png_encode(rgba, 3, 2, &png, &length) == CODEC_OK);
    assert(png[25] == 2);
    assert(crc32(0, png + 12, 17) == (uLong)((uint32_t)png[29] << 24 | (uint32_t)png[30] << 16 |
                                             (uint32_t)png[31] << 8 | png[32]));
    assert(png_decode(png, length, &w, &h, &back) == CODEC_OK && w == 3 && h == 2);
    assert(memcmp(back, rgba, sizeof rgba) == 0);
    free(back);
    free(png);
    rgba[7] = 0;
    assert(png_encode(rgba, 3, 2, &png, &length) == CODEC_OK);
    assert(png[25] == 6);
    assert(png_decode(png, length, &w, &h, &back) == CODEC_OK);
    assert(memcmp(back, rgba, sizeof rgba) == 0);
    free(back);
    free(png);
    assert(png_encode(rgba, 0, 2, &png, &length) == CODEC_TOO_LARGE);
}

static void test_zlib(void)
{
    uint8_t src[1000], z[1100], out[1000];
    size_t zn, n;

    for (n = 0; n < sizeof src; n++)
        src[n] = (uint8_t)(n * n);
    assert(zlib_deflate(src, sizeof src, z, sizeof z, 9, &zn) == CODEC_OK);
    assert(zlib_deflate(src, sizeof src, z, 10, 9, &n) == CODEC_TOO_LARGE);
    assert(zlib_deflate_bound(sizeof src) >= compressBound(sizeof src));
    assert(zlib_deflate_bound((size_t)-1) == 0);
    assert(zlib_inflate(z, zn, out, sizeof out, &n) == CODEC_OK && n == sizeof src);
    assert(memcmp(out, src, sizeof src) == 0);
    assert(zlib_inflate(z, zn, out, 999, &n) == CODEC_TOO_LARGE && n == 999);
    assert(zlib_inflate(z, zn - 1, out, sizeof out, &n) == CODEC_TRUNCATED);
    assert(zlib_inflate(z, 0, out, sizeof out, &n) == CODEC_TRUNCATED && n == 0);
    z[zn - 1] ^= 1; /* checksum */
    assert(zlib_inflate(z, zn, out, sizeof out, &n) == CODEC_INVALID);
    z[0] = 0; /* header */
    assert(zlib_inflate(z, zn, out, sizeof out, &n) == CODEC_INVALID);
}

int main(void)
{
    test_zlib();
    test_container();
    test_legacy();
    test_rgb();
    test_argb();
    test_png();
    test_select();
    test_encode();
    test_png_encode();
    puts("icns tests passed");
    return 0;
}
