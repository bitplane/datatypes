#include "../formats/gd/decode.h"
#include "../formats/gd/encode.h"
#include "common/zlib.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct out { uint8_t *data; size_t length; };

static uint8_t file[1u << 20];

static void put8(struct out *o, unsigned v) { o->data[o->length++] = (uint8_t)v; }
static void put16(struct out *o, unsigned v) { put8(o, v >> 8); put8(o, v); }
static void put32(struct out *o, uint32_t v) { put16(o, v >> 16); put16(o, v & 0xffffu); }
static void set32(struct out *o, size_t at, uint32_t v)
{
    o->data[at] = (uint8_t)(v >> 24); o->data[at + 1] = (uint8_t)(v >> 16);
    o->data[at + 2] = (uint8_t)(v >> 8); o->data[at + 3] = (uint8_t)v;
}

static uint8_t gd7(unsigned a) { return (uint8_t)(255u - ((a << 1) + (a >> 6))); }

/* Palette entry i: r = i, g = 255 - i, b = i * 3, gd alpha = i & 127. */
static void put_palette(struct out *o, int extended)
{
    unsigned i;
    for (i = 0; i < 256; i++) {
        put8(o, i); put8(o, 255 - i); put8(o, i * 3);
        if (extended) put8(o, i & 127);
    }
}

static void expect_entry(const uint8_t *p, unsigned i, int extended)
{
    assert(p[0] == i && p[1] == (uint8_t)(255 - i) && p[2] == (uint8_t)(i * 3));
    assert(p[3] == (extended ? gd7(i & 127) : 255));
}

/* Truecolour pixel at x, y: gd alpha x * 9, r = x, g = y, b = x ^ y. */
static uint32_t tc(unsigned x, unsigned y)
{
    return ((uint32_t)((x * 9u) & 127u) << 24) | ((x & 255u) << 16) | ((y & 255u) << 8) |
           ((x ^ y) & 255u);
}

static void expect_tc(const struct gd_image *image)
{
    unsigned x, y;
    for (y = 0; y < image->height; y++)
        for (x = 0; x < image->width; x++) {
            const uint8_t *p = image->rgba + ((size_t)y * image->width + x) * 4u;
            uint32_t v = tc(x, y);
            assert(p[0] == (uint8_t)(v >> 16) && p[1] == (uint8_t)(v >> 8) &&
                   p[2] == (uint8_t)v && p[3] == gd7(v >> 24));
        }
}

static unsigned pal_index(unsigned x, unsigned y) { return (x * 7u + y * 13u) & 255u; }

static void expect_pal(const struct gd_image *image, int extended)
{
    unsigned x, y;
    for (y = 0; y < image->height; y++)
        for (x = 0; x < image->width; x++)
            expect_entry(image->rgba + ((size_t)y * image->width + x) * 4u,
                         pal_index(x, y), extended);
}

static void expect_truncated(const struct out *o)
{
    struct gd_image image;
    size_t n;
    for (n = 0; n < o->length; n++)
        assert(gd_decode(o->data, n, &image) == CODEC_TRUNCATED && image.rgba == NULL);
}

static enum codec_result decode(const struct out *o, struct gd_image *image)
{
    return gd_decode(o->data, o->length, image);
}

/* GD2 of the test pattern. Compressed chunks are deflated, and "extra" adds
   a column of empty chunks with a garbage index that must be skipped. */
