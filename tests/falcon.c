#include "../formats/falcon/decode.h"
#include "../formats/falcon/encode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUFFER (4u * 1024u * 1024u)

static uint8_t *file;
static uint8_t *scratch;

static void put16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, v >> 16);
    put16(p + 2, v & 0xffffu);
}

/* A deterministic colour for pixel (x, y). */
static unsigned pattern(unsigned x, unsigned y, unsigned salt)
{
    return (x * 7u + y * 13u + salt * 29u + (x ^ y)) & 0xffffu;
}

static void expect(const struct falcon_image *image, unsigned x, unsigned y,
                   unsigned r, unsigned g, unsigned b)
{
    const uint8_t *p = image->rgba + ((size_t)y * image->width + x) * 4u;
    if (p[0] != r || p[1] != g || p[2] != b || p[3] != 255) {
        fprintf(stderr, "(%u,%u) is %u,%u,%u,%u, expected %u,%u,%u\n", x, y,
                p[0], p[1], p[2], p[3], r, g, b);
        assert(0);
    }
}

static void expect565(const struct falcon_image *image, unsigned x, unsigned y,
                      unsigned w)
{
    unsigned r = w >> 11, g = (w >> 5) & 63u, b = w & 31u;
    expect(image, x, y, (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2));
}

static enum codec_result decode(size_t length, unsigned index,
                                struct falcon_image *image, unsigned *count)
{
    return falcon_decode(file, length, index, image, count);
}

/* Every shorter prefix of a valid file must fail cleanly as truncated or
   unrecognised; step keeps large files quick. */
static void prefixes(size_t length, size_t step)
{
    struct falcon_image image;
    size_t n;

    for (n = 0; n < length; n += (n < 1100 ? 1 : step)) {
        enum codec_result r = decode(n, 0, &image, NULL);
        assert(r == CODEC_TRUNCATED || r == CODEC_INVALID);
        assert(image.rgba == NULL);
    }
}

static void fill565(size_t at, unsigned width, unsigned height, unsigned salt)
{
    unsigned x, y;
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++)
            put16(file + at + ((size_t)y * width + x) * 2u, pattern(x, y, salt));
}

static void check565(const struct falcon_image *image, unsigned salt)
{
    unsigned x, y;
    for (y = 0; y < image->height; y++)
        for (x = 0; x < image->width; x++)
            expect565(image, x, y, pattern(x, y, salt));
}

/* Set pixel (x, y) of interleaved bitplanes to index. */
static void plot(uint8_t *bitmap, size_t stride, unsigned planes, unsigned x,
                 unsigned y, unsigned index)
{
    uint8_t *group = bitmap + (size_t)y * stride + (size_t)(x / 16u) * planes * 2u;
    unsigned p, bit = 15u - x % 16u;

    for (p = 0; p < planes; p++) {
        uint8_t *at = group + p * 2u + (bit < 8u);
        *at = (uint8_t)(*at & ~(1u << bit % 8u));
        if (index >> p & 1u)
            *at |= (uint8_t)(1u << bit % 8u);
    }
}

static void fill_planes(uint8_t *bitmap, size_t stride, unsigned planes,
                        unsigned width, unsigned height, unsigned salt)
{
    unsigned x, y;
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++)
            plot(bitmap, stride, planes, x, y, pattern(x, y, salt) & ((1u << planes) - 1u));
}

static unsigned falcon_level(unsigned byte)
{
    byte &= 0xfcu;
    return byte | byte >> 6;
}

/* A Falcon palette at at: entry i is R, G, 0, B. */
static void falcon_palette(size_t at)
{
    unsigned i;
    for (i = 0; i < 256; i++) {
        file[at + i * 4u] = (uint8_t)(i * 5u + 3u);
        file[at + i * 4u + 1u] = (uint8_t)(255u - i);
        file[at + i * 4u + 2u] = 0;
        file[at + i * 4u + 3u] = (uint8_t)(i * 3u + 1u);
    }
}