static void make_gd2(struct out *o, unsigned version, unsigned format, unsigned width,
                     unsigned height, unsigned chunk, int extra)
{
    unsigned across = (width + chunk - 1) / chunk + (extra ? 1 : 0);
    unsigned down = (height + chunk - 1) / chunk, cx, cy, x, y;
    int truecolor = format >= 3, compressed = format == 2 || format == 4;
    size_t index, n = 0;
    uint8_t *raw = malloc((size_t)chunk * chunk * 4u), *packed;
    size_t packed_size = zlib_deflate_bound((size_t)chunk * chunk * 4u), written;

    packed = malloc(packed_size);
    assert(raw != NULL && packed != NULL);
    o->length = 0;
    put8(o, 'g'); put8(o, 'd'); put8(o, '2'); put8(o, 0);
    put16(o, version); put16(o, width); put16(o, height); put16(o, chunk);
    put16(o, format); put16(o, across); put16(o, down);
    index = o->length;
    if (compressed)
        for (n = 0; n < (size_t)across * down; n++) { put32(o, 0x7fffffffu); put32(o, 0x7fffffffu); }
    if (version == 2) {
        put8(o, truecolor);
        if (!truecolor) put16(o, 256);
        put32(o, 0xffffffffu);
    } else {
        put8(o, 0); put16(o, 0xffff);
    }
    if (!truecolor) put_palette(o, version == 2);
    for (cy = 0, n = 0; cy < down; cy++)
        for (cx = 0; cx < across; cx++, n++) {
            size_t len = 0;
            if (cx * chunk >= width) continue;
            for (y = cy * chunk; y < height && y < (cy + 1) * chunk; y++)
                for (x = cx * chunk; x < width && x < (cx + 1) * chunk; x++) {
                    if (truecolor) {
                        uint32_t v = tc(x, y);
                        raw[len++] = (uint8_t)(v >> 24); raw[len++] = (uint8_t)(v >> 16);
                        raw[len++] = (uint8_t)(v >> 8); raw[len++] = (uint8_t)v;
                    } else {
                        raw[len++] = (uint8_t)pal_index(x, y);
                    }
                }
            if (!compressed) {
                memcpy(o->data + o->length, raw, len);
                o->length += len;
                continue;
            }
            assert(zlib_deflate(raw, len, packed, packed_size, 9, &written) == CODEC_OK);
            set32(o, index + n * 8u, (uint32_t)o->length);
            set32(o, index + n * 8u + 4u, (uint32_t)written);
            memcpy(o->data + o->length, packed, written);
            o->length += written;
        }
    free(raw);
    free(packed);
}

static void test_gd1(void)
{
    struct out o = { file, 0 };
    struct gd_image image;
    const unsigned pixels[4] = { 0, 1, 2, 200 };
    unsigned i;

    put16(&o, 2); put16(&o, 2); put8(&o, 3); put16(&o, 1);
    put_palette(&o, 0);
    for (i = 0; i < 4; i++) put8(&o, pixels[i]);
    assert(decode(&o, &image) == CODEC_OK);
    assert(image.width == 2 && image.height == 2);
    expect_entry(image.rgba, 0, 0);
    assert(memcmp(image.rgba + 4, "\x01\xfe\x03\x00", 4) == 0);
    expect_entry(image.rgba + 8, 2, 0);
    /* Indexes past the colour count still use the stored entry. */
    expect_entry(image.rgba + 12, 200, 0);
    gd_free(&image);
    expect_truncated(&o);

    /* A transparent index at or past the colour count means none. */
    file[5] = 0; file[6] = 3;
    assert(decode(&o, &image) == CODEC_OK);
    expect_entry(image.rgba + 12, 200, 0);
    expect_entry(image.rgba + 4, 1, 0);
    gd_free(&image);
    file[5] = 0xff; file[6] = 0xff;
    assert(decode(&o, &image) == CODEC_OK);
    expect_entry(image.rgba + 4, 1, 0);
    gd_free(&image);

    file[1] = 0;
    assert(decode(&o, &image) == CODEC_INVALID && image.rgba == NULL);
}

static void test_gd2x_palette(void)
{
    struct out o = { file, 0 };
    struct gd_image image;
    unsigned x, y;

    put16(&o, 0xffff); put16(&o, 5); put16(&o, 3);
    put8(&o, 0); put16(&o, 256); put32(&o, 0xffffffffu);
    put_palette(&o, 1);
    for (y = 0; y < 3; y++)
        for (x = 0; x < 5; x++) put8(&o, pal_index(x, y));
    assert(decode(&o, &image) == CODEC_OK);
    expect_pal(&image, 1);
    gd_free(&image);
    expect_truncated(&o);

    /* A transparent index clears that entry's alpha. */
    set32(&o, 9, 7);
    assert(decode(&o, &image) == CODEC_OK);
    assert(image.rgba[4 * 1 + 3] == 0 && image.rgba[4 * 1] == 7);
    assert(image.rgba[3] == 255);
    gd_free(&image);
    set32(&o, 9, 0x80000000u);
    assert(decode(&o, &image) == CODEC_OK);
    expect_pal(&image, 1);
    gd_free(&image);

    file[8] = 1; file[7] = 1; /* colour count 257 */
    assert(decode(&o, &image) == CODEC_INVALID);
    file[7] = 0; file[8] = 0;
    file[6] = 1; /* truecolour flag without the truecolour signature */
    assert(decode(&o, &image) == CODEC_INVALID);
}