static void check_falcon_planes(const struct falcon_image *image, unsigned salt)
{
    unsigned x, y;
    for (y = 0; y < image->height; y++)
        for (x = 0; x < image->width; x++) {
            unsigned i = pattern(x, y, salt) & 255u;
            expect(image, x, y, falcon_level(i * 5u + 3u), falcon_level(255u - i),
                   falcon_level(i * 3u + 1u));
        }
}

static void test_true_colour(void)
{
    struct falcon_image image;
    unsigned count = 99;
    size_t length;

    /* GodPaint: ID word ignored, only the size tells. */
    memset(file, 0, BUFFER);
    put16(file, 0x0434);
    put16(file + 2, 37);
    put16(file + 4, 11);
    fill565(6, 37, 11, 1);
    length = 6 + 37 * 11 * 2;
    assert(decode(length, 0, &image, &count) == CODEC_OK);
    assert(count == 1 && image.format == FALCON_GOD);
    assert(image.width == 37 && image.height == 11);
    check565(&image, 1);
    falcon_free(&image);
    assert(decode(length, 1, &image, &count) == CODEC_INVALID && count == 1);
    /* One byte more or less is no longer GodPaint. */
    assert(decode(length + 1, 0, &image, NULL) == CODEC_INVALID);
    assert(decode(length - 1, 0, &image, NULL) == CODEC_INVALID);
    /* Zero width can't be recognised. */
    put16(file + 2, 0);
    assert(decode(6, 0, &image, NULL) == CODEC_INVALID);

    /* EggPaint, both IDs, and bytes after the picture ignored. */
    memset(file, 0, BUFFER);
    memcpy(file, "TRUP", 4);
    put16(file + 4, 16);
    put16(file + 6, 3);
    fill565(8, 16, 3, 2);
    length = 8 + 16 * 3 * 2;
    assert(decode(length + 5, 0, &image, NULL) == CODEC_OK);
    assert(image.format == FALCON_EGG);
    check565(&image, 2);
    falcon_free(&image);
    memcpy(file, "tru?", 4);
    assert(decode(length, 0, &image, NULL) == CODEC_OK);
    falcon_free(&image);
    prefixes(length, 1);
    put16(file + 6, 0);
    assert(decode(length, 0, &image, NULL) == CODEC_INVALID);
    put16(file + 4, 4097);
    put16(file + 6, 4097);
    assert(decode(length, 0, &image, NULL) == CODEC_TOO_LARGE);

    /* IndyPaint: data at 256. */
    memset(file, 0, BUFFER);
    memcpy(file, "Indy", 4);
    put16(file + 4, 5);
    put16(file + 6, 9);
    fill565(256, 5, 9, 3);
    length = 256 + 5 * 9 * 2;
    assert(decode(length, 0, &image, NULL) == CODEC_OK);
    assert(image.format == FALCON_INDY);
    check565(&image, 3);
    falcon_free(&image);
    prefixes(length, 1);

    /* COKE: the data offset is honoured. */
    memset(file, 0, BUFFER);
    memcpy(file, "COKE format.", 12);
    put16(file + 12, 7);
    put16(file + 14, 6);
    put16(file + 16, 18);
    fill565(18, 7, 6, 4);
    length = 18 + 7 * 6 * 2;
    assert(decode(length, 0, &image, NULL) == CODEC_OK);
    assert(image.format == FALCON_COKE);
    check565(&image, 4);
    falcon_free(&image);
    prefixes(length, 1);
    memmove(file + 22, file + 18, 7 * 6 * 2);
    put16(file + 16, 22);
    assert(decode(length + 4, 0, &image, NULL) == CODEC_OK);
    check565(&image, 4);
    falcon_free(&image);
    put16(file + 16, 17);
    assert(decode(length + 4, 0, &image, NULL) == CODEC_INVALID);

    /* Falcon true colour screen dump, by size. */
    memset(file, 0, BUFFER);
    fill565(0, 384, 240, 5);
    assert(decode(184320, 0, &image, NULL) == CODEC_OK);
    assert(image.format == FALCON_FTC && image.width == 384 && image.height == 240);
    check565(&image, 5);
    falcon_free(&image);

    /* Channel expansion repeats the top bits. */
    memset(file, 0, BUFFER);
    memcpy(file, "TRUP", 4);
    put16(file + 4, 3);
    put16(file + 6, 1);
    put16(file + 8, 0xffff);
    put16(file + 10, 0x0000);
    put16(file + 12, 0x8410);
    assert(decode(14, 0, &image, NULL) == CODEC_OK);
    expect(&image, 0, 0, 255, 255, 255);
    expect(&image, 1, 0, 0, 0, 0);
    expect(&image, 2, 0, 132, 130, 132);
    falcon_free(&image);
}