static void test_gd2x_truecolor(void)
{
    struct out o = { file, 0 };
    struct gd_image image;
    unsigned x, y;

    put16(&o, 0xfffe); put16(&o, 17); put16(&o, 4);
    put8(&o, 1); put32(&o, 0xffffffffu);
    for (y = 0; y < 4; y++)
        for (x = 0; x < 17; x++) put32(&o, tc(x, y));
    assert(decode(&o, &image) == CODEC_OK);
    assert(image.width == 17 && image.height == 4);
    expect_tc(&image);
    gd_free(&image);
    expect_truncated(&o);

    /* Top alpha bit ignored, as libgd masks it; the transparent colour is a
       whole pixel, alpha included. */
    set32(&o, 11, 0x80102030u);
    set32(&o, 15, 0x00405060u);
    set32(&o, 19, 0x7f405060u);
    set32(&o, 7, 0x00405060u);
    assert(decode(&o, &image) == CODEC_OK);
    assert(memcmp(image.rgba, "\x10\x20\x30\xff", 4) == 0);
    assert(memcmp(image.rgba + 4, "\x40\x50\x60\x00", 4) == 0);
    assert(memcmp(image.rgba + 8, "\x40\x50\x60\x00", 4) == 0);
    gd_free(&image);
    /* -1 means no transparent colour, even for a pixel of -1. */
    set32(&o, 7, 0xffffffffu);
    set32(&o, 11, 0xffffffffu);
    assert(decode(&o, &image) == CODEC_OK);
    assert(memcmp(image.rgba, "\xff\xff\xff\x00", 4) == 0);
    set32(&o, 11, 0x00ffffffu);
    gd_free(&image);
    assert(decode(&o, &image) == CODEC_OK);
    assert(memcmp(image.rgba, "\xff\xff\xff\xff", 4) == 0);
    gd_free(&image);

    file[6] = 0; /* palette flag with the truecolour signature */
    assert(decode(&o, &image) == CODEC_INVALID);
    file[6] = 1;
    file[2] = file[3] = file[4] = file[5] = 0xff;
    assert(decode(&o, &image) == CODEC_TOO_LARGE);
}

static void test_gd2(void)
{
    static const unsigned formats[4] = { 1, 2, 3, 4 };
    struct out o = { file, 0 };
    struct gd_image image;
    unsigned f, version;

    for (version = 1; version <= 2; version++)
        for (f = 0; f < 4; f++) {
            make_gd2(&o, version, formats[f], 130, 65, 64, 0);
            assert(decode(&o, &image) == CODEC_OK);
            assert(image.width == 130 && image.height == 65);
            if (formats[f] >= 3) expect_tc(&image); else expect_pal(&image, version == 2);
            gd_free(&image);
            expect_truncated(&o);

            make_gd2(&o, version, formats[f], 70, 3, 64, 1);
            assert(decode(&o, &image) == CODEC_OK);
            if (formats[f] >= 3) expect_tc(&image); else expect_pal(&image, version == 2);
            gd_free(&image);
        }

    make_gd2(&o, 2, 4, 64, 64, 64, 0);
    assert(decode(&o, &image) == CODEC_OK);
    gd_free(&image);
    make_gd2(&o, 2, 2, 1, 1, 4096, 0);
    assert(decode(&o, &image) == CODEC_OK);
    expect_pal(&image, 1);
    gd_free(&image);

    make_gd2(&o, 2, 2, 130, 65, 64, 0);
    file[5] = 3;
    assert(decode(&o, &image) == CODEC_INVALID && image.rgba == NULL);
    file[5] = 2;
    file[11] = 63;
    assert(decode(&o, &image) == CODEC_INVALID);
    file[10] = 0x10; file[11] = 1;
    assert(decode(&o, &image) == CODEC_INVALID);
    file[10] = 0; file[11] = 64;
    file[13] = 0;
    assert(decode(&o, &image) == CODEC_INVALID);
    file[13] = 5;
    assert(decode(&o, &image) == CODEC_INVALID);
    file[13] = 2;
    file[15] = 2; /* chunks don't cover the width */
    assert(decode(&o, &image) == CODEC_INVALID);
    file[15] = 3;
    file[17] = 1;
    assert(decode(&o, &image) == CODEC_INVALID);
    file[17] = 2;
    file[7] = 0;
    assert(decode(&o, &image) == CODEC_INVALID);
    file[7] = 130;
    file[18] = 0x80; /* negative chunk offset */
    assert(decode(&o, &image) == CODEC_INVALID && image.rgba == NULL);
    file[18] = 0;
    assert(decode(&o, &image) == CODEC_OK);
    gd_free(&image);

    /* A chunk that inflates to too little or too much, or isn't zlib. */
    file[18 + 11] += 1;
    assert(decode(&o, &image) == CODEC_INVALID);
    file[18 + 11] -= 1;
    file[18 + 8 + 3] -= 1; /* second chunk now starts inside the first */
    assert(decode(&o, &image) == CODEC_INVALID && image.rgba == NULL);
    make_gd2(&o, 2, 2, 64, 64, 64, 0);
    set32(&o, 22, 1);
    assert(decode(&o, &image) == CODEC_INVALID);
    {
        /* One chunk whose stream holds the wrong number of pixels. */
        uint8_t raw[64 * 64 + 1], packed[8192];
        size_t written, colors = 18 + 8 + 7 + 1024;
        memset(raw, 5, sizeof raw);
        make_gd2(&o, 2, 2, 64, 64, 64, 0);
        o.length = colors;
        assert(zlib_deflate(raw, sizeof raw, packed, sizeof packed, 9, &written) == CODEC_OK);
        set32(&o, 18, (uint32_t)o.length); set32(&o, 22, (uint32_t)written);
        memcpy(file + o.length, packed, written); o.length += written;
        assert(decode(&o, &image) == CODEC_INVALID);
        o.length = colors;
        assert(zlib_deflate(raw, sizeof raw - 2, packed, sizeof packed, 9, &written) == CODEC_OK);
        set32(&o, 22, (uint32_t)written);
        memcpy(file + o.length, packed, written); o.length += written;
        assert(decode(&o, &image) == CODEC_INVALID);
        o.length = colors;
        assert(zlib_deflate(raw, sizeof raw - 1, packed, sizeof packed, 9, &written) == CODEC_OK);
        set32(&o, 22, (uint32_t)written);
        memcpy(file + o.length, packed, written); o.length += written;
        assert(decode(&o, &image) == CODEC_OK);
        assert(memcmp(image.rgba + 4 * 4095, "\x05\xfa\x0f", 3) == 0);
        gd_free(&image);
    }

    /* A chunk index too large for the file. */
    o.length = 0;
    put8(&o, 'g'); put8(&o, 'd'); put8(&o, '2'); put8(&o, 0);
    put16(&o, 2); put16(&o, 4096); put16(&o, 4096); put16(&o, 64);
    put16(&o, 4); put16(&o, 65535); put16(&o, 65535);
    assert(decode(&o, &image) == CODEC_TRUNCATED);
    file[6] = 0;
    assert(decode(&o, &image) == CODEC_INVALID);
    file[6] = file[7] = file[8] = file[9] = 0xff;
    assert(decode(&o, &image) == CODEC_TOO_LARGE);
}