/* Rembrandt with pictures of the given sizes. Returns the file length. */
static size_t rembrandt(unsigned pictures, const unsigned (*sizes)[2])
{
    size_t at = 18;
    unsigned i;

    memset(file, 0, BUFFER);
    memcpy(file, "TRUECOLR", 8);
    put16(file + 12, 18);
    put16(file + 14, 1);
    put16(file + 16, pictures);
    for (i = 0; i < pictures; i++) {
        size_t data = (size_t)sizes[i][0] * sizes[i][1] * 2u;
        memcpy(file + at, "PICT", 4);
        put32(file + at + 4, (uint32_t)data);
        put16(file + at + 8, 198);
        put16(file + at + 10, sizes[i][0]);
        put16(file + at + 12, sizes[i][1]);
        fill565(at + 198, sizes[i][0], sizes[i][1], 10 + i);
        at += 198 + data;
    }
    put32(file + 8, (uint32_t)at);
    return at;
}

static void test_rembrandt(void)
{
    static const unsigned sizes[3][2] = { { 4, 3 }, { 9, 2 }, { 1, 1 } };
    struct falcon_image image;
    unsigned count, i;
    size_t length = rembrandt(3, sizes);

    for (i = 0; i < 3; i++) {
        count = 0;
        assert(decode(length, i, &image, &count) == CODEC_OK);
        assert(count == 3 && image.format == FALCON_REMBRANDT);
        assert(image.width == sizes[i][0] && image.height == sizes[i][1]);
        check565(&image, 10 + i);
        falcon_free(&image);
    }
    count = 0;
    assert(decode(length, 3, &image, &count) == CODEC_INVALID && count == 3);
    /* Up to the end of the first picture; later ones don't affect it. */
    prefixes(18 + 198 + 24, 1);
    /* Truncated inside the second picture still loads the first. */
    assert(decode(18 + 198 + 24 + 100, 0, &image, NULL) == CODEC_OK);
    falcon_free(&image);
    assert(decode(18 + 198 + 24 + 100, 1, &image, NULL) == CODEC_TRUNCATED);
    /* A picture's data length that runs past the file. */
    put32(file + 18 + 4, 0xfffffff0u);
    assert(decode(length, 1, &image, NULL) == CODEC_TRUNCATED);
    length = rembrandt(3, sizes);
    /* Compression was never implemented. */
    file[18 + 18] = 1;
    assert(decode(length, 0, &image, NULL) == CODEC_INVALID);
    file[18 + 18] = 0;
    memcpy(file + 18, "PICS", 4);
    assert(decode(length, 0, &image, NULL) == CODEC_INVALID);
    memcpy(file + 18, "PICT", 4);
    put16(file + 18 + 8, 19);
    assert(decode(length, 0, &image, NULL) == CODEC_INVALID);
    put16(file + 18 + 8, 198);
    put16(file + 16, 0);
    assert(decode(length, 0, &image, NULL) == CODEC_INVALID);
    put16(file + 16, 3);
    put16(file + 12, 17);
    assert(decode(length, 0, &image, NULL) == CODEC_INVALID);
}

static void test_tt(void)
{
    struct falcon_image image;
    unsigned x, y, i;

    /* TT low: 256 colours, 320x480, 8 planes. */
    memset(file, 0, BUFFER);
    put16(file, 7);
    for (i = 0; i < 256; i++)
        put16(file + 2 + i * 2u, 0xf000u | (i * 37u & 0xfffu));
    fill_planes(file + 514, 320, 8, 320, 480, 6);
    assert(decode(154114, 0, &image, NULL) == CODEC_OK);
    assert(image.format == FALCON_TT_LOW && image.width == 320 && image.height == 480);
    for (y = 0; y < 480; y += 7)
        for (x = 0; x < 320; x++) {
            unsigned c = (pattern(x, y, 6) & 255u) * 37u & 0xfffu;
            expect(&image, x, y, (c >> 8) * 17u, (c >> 4 & 15u) * 17u, (c & 15u) * 17u);
        }
    falcon_free(&image);
    /* DEGAS Elite's 32 extra bytes. */
    assert(decode(154114 + 32, 0, &image, NULL) == CODEC_OK);
    falcon_free(&image);
    assert(decode(154114 - 1, 0, &image, NULL) == CODEC_INVALID);
    assert(decode(154114 + 1, 0, &image, NULL) == CODEC_INVALID);

    /* TT medium: 16 colours, 640x480, 4 planes. */
    memset(file, 0, BUFFER);
    put16(file, 4);
    for (i = 0; i < 16; i++)
        put16(file + 2 + i * 2u, i * 0x111u);
    fill_planes(file + 34, 320, 4, 640, 480, 7);
    assert(decode(153634, 0, &image, NULL) == CODEC_OK);
    assert(image.format == FALCON_TT_MEDIUM && image.width == 640 && image.height == 480);
    for (y = 0; y < 480; y += 5)
        for (x = 0; x < 640; x++) {
            unsigned v = (pattern(x, y, 7) & 15u) * 17u;
            expect(&image, x, y, v, v, v);
        }
    falcon_free(&image);
    /* Another resolution word at that size isn't a TT picture. */
    put16(file, 5);
    assert(decode(153634, 0, &image, NULL) == CODEC_INVALID);

    /* TT high: monochrome, white paper. */
    memset(file, 0, BUFFER);
    put16(file, 6);
    put16(file + 2, 0x0fff);
    put16(file + 4, 0x0f00);
    fill_planes(file + 6, 160, 1, 1280, 960, 8);
    assert(decode(153606, 0, &image, NULL) == CODEC_OK);
    assert(image.format == FALCON_TT_HIGH && image.width == 1280 && image.height == 960);
    for (y = 0; y < 960; y += 11)
        for (x = 0; x < 1280; x++) {
            unsigned v = pattern(x, y, 8) & 1u ? 0u : 255u;
            expect(&image, x, y, v, v, v);
        }
    falcon_free(&image);
}

static void test_fuckpaint(void)
{
    static const unsigned sizes[3][3] = {
        { 65024, 320, 200 }, { 77824, 320, 240 }, { 308224, 640, 480 }
    };
    struct falcon_image image;
    unsigned i;

    for (i = 0; i < 3; i++) {
        memset(file, 0, BUFFER);
        falcon_palette(0);
        fill_planes(file + 1024, sizes[i][1], 8, sizes[i][1], sizes[i][2], 20 + i);
        assert(decode(sizes[i][0], 0, &image, NULL) == CODEC_OK);
        assert(image.format == FALCON_FUCKPAINT);
        assert(image.width == sizes[i][1] && image.height == sizes[i][2]);
        check_falcon_planes(&image, 20 + i);
        falcon_free(&image);
    }
}

static void dune_header(const char *id, unsigned width, unsigned height)
{
    memset(file, 0, BUFFER);
    memcpy(file, id, 4);
    put16(file + 4, width);
    put16(file + 6, height);
}

/* Pack plane-major DuneGraph data: runs of units of size bytes. Returns
   the packed length, from 1038. */