static void test_encode(void)
{
    uint8_t rgba[256 * 4], header[GD_HEADER_SIZE], out[256 * 4];
    struct out o = { file, 0 };
    struct gd_image image;
    unsigned a;

    assert(!gd_make_header(0, 1, header) && !gd_make_header(1, 0, header));
    assert(!gd_make_header(65536, 1, header) && !gd_make_header(1, 65536, header));
    assert(gd_make_header(256, 1, header));
    for (a = 0; a < 256; a++) {
        rgba[a * 4] = (uint8_t)a; rgba[a * 4 + 1] = (uint8_t)~a;
        rgba[a * 4 + 2] = (uint8_t)(a * 5); rgba[a * 4 + 3] = (uint8_t)a;
    }
    gd_encode_row(rgba, 256, out);
    memcpy(file, header, sizeof header);
    memcpy(file + sizeof header, out, sizeof out);
    o.length = sizeof header + sizeof out;
    assert(decode(&o, &image) == CODEC_OK);
    assert(image.width == 256 && image.height == 1);
    for (a = 0; a < 256; a++) {
        const uint8_t *p = image.rgba + a * 4;
        assert(p[0] == rgba[a * 4] && p[1] == rgba[a * 4 + 1] && p[2] == rgba[a * 4 + 2]);
        assert(p[3] + 1u >= a && p[3] <= a + 1u);
    }
    assert(image.rgba[3] == 0 && image.rgba[255 * 4 + 3] == 255);
    gd_free(&image);
    /* Every alpha the decoder produces survives a save. */
    for (a = 0; a < 128; a++) rgba[a * 4 + 3] = gd7(a);
    gd_encode_row(rgba, 128, out);
    for (a = 0; a < 128; a++) assert(out[a * 4] == a);
}

int main(void)
{
    struct gd_image image;

    assert(gd_decode(NULL, 0, &image) == CODEC_INVALID);
    assert(gd_decode(file, 0, NULL) == CODEC_INVALID);
    test_gd1();
    test_gd2x_palette();
    test_gd2x_truecolor();
    test_gd2();
    test_encode();
    puts("gd codec tests passed");
    return 0;
}