static size_t dune_pack(const uint8_t *bitmap, size_t length, unsigned method)
{
    unsigned size = 1u << (method - 1u), count_size = method == 1 ? 1u : 2u;
    unsigned limit = method == 1 ? 256u : 65536u;
    size_t groups = length / 16u, at = 1038, i = 0, units = length / size;
    uint8_t *planar = scratch;
    unsigned p, b;
    size_t g, n = 0;

    for (p = 0; p < 8; p++)
        for (g = 0; g < groups; g++)
            for (b = 0; b < 2; b++)
                planar[n++] = bitmap[g * 16u + p * 2u + b];
    while (i < units) {
        unsigned run = 1;
        while (i + run < units && run < limit &&
               memcmp(planar + (i + run) * size, planar + i * size, size) == 0)
            run++;
        if (count_size == 1)
            file[at] = (uint8_t)(run - 1u);
        else
            put16(file + at, run - 1u);
        memcpy(file + at + count_size, planar + i * size, size);
        at += count_size + size;
        i += run;
    }
    put32(file + 1034, (uint32_t)(at - 1034));
    return at;
}

static void test_dune(void)
{
    struct falcon_image image;
    uint8_t *bitmap = malloc(64000);
    unsigned method;
    size_t length;

    assert(bitmap != NULL);
    /* Uncompressed DG1. */
    dune_header("DGU\1", 320, 200);
    falcon_palette(8);
    fill_planes(file + 1032, 320, 8, 320, 200, 30);
    assert(decode(65032, 0, &image, NULL) == CODEC_OK);
    assert(image.format == FALCON_DUNE && image.width == 320 && image.height == 200);
    check_falcon_planes(&image, 30);
    falcon_free(&image);
    prefixes(65032, 997);
    /* Other sizes follow the header; lines are whole groups. */
    dune_header("DGU\1", 20, 3);
    falcon_palette(8);
    fill_planes(file + 1032, 32, 8, 20, 3, 31);
    assert(decode(1032 + 32 * 3, 0, &image, NULL) == CODEC_OK);
    check_falcon_planes(&image, 31);
    falcon_free(&image);
    assert(decode(1032 + 32 * 3 - 1, 0, &image, NULL) == CODEC_TRUNCATED);

    /* DC1 method 0. */
    dune_header("DGC", 320, 200);
    file[3] = 0;
    falcon_palette(10);
    fill_planes(file + 1034, 320, 8, 320, 200, 32);
    assert(decode(65034, 0, &image, NULL) == CODEC_OK);
    assert(image.format == FALCON_DUNE_PACKED);
    check_falcon_planes(&image, 32);
    falcon_free(&image);
    assert(decode(65033, 0, &image, NULL) == CODEC_TRUNCATED);

    /* Methods 1 to 3, with runs: a pattern that repeats in stretches. */
    memset(bitmap, 0, 64000);
    {
        unsigned x, y;
        for (y = 0; y < 200; y++)
            for (x = 0; x < 320; x++)
                plot(bitmap, 320, 8, x, y, (x / 40u + y / 25u * 8u) * 3u & 255u);
    }
    for (method = 1; method <= 3; method++) {
        unsigned x, y;
        dune_header("DGC", 320, 200);
        file[3] = (uint8_t)method;
        falcon_palette(10);
        length = dune_pack(bitmap, 64000, method);
        assert(decode(length, 0, &image, NULL) == CODEC_OK);
        for (y = 0; y < 200; y++)
            for (x = 0; x < 320; x++) {
                unsigned i = (x / 40u + y / 25u * 8u) * 3u & 255u;
                expect(&image, x, y, falcon_level(i * 5u + 3u), falcon_level(255u - i),
                       falcon_level(i * 3u + 1u));
            }
        falcon_free(&image);
        prefixes(length, 1);
        /* The packed size bounds the runs: bytes after it are ignored. */
        assert(decode(length + 7, 0, &image, NULL) == CODEC_OK);
        falcon_free(&image);
        put32(file + 1034, (uint32_t)(length - 1034 + 1));
        assert(decode(length, 0, &image, NULL) == CODEC_TRUNCATED);
        put32(file + 1034, 3);
        assert(decode(length, 0, &image, NULL) == CODEC_INVALID);
    }
    /* A stream that stops early leaves zeros, as DuneGraph writes them;
       a run past the end is clamped. */
    dune_header("DGC", 16, 1);
    file[3] = 1;
    falcon_palette(10);
    file[1038] = 3;    /* four 0xff: plane 0 and 1 of the only group */
    file[1039] = 0xff;
    put32(file + 1034, 6);
    assert(decode(1040, 0, &image, NULL) == CODEC_OK);
    expect(&image, 0, 0, falcon_level(3 * 5 + 3), falcon_level(255 - 3), falcon_level(3 * 3 + 1));
    falcon_free(&image);
    file[1038] = 200;
    assert(decode(1040, 0, &image, NULL) == CODEC_OK);
    expect(&image, 15, 0, falcon_level(255 * 5 + 3), falcon_level(0), falcon_level(255 * 3 + 1));
    falcon_free(&image);
    file[3] = 4;
    assert(decode(1040, 0, &image, NULL) == CODEC_INVALID);
    free(bitmap);
}

/* PackBits: repeats of three or more, literals otherwise, and a no-op
   command (128) at the start to check it is skipped. */
static size_t packbits(const uint8_t *in, size_t length, uint8_t *out)
{
    size_t i = 0, o = 0;

    out[o++] = 128;
    while (i < length) {
        size_t run = 1;
        while (i + run < length && run < 128 && in[i + run] == in[i])
            run++;
        if (run >= 3) {
            out[o++] = (uint8_t)(257u - run);
            out[o++] = in[i];
            i += run;
        } else {
            size_t start = i, n = 0;
            while (i < length && n < 128 &&
                   !(i + 2 < length && in[i] == in[i + 1] && in[i] == in[i + 2])) {
                i++;
                n++;
            }
            out[o++] = (uint8_t)(n - 1u);
            memcpy(out + o, in + start, n);
            o += n;
        }
    }
    return o;
}

/* A Prism Paint header. */
static void prism_header(unsigned colours, unsigned width, unsigned height,
                         unsigned bpp, unsigned compression)
{
    memset(file, 0, BUFFER);
    memcpy(file, "PNT", 4);
    put16(file + 4, 0x0100);
    put16(file + 6, colours);
    put16(file + 8, width);
    put16(file + 10, height);
    put16(file + 12, bpp);
    put16(file + 14, compression);
}

/* Build the uncompressed bitmap for a depth into scratch; return the
   stride. Pixel colours are pattern() values. */
static size_t prism_bitmap(unsigned width, unsigned height, unsigned bpp,
                           unsigned salt)
{
    size_t padded = ((size_t)width + 15u) & ~(size_t)15u, stride = padded * bpp / 8u;
    unsigned x, y;

    memset(scratch, 0, stride * height);
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++) {
            unsigned v = pattern(x, y, salt);
            uint8_t *line = scratch + (size_t)y * stride;
            if (bpp <= 8)
                plot(scratch, stride, bpp, x, y, v & ((1u << bpp) - 1u));
            else if (bpp == 16)
                put16(line + (size_t)x * 2u, v);
            else {
                line[x * 3u] = (uint8_t)v;
                line[x * 3u + 1u] = (uint8_t)(v >> 8);
                line[x * 3u + 2u] = (uint8_t)(v * 3u);
            }
        }
    return stride;
}

/* Reorder each line to plane order, as Prism Paint packs it. */
static void prism_plane_order(const uint8_t *bitmap, uint8_t *out, size_t stride,
                              unsigned height, unsigned bpp)
{
    size_t words = stride / 2u / bpp, k, o = 0;
    unsigned y, p;

    for (y = 0; y < height; y++)
        for (p = 0; p < bpp; p++)
            for (k = 0; k < words; k++) {
                out[o++] = bitmap[(size_t)y * stride + (k * bpp + p) * 2u];
                out[o++] = bitmap[(size_t)y * stride + (k * bpp + p) * 2u + 1u];
            }
}

static unsigned vdi_level(unsigned v)
{
    return v >= 1000u ? 255u : (v * 255u + 500u) / 1000u;
}

static void test_prism(void)
{
    static const unsigned depths[6] = { 1, 2, 4, 8, 16, 24 };
    static const uint8_t vdi16[16] = { 0, 15, 1, 2, 4, 6, 3, 5, 7, 8, 9, 10, 12, 14, 11, 13 };
    struct falcon_image image;
    uint8_t *ordered = malloc(BUFFER);
    unsigned d, compression;

    assert(ordered != NULL);
    for (d = 0; d < 6; d++)
        for (compression = 0; compression < 2; compression++) {
            unsigned bpp = depths[d], width = 37, height = 5, x, y, i;
            unsigned colours = bpp <= 8 ? 1u << bpp : 2u;
            size_t stride, at, length;
            uint8_t pens[256][3];

            prism_header(colours, width, height, bpp, compression);
            at = 128;
            memset(pens, 0, sizeof pens);
            for (i = 0; i < colours; i++) {
                unsigned pen = bpp > 8 ? 0 : i == 1 ? colours - 1u
                             : i < 16 ? vdi16[i] : i == 255 ? 15u : i;
                unsigned r = (i * 97u) % 1001u, g = 1000u - (i * 13u) % 1001u,
                         b = i == 3 ? 1015u : (i * 7u) % 1001u;
                put16(file + at, r);
                put16(file + at + 2, g);
                put16(file + at + 4, b);
                at += 6;
                if (pen < 256) {
                    pens[pen][0] = (uint8_t)vdi_level(r);
                    pens[pen][1] = (uint8_t)vdi_level(g);
                    pens[pen][2] = (uint8_t)vdi_level(b);
                }
            }
            stride = prism_bitmap(width, height, bpp, 40 + d);
            if (compression) {
                prism_plane_order(scratch, ordered, stride, height, bpp);
                length = at + packbits(ordered, stride * height, file + at);
            } else {
                memcpy(file + at, scratch, stride * height);
                length = at + stride * height;
            }
            put32(file + 16, (uint32_t)(length - at));
            assert(decode(length, 0, &image, NULL) == CODEC_OK);
            assert(image.format == FALCON_PRISM);
            assert(image.width == width && image.height == height);
            for (y = 0; y < height; y++)
                for (x = 0; x < width; x++) {
                    unsigned v = pattern(x, y, 40 + d);
                    if (bpp <= 8) {
                        const uint8_t *c = pens[v & ((1u << bpp) - 1u)];
                        expect(&image, x, y, c[0], c[1], c[2]);
                    } else if (bpp == 16) {
                        expect565(&image, x, y, v);
                    } else {
                        expect(&image, x, y, v & 255u, v >> 8, v * 3u & 255u);
                    }
                }
            falcon_free(&image);
            prefixes(length, 1);
            /* The size field is not trusted. */
            put32(file + 16, 0xffffffffu);
            assert(decode(length, 0, &image, NULL) == CODEC_OK);
            falcon_free(&image);
        }

    /* A literal run past the last line is clamped. */
    prism_header(0, 16, 1, 1, 1);
    file[128] = 5;
    memset(file + 129, 0xaa, 6);
    assert(decode(135, 0, &image, NULL) == CODEC_OK);
    /* No palette: pen 0 is white, pen 1 black. */
    expect(&image, 0, 0, 0, 0, 0);
    expect(&image, 1, 0, 255, 255, 255);
    falcon_free(&image);
    /* A repeat command with no value is truncated. */
    file[128] = 0xff;
    assert(decode(129, 0, &image, NULL) == CODEC_TRUNCATED);

    /* Header checks. */
    prism_header(0, 16, 1, 3, 0);
    assert(decode(128 + 6, 0, &image, NULL) == CODEC_INVALID);
    prism_header(0, 16, 1, 8, 2);
    assert(decode(128 + 16, 0, &image, NULL) == CODEC_INVALID);
    prism_header(0, 16, 1, 8, 0);
    put16(file + 4, 0x0200);
    assert(decode(128 + 16, 0, &image, NULL) == CODEC_INVALID);
    prism_header(0, 0, 1, 8, 0);
    assert(decode(128 + 16, 0, &image, NULL) == CODEC_INVALID);
    prism_header(0, 65535, 65535, 24, 0);
    assert(decode(128 + 16, 0, &image, NULL) == CODEC_TOO_LARGE);
    prism_header(300, 16, 1, 8, 0);
    assert(decode(128 + 16, 0, &image, NULL) == CODEC_TRUNCATED);
    free(ordered);
}

static void test_encode(void)
{
    struct falcon_image image;
    uint8_t *rgba = malloc(33u * 7u * 4u);
    size_t size;
    unsigned x, y;

    assert(rgba != NULL);
    for (y = 0; y < 7; y++)
        for (x = 0; x < 33; x++) {
            uint8_t *p = rgba + (y * 33u + x) * 4u;
            p[0] = (uint8_t)(x * 7u);
            p[1] = (uint8_t)(y * 31u);
            p[2] = (uint8_t)(x * y);
            p[3] = x == 0 ? 0 : x == 1 ? 128 : 255;
        }
    size = falcon_encode_size(33, 7);
    assert(size == 128u + 48u * 3u * 7u);
    assert(falcon_encode(rgba, 33, 7, file, size - 1) == CODEC_NO_MEMORY);
    assert(falcon_encode(rgba, 33, 7, file, size) == CODEC_OK);
    assert(memcmp(file, "PNT\0\1\0\0\0", 8) == 0);
    assert(decode(size, 0, &image, NULL) == CODEC_OK);
    assert(image.format == FALCON_PRISM && image.width == 33 && image.height == 7);
    for (y = 0; y < 7; y++)
        for (x = 0; x < 33; x++) {
            const uint8_t *p = rgba + (y * 33u + x) * 4u;
            if (x == 0)
                expect(&image, x, y, 255, 255, 255);
            else if (x == 1)
                expect(&image, x, y, (p[0] * 128u + 255u * 127u + 127u) / 255u,
                       (p[1] * 128u + 255u * 127u + 127u) / 255u,
                       (p[2] * 128u + 255u * 127u + 127u) / 255u);
            else
                expect(&image, x, y, p[0], p[1], p[2]);
        }
    falcon_free(&image);
    assert(falcon_encode_size(0, 7) == 0);
    assert(falcon_encode_size(4097, 4097) == 0);
    assert(falcon_encode_size(65535, 256) != 0);
    assert(falcon_encode(rgba, 0, 7, file, BUFFER) == CODEC_TOO_LARGE);
    free(rgba);
}

static void test_unknown(void)
{
    struct falcon_image image;
    unsigned count = 5;

    memset(file, 0x55, 64);
    assert(decode(64, 0, &image, &count) == CODEC_INVALID && count == 0);
    assert(image.rgba == NULL && image.format == FALCON_UNKNOWN);
    assert(falcon_decode(NULL, 10, 0, &image, NULL) == CODEC_TRUNCATED);
    assert(falcon_decode(file, 10, 0, NULL, NULL) == CODEC_INVALID);
    assert(falcon_identify(NULL, 10) == FALCON_UNKNOWN);
    memcpy(file, "DGU\2", 4);
    assert(falcon_identify(file, 64) == FALCON_UNKNOWN);
}

int main(void)
{
    file = malloc(BUFFER);
    scratch = malloc(BUFFER);
    assert(file != NULL && scratch != NULL);
    test_true_colour();
    test_rembrandt();
    test_tt();
    test_fuckpaint();
    test_dune();
    test_prism();
    test_encode();
    test_unknown();
    free(scratch);
    free(file);
    puts("falcon: ok");
    return 0;
}
