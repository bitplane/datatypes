#include "../formats/pvr/decode.h"
#include "../formats/pvr/encode.h"
#include "../formats/pvr/etc.h"
#include "../formats/pvr/pvrtc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t data[1 << 20];

struct v3 {
    uint32_t flags;
    const char *names;          /* NULL: format holds a compressed format */
    uint8_t bits[4];
    uint32_t format, space, type;
    uint32_t width, height, depth, surfaces, faces, levels;
    const uint8_t *meta;
    uint32_t meta_size;
};

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* Write a version 3 header and metadata to data; returns the data offset. */
static size_t v3(struct v3 h)
{
    memset(data, 0, 52);
    memcpy(data, "PVR\3", 4);
    put32(data + 4, h.flags);
    if (h.names) {
        memcpy(data + 8, h.names, strlen(h.names));
        memcpy(data + 12, h.bits, 4);
    } else {
        put32(data + 8, h.format);
    }
    put32(data + 16, h.space);
    put32(data + 20, h.type);
    put32(data + 24, h.height);
    put32(data + 28, h.width);
    put32(data + 32, h.depth);
    put32(data + 36, h.surfaces);
    put32(data + 40, h.faces);
    put32(data + 44, h.levels);
    put32(data + 48, h.meta_size);
    if (h.meta_size)
        memcpy(data + 52, h.meta, h.meta_size);
    return 52 + h.meta_size;
}

/* Write a version 2 header; levels counts mip levels below the top. */
static size_t v2(uint32_t width, uint32_t height, uint32_t levels, uint32_t flags,
                 uint32_t amask, uint32_t surfaces)
{
    memset(data, 0, 52);
    put32(data, 52);
    put32(data + 4, height);
    put32(data + 8, width);
    put32(data + 12, levels);
    put32(data + 16, flags);
    put32(data + 40, amask);
    memcpy(data + 44, "PVR!", 4);
    put32(data + 48, surfaces);
    return 52;
}

static void pixel(const struct pvr_image *im, unsigned x, unsigned y,
                  unsigned r, unsigned g, unsigned b, unsigned a)
{
    const uint8_t *p = im->rgba + (y * im->width + x) * 4u;
    if (p[0] != r || p[1] != g || p[2] != b || p[3] != a) {
        fprintf(stderr, "pixel %u,%u is %u %u %u %u, expected %u %u %u %u\n",
                x, y, p[0], p[1], p[2], p[3], r, g, b, a);
        assert(0);
    }
}

static void block_pixel(const uint8_t *out, unsigned x, unsigned y,
                        unsigned r, unsigned g, unsigned b, unsigned a)
{
    struct pvr_image im;
    im.width = 4;
    im.rgba = (uint8_t *)out;
    pixel(&im, x, y, r, g, b, a);
}

static enum codec_result decode(size_t length, unsigned long index, struct pvr_image *im)
{
    return pvr_decode(data, length, index, im);
}

static unsigned long count(size_t length)
{
    unsigned long n = 99;
    pvr_count(data, length, &n);
    return n;
}

static void test_v3_bytes(void)
{
    struct v3 h = { 0 };
    struct pvr_image im;
    size_t o;

    h.names = "rgba";
    memcpy(h.bits, "\10\10\10\10", 4);
    h.width = 2;
    h.height = 1;
    o = v3(h);
    memcpy(data + o, "\1\2\3\4\5\6\7\0", 8);
    assert(count(o + 8) == 1);
    assert(decode(o + 8, 0, &im) == CODEC_OK);
    assert(im.width == 2 && im.height == 1);
    pixel(&im, 0, 0, 1, 2, 3, 4);
    /* Declared alpha is kept even at zero. */
    pixel(&im, 1, 0, 5, 6, 7, 0);
    pvr_free(&im);

    /* Channels in any order, with unused ones */
    h.names = "bgrx";
    o = v3(h);
    memcpy(data + o, "\1\2\3\4\5\6\7\0", 8);
    assert(decode(o + 8, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 3, 2, 1, 255);
    pvr_free(&im);

    h.names = "rgb";
    memcpy(h.bits, "\10\10\10\0", 4);
    o = v3(h);
    memcpy(data + o, "\1\2\3\4\5\6", 6);
    assert(decode(o + 6, 0, &im) == CODEC_OK);
    pixel(&im, 1, 0, 4, 5, 6, 255);
    pvr_free(&im);

    /* Luminance, luminance with alpha, alpha alone and intensity */
    h.names = "la";
    memcpy(h.bits, "\10\10\0\0", 4);
    o = v3(h);
    memcpy(data + o, "\100\200\1\2", 4);
    assert(decode(o + 4, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 64, 64, 64, 128);
    pvr_free(&im);
    h.names = "a";
    memcpy(h.bits, "\10\0\0\0", 4);
    o = v3(h);
    memcpy(data + o, "\100\200", 2);
    assert(decode(o + 2, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 0, 0, 0, 64);
    pvr_free(&im);
    h.names = "i";
    o = v3(h);
    assert(decode(o + 2, 0, &im) == CODEC_OK);
    pixel(&im, 1, 0, 128, 128, 128, 128);
    pvr_free(&im);
    h.names = "l";
    o = v3(h);
    assert(decode(o + 2, 0, &im) == CODEC_OK);
    pixel(&im, 1, 0, 128, 128, 128, 255);
    pvr_free(&im);

    /* 16-bit channels, little-endian, rounded */
    h.names = "rg";
    memcpy(h.bits, "\20\20\0\0", 4);
    h.type = 4;
    h.width = 1;
    o = v3(h);
    memcpy(data + o, "\377\377\200\200", 4);
    assert(decode(o + 4, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 255, 128, 0, 255);
    pvr_free(&im);
    /* Signed and float channels aren't supported. */
    h.type = 5;
    v3(h);
    assert(decode(o + 4, 0, &im) == CODEC_INVALID);
    h.type = 12;
    v3(h);
    assert(decode(o + 4, 0, &im) == CODEC_INVALID);
    /* Unknown channel names and depths */
    h.type = 0;
    h.names = "dg";
    v3(h);
    assert(decode(o + 4, 0, &im) == CODEC_INVALID);
    h.names = "rg";
    memcpy(h.bits, "\20\41\0\0", 4);
    v3(h);
    assert(decode(o + 4, 0, &im) == CODEC_INVALID);
    memcpy(h.bits, "\4\4\0\0", 4);
    h.names = "rgb";
    v3(h);
    assert(decode(o + 4, 0, &im) == CODEC_INVALID);
}

static void test_v3_packed(void)
{
    struct v3 h = { 0 };
    struct pvr_image im;
    size_t o;

    /* Packed channels fill a little-endian word from the top. */
    h.names = "rgba";
    memcpy(h.bits, "\4\4\4\4", 4);
    h.width = 1;
    h.height = 1;
    o = v3(h);
    put32(data + o, 0x1f80);
    assert(decode(o + 2, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 17, 255, 136, 0);
    pvr_free(&im);

    h.names = "rgb";
    memcpy(h.bits, "\5\6\5\0", 4);
    o = v3(h);
    put32(data + o, 0xf81fu);
    assert(decode(o + 2, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 255, 0, 255, 255);
    pvr_free(&im);
    put32(data + o, 0x0410u);
    assert(decode(o + 2, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 0, 130, 132, 255);
    pvr_free(&im);

    h.names = "rgba";
    memcpy(h.bits, "\5\5\5\1", 4);
    o = v3(h);
    put32(data + o, 0x07c1u);
    assert(decode(o + 2, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 0, 255, 0, 255);
    pvr_free(&im);

    /* A 32-bit word: 10-bit channels and 2-bit alpha */
    h.names = "argb";
    memcpy(h.bits, "\2\12\12\12", 4);
    o = v3(h);
    put32(data + o, 0xbff00000u | 0x3ffu);
    assert(decode(o + 4, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 255, 0, 255, 170);
    pvr_free(&im);
}

static void test_v3_structure(void)
{
    static const uint8_t flip_meta[] = {
        'P', 'V', 'R', 3, 3, 0, 0, 0, 3, 0, 0, 0, 1, 1, 0,
        /* Padding in a foreign block, then a malformed one */
        'A', 'B', 'C', 'D', 5, 0, 0, 0, 1, 0, 0, 0, 9
    };
    static const uint8_t bad_meta[] = {
        'P', 'V', 'R', 3, 3, 0, 0, 0, 200, 0, 0, 0, 1, 1, 0, 0
    };
    static const uint8_t signed_meta[] = {
        'P', 'V', 'R', 3, 6, 0, 0, 0, 4, 0, 0, 0, 0, 1, 0, 0
    };
    struct v3 h = { 0 };
    struct pvr_image im;
    size_t o, i;

    h.names = "rgba";
    memcpy(h.bits, "\10\10\10\10", 4);
    h.width = 2;
    h.height = 2;
    h.meta = flip_meta;
    h.meta_size = sizeof flip_meta;
    o = v3(h);
    for (i = 0; i < 16; i++)
        data[o + i] = (uint8_t)i;
    assert(decode(o + 16, 0, &im) == CODEC_OK);
    /* Stored bottom up and right to left */
    pixel(&im, 0, 0, 12, 13, 14, 15);
    pixel(&im, 1, 1, 0, 1, 2, 3);
    pvr_free(&im);

    /* Metadata that overruns its block is ignored. */
    h.meta = bad_meta;
    h.meta_size = sizeof bad_meta;
    o = v3(h);
    for (i = 0; i < 16; i++)
        data[o + i] = (uint8_t)i;
    assert(decode(o + 16, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 0, 1, 2, 3);
    pvr_free(&im);
    /* Metadata beyond the file is truncation. */
    assert(decode(52 + 10, 0, &im) == CODEC_TRUNCATED);
    /* Per-channel types override the header. */
    h.meta = signed_meta;
    h.meta_size = sizeof signed_meta;
    o = v3(h);
    assert(decode(o + 16, 0, &im) == CODEC_INVALID);

    /* Premultiplied alpha is divided out. */
    h.meta_size = 0;
    h.flags = 2;
    h.width = 1;
    h.height = 1;
    o = v3(h);
    memcpy(data + o, "\100\40\0\200", 4);
    assert(decode(o + 4, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 128, 64, 0, 128);
    pvr_free(&im);
}

/* Two levels of 2 surfaces x 3 faces x a volume of depth 2: 24 + 12 images. */
static void test_v3_order(void)
{
    struct v3 h = { 0 };
    struct pvr_image im;
    size_t o, top = 4 * 2 * 4, small = 2 * 1 * 4, i, n;

    h.names = "rgba";
    memcpy(h.bits, "\10\10\10\10", 4);
    h.width = 4;
    h.height = 2;
    h.depth = 2;
    h.surfaces = 2;
    h.faces = 3;
    h.levels = 2;
    o = v3(h);
    n = 12 * top + 6 * small;
    for (i = 0; i < n; i++)
        data[o + i] = (uint8_t)(i < 12 * top ? i / top : 100 + (i - 12 * top) / small);
    assert(count(o + n) == 18);
    assert(decode(o + n, 0, &im) == CODEC_OK);
    assert(im.width == 4 && im.height == 2);
    pixel(&im, 3, 1, 0, 0, 0, 0);
    pvr_free(&im);
    assert(decode(o + n, 11, &im) == CODEC_OK);
    pixel(&im, 0, 0, 11, 11, 11, 11);
    pvr_free(&im);
    /* The second level has depth 1. */
    assert(decode(o + n, 12, &im) == CODEC_OK);
    assert(im.width == 2 && im.height == 1);
    pixel(&im, 1, 0, 100, 100, 100, 100);
    pvr_free(&im);
    assert(decode(o + n, 17, &im) == CODEC_OK);
    pixel(&im, 1, 0, 105, 105, 105, 105);
    pvr_free(&im);
    assert(decode(o + n, 18, &im) == CODEC_INVALID);
    assert(decode(o + n, (unsigned long)-1, &im) == CODEC_INVALID);
    /* Only whole images count; missing ones are truncated. */
    assert(count(o + n - 1) == 17);
    assert(decode(o + n - 1, 17, &im) == CODEC_TRUNCATED);
    assert(count(o + 12 * top - 1) == 11);
    assert(decode(o + 12 * top - 1, 11, &im) == CODEC_TRUNCATED);
    assert(decode(o + 12 * top - 1, 12, &im) == CODEC_TRUNCATED);
    assert(count(o) == 0);
    assert(decode(o, 0, &im) == CODEC_TRUNCATED);

    /* Too many levels, or sides too large */
    h.levels = 4;
    v3(h);
    assert(decode(o + n, 0, &im) == CODEC_INVALID);
    h.levels = 3;
    v3(h);
    assert(count(o + n) == 18);
    h.levels = 1;
    h.width = 65536;
    v3(h);
    assert(decode(o + n, 0, &im) == CODEC_TOO_LARGE);
    h.width = 0;
    v3(h);
    assert(decode(o + n, 0, &im) == CODEC_INVALID);
    h.width = 4096;
    h.height = 8192;
    h.depth = 1;
    v3(h);
    assert(decode(sizeof data, 0, &im) == CODEC_TOO_LARGE);
    h.height = 2;
    h.surfaces = 70000;
    v3(h);
    assert(decode(o + n, 0, &im) == CODEC_TOO_LARGE);
    /* Surfaces beyond the file cost no memory to count. */
    h.surfaces = 65535;
    h.faces = 65535;
    h.depth = 65535;
    h.width = 65535;
    h.height = 65535;
    v3(h);
    assert(count(sizeof data) == 0);
    assert(decode(sizeof data, 0, &im) == CODEC_TOO_LARGE);
}

static void test_v2(void)
{
    struct pvr_image im;
    size_t o, i;

    /* OpenGL RGBA 4444, flipped */
    o = v2(1, 2, 0, 0x10010, 0xf, 0);
    put32(data + o, 0xf00fu);
    put32(data + o + 2, 0x0f0fu);
    assert(count(o + 4) == 1);
    assert(decode(o + 4, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 0, 255, 0, 255);
    pixel(&im, 0, 1, 255, 0, 0, 255);
    pvr_free(&im);

    /* 5551, 565, 8888, 888, BGRA, I8, AI88, A8 */
    o = v2(1, 1, 0, 0x11, 1, 0);
    put32(data + o, 0x003fu);
    assert(decode(o + 2, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 0, 0, 255, 255);
    pvr_free(&im);
    o = v2(1, 1, 0, 0x13, 0, 0);
    put32(data + o, 0xf800u);
    assert(decode(o + 2, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 255, 0, 0, 255);
    pvr_free(&im);
    o = v2(1, 1, 0, 0x12, 0xff, 0);
    memcpy(data + o, "\1\2\3\4", 4);
    assert(decode(o + 4, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 1, 2, 3, 4);
    pvr_free(&im);
    o = v2(1, 1, 0, 0x15, 0, 0);
    assert(decode(o + 3, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 1, 2, 3, 255);
    pvr_free(&im);
    o = v2(1, 1, 0, 0x1a, 0, 0);
    assert(decode(o + 4, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 3, 2, 1, 4);
    pvr_free(&im);
    o = v2(1, 1, 0, 0x16, 0, 0);
    assert(decode(o + 1, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 1, 1, 1, 255);
    pvr_free(&im);
    o = v2(1, 1, 0, 0x17, 0, 0);
    assert(decode(o + 2, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 1, 1, 1, 2);
    pvr_free(&im);
    o = v2(1, 1, 0, 0x1b, 0, 0);
    assert(decode(o + 1, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 0, 0, 0, 1);
    pvr_free(&im);
    /* SDK ARGB 8888: a little-endian word, alpha on top */
    o = v2(1, 1, 0, 0x05, 0, 0);
    assert(decode(o + 4, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 3, 2, 1, 4);
    pvr_free(&im);

    /* Twiddled 4x2: Morton order, y in the low bit */
    o = v2(4, 2, 0, 0x216, 0, 0);
    for (i = 0; i < 8; i++)
        data[o + i] = (uint8_t)i;
    assert(decode(o + 8, 0, &im) == CODEC_OK);
    pixel(&im, 0, 1, 1, 1, 1, 255);
    pixel(&im, 1, 0, 2, 2, 2, 255);
    pixel(&im, 1, 1, 3, 3, 3, 255);
    pixel(&im, 2, 0, 4, 4, 4, 255);
    pixel(&im, 3, 1, 7, 7, 7, 255);
    pvr_free(&im);
    o = v2(3, 2, 0, 0x216, 0, 0);
    assert(decode(o + 6, 0, &im) == CODEC_INVALID);

    /* Cube faces each hold a mip chain: 4x2, 2x1, 1x1 */
    o = v2(4, 2, 2, 0x1016, 0, 0);
    for (i = 0; i < 6 * 11; i++)
        data[o + i] = (uint8_t)(i / 11 * 10 + (i % 11 < 8 ? 0 : i % 11 < 10 ? 1 : 2));
    assert(count(o + 66) == 18);
    assert(decode(o + 66, 4, &im) == CODEC_OK);
    assert(im.width == 2 && im.height == 1);
    pixel(&im, 1, 0, 11, 11, 11, 255);
    pvr_free(&im);
    assert(decode(o + 66, 17, &im) == CODEC_OK);
    assert(im.width == 1);
    pixel(&im, 0, 0, 52, 52, 52, 255);
    pvr_free(&im);
    assert(decode(o + 66, 18, &im) == CODEC_INVALID);
    assert(count(o + 65) == 17);
    assert(decode(o + 65, 17, &im) == CODEC_TRUNCATED);
    assert(count(o + 30) == 7);
    assert(decode(o + 30, 7, &im) == CODEC_TRUNCATED);
    assert(decode(o + 30, 8, &im) == CODEC_TRUNCATED);
    assert(decode(o + 30, 6, &im) == CODEC_OK);
    pvr_free(&im);
    /* The surface count wins over the cube flag. */
    v2(4, 2, 2, 0x1016, 0, 2);
    assert(count(o + 66) == 6);
    /* Too many levels, volumes and unknown types */
    v2(4, 2, 3, 0x16, 0, 0);
    assert(decode(o + 66, 0, &im) == CODEC_INVALID);
    v2(4, 2, 0, 0x4016, 0, 0);
    assert(decode(o + 66, 0, &im) == CODEC_INVALID);
    v2(4, 2, 0, 0x1c, 0, 0);
    assert(decode(o + 66, 0, &im) == CODEC_INVALID);
    /* A header size without the tag */
    v2(4, 2, 0, 0x16, 0, 0);
    data[47] = '?';
    assert(decode(o + 66, 0, &im) == CODEC_INVALID);
}

static void test_etc(void)
{
    /* Individual mode: 4-bit bases, table 0 then 7, pixel indices vary */
    static const uint8_t individual[8] = { 0x8f, 0x00, 0x0f, 0x1c, 0x00, 0x01, 0x80, 0x00 };
    /* Differential mode, flipped: base 16 + 3 in red, table 1 */
    static const uint8_t differential[8] = { 0x83, 0x00, 0x00, 0x27, 0xff, 0xff, 0x00, 0x00 };
    /* T mode: red 31 + 3 overflows. Colours 15,0,0 and 4,11,13 at
       distance 5 (32); (0,1) has index 1, (1,0) 2 and (2,0) 3. */
    static const uint8_t t_mode[8] = { 0xfb, 0x00, 0x4b, 0xdb, 0x01, 0x10, 0x01, 0x02 };
    /* H mode: green 0 - 4 overflows. Colours 0,0,0 and 15,15,15 at
       distance 4 (23); (1,1) has index 3. */
    static const uint8_t h_mode[8] = { 0x00, 0x04, 0x7f, 0xfe, 0x00, 0x20, 0x00, 0x20 };
    /* Planar mode: blue 31 + 1 overflows. Origin 63,1,26, horizontal 0,0,0
       and vertical 0,127,63 in 6, 7 and 6 bits. */
    static const uint8_t planar[8] = { 0x7e, 0x02, 0xf9, 0x02, 0x00, 0x00, 0x1f, 0xff };
    static const uint8_t eac[8] = { 0x80, 0x2d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07 };
    uint8_t out[64];

    etc2_rgb_block(individual, out, 0);
    /* Left: base 136, 0, 0 and table 0; right: 255, 0, 255 and table 7.
       (0,0) has index 2, -small; (3,3) index 1, +large; others 0, +small */
    block_pixel(out, 0, 0, 134, 0, 0, 255);
    block_pixel(out, 0, 1, 138, 2, 2, 255);
    block_pixel(out, 3, 3, 255, 183, 255, 255);
    etc2_rgb_block(individual, out, 1);
    block_pixel(out, 0, 0, 0, 0, 0, 0);

    etc2_rgb_block(differential, out, 0);
    /* Top: 132, 0, 0; bottom: 156, 0, 0. All msb set, lsb clear: -small */
    block_pixel(out, 3, 0, 127, 0, 0, 255);
    block_pixel(out, 0, 2, 151, 0, 0, 255);
    /* ETC2 RGB A1 with the opaque bit set decodes the same. */
    etc2_rgb_block(differential, out, 1);
    block_pixel(out, 0, 0, 127, 0, 0, 255);
    {
        uint8_t clear[8];
        memcpy(clear, differential, 8);
        /* With it clear, index 2 is transparent and index 0 unmodified. */
        clear[3] &= (uint8_t)~2u;
        etc2_rgb_block(clear, out, 1);
        block_pixel(out, 0, 0, 0, 0, 0, 0);
        clear[4] = clear[5] = clear[6] = clear[7] = 0;
        etc2_rgb_block(clear, out, 1);
        block_pixel(out, 0, 0, 132, 0, 0, 255);
        clear[6] = clear[7] = 0xff;
        clear[4] = clear[5] = 0;
        etc2_rgb_block(clear, out, 1);
        block_pixel(out, 0, 3, 173, 17, 17, 255);
    }

    etc2_rgb_block(t_mode, out, 0);
    block_pixel(out, 0, 0, 255, 0, 0, 255);
    block_pixel(out, 0, 1, 100, 219, 253, 255);
    block_pixel(out, 1, 0, 68, 187, 221, 255);
    block_pixel(out, 2, 0, 36, 155, 189, 255);
    etc2_rgb_block(t_mode, out, 1);
    block_pixel(out, 1, 0, 68, 187, 221, 255);
    {
        uint8_t clear[8];
        memcpy(clear, t_mode, 8);
        clear[3] &= (uint8_t)~2u;
        etc2_rgb_block(clear, out, 1);
        block_pixel(out, 1, 0, 0, 0, 0, 0);
        block_pixel(out, 2, 0, 36, 155, 189, 255);
    }

    etc2_rgb_block(h_mode, out, 0);
    block_pixel(out, 0, 0, 23, 23, 23, 255);
    block_pixel(out, 1, 1, 232, 232, 232, 255);
    /* The order of the colours sets the distance's low bit. */
    {
        uint8_t swap[8] = { 0x7f, 0xfb, 0x80, 0x02, 0x00, 0x20, 0x00, 0x22 };
        etc2_rgb_block(swap, out, 0);
        block_pixel(out, 0, 0, 255, 255, 255, 255);
        block_pixel(out, 0, 1, 249, 249, 249, 255);
        block_pixel(out, 1, 1, 0, 0, 0, 255);
    }

    etc2_rgb_block(planar, out, 0);
    block_pixel(out, 0, 0, 255, 2, 105, 255);
    block_pixel(out, 1, 0, 191, 2, 79, 255);
    block_pixel(out, 0, 3, 64, 192, 218, 255);
    block_pixel(out, 3, 3, 0, 190, 139, 255);

    memset(out, 9, sizeof out);
    eac8_block(eac, out, 3);
    /* Base 128, multiplier 2, table 13: last pixel's index 7 is +9 */
    block_pixel(out, 0, 0, 9, 9, 9, 126);
    block_pixel(out, 3, 3, 9, 9, 9, 146);
    /* 11 bits: base * 8 + 4 and modifiers times 8 */
    eac11_block(eac, out, 0);
    block_pixel(out, 0, 0, 126, 9, 9, 126);
    block_pixel(out, 3, 3, 146, 9, 9, 146);
    /* Multiplier 0 adds the modifier unscaled. */
    {
        uint8_t flat[8] = { 0x80, 0x0d, 0, 0, 0, 0, 0, 0x07 };
        eac8_block(flat, out, 3);
        block_pixel(out, 3, 3, 146, 9, 9, 128);
        eac11_block(flat, out, 0);
        block_pixel(out, 0, 0, 128, 9, 9, 128);
        block_pixel(out, 3, 3, 129, 9, 9, 128);
    }
}

static void test_etc_files(void)
{
    static const uint8_t block[8] = { 0x83, 0x00, 0x00, 0x27, 0xff, 0xff, 0x00, 0x00 };
    struct v3 h = { 0 };
    struct pvr_image im;
    size_t o;

    h.format = 6;
    h.width = 5;
    h.height = 3;
    o = v3(h);
    memcpy(data + o, block, 8);
    memcpy(data + o + 8, block, 8);
    assert(decode(o + 16, 0, &im) == CODEC_OK);
    assert(im.width == 5 && im.height == 3);
    pixel(&im, 4, 0, 127, 0, 0, 255);
    pixel(&im, 4, 2, 151, 0, 0, 255);
    pvr_free(&im);
    assert(decode(o + 15, 0, &im) == CODEC_TRUNCATED);

    /* ETC2 RGBA stores EAC alpha first. */
    h.format = 23;
    h.width = 4;
    h.height = 4;
    o = v3(h);
    memcpy(data + o, "\x80\x2d\0\0\0\0\0\x07", 8);
    memcpy(data + o + 8, block, 8);
    assert(decode(o + 16, 0, &im) == CODEC_OK);
    pixel(&im, 3, 3, 151, 0, 0, 146);
    pvr_free(&im);
    /* EAC RG11: the second half, read as EAC, is 131 at (3,3). */
    h.format = 26;
    o = v3(h);
    assert(decode(o + 16, 0, &im) == CODEC_OK);
    pixel(&im, 3, 3, 146, 131, 0, 255);
    pvr_free(&im);
    /* EAC R11 is gray; signed R11 isn't supported. */
    h.format = 25;
    o = v3(h);
    assert(decode(o + 8, 0, &im) == CODEC_OK);
    pixel(&im, 3, 3, 146, 146, 146, 255);
    pvr_free(&im);
    h.type = 1;
    v3(h);
    assert(decode(o + 8, 0, &im) == CODEC_INVALID);
    /* ASTC, PVRTC-II and HDR formats aren't supported. */
    h.type = 0;
    h.format = 27;
    v3(h);
    assert(decode(o + 16, 0, &im) == CODEC_INVALID);
    h.format = 5;
    v3(h);
    assert(decode(o + 16, 0, &im) == CODEC_INVALID);
    h.format = 14;
    v3(h);
    assert(decode(o + 16, 0, &im) == CODEC_INVALID);
    h.format = 19;
    v3(h);
    assert(decode(o + 16, 0, &im) == CODEC_INVALID);
}

static void test_bc(void)
{
    /* DXT1: colour 0 white, colour 1 black, three-colour block, index 3 */
    static const uint8_t dxt1[8] = { 0, 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    /* DXT2: premultiplied; explicit alpha 8 of 15 over white */
    static const uint8_t dxt2[16] = {
        0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88,
        0xff, 0xff, 0xff, 0xff, 0, 0, 0, 0
    };
    struct v3 h = { 0 };
    struct pvr_image im;
    size_t o;

    h.format = 7;
    h.width = 4;
    h.height = 4;
    o = v3(h);
    memcpy(data + o, dxt1, 8);
    assert(decode(o + 8, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 0, 0, 0, 0);
    pvr_free(&im);
    h.format = 8;
    o = v3(h);
    memcpy(data + o, dxt2, 16);
    assert(decode(o + 16, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 255, 255, 255, 136);
    pvr_free(&im);
    h.format = 9;
    v3(h);
    assert(decode(o + 16, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 255, 255, 255, 136);
    pvr_free(&im);

    /* Version 2 DXT1 has alpha only when declared. */
    o = v2(4, 4, 0, 0x20, 0, 0);
    memcpy(data + o, dxt1, 8);
    assert(decode(o + 8, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 0, 0, 0, 255);
    pvr_free(&im);
    v2(4, 4, 0, 0x8020, 0, 0);
    assert(decode(o + 8, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 0, 0, 0, 0);
    pvr_free(&im);
}

/* PVRTC block: modulation word, then colour B in the top half and colour A
   in the bottom, with the mode in bit 0. */
static void pvrtc_block(uint8_t *p, uint32_t modulation, uint32_t colours)
{
    put32(p, modulation);
    put32(p + 4, colours);
}

static void test_pvrtc(void)
{
    struct v3 h = { 0 };
    struct pvr_image im;
    size_t o, i;

    assert(pvrtc_size(1, 1, 0) == 32);
    assert(pvrtc_size(16, 8, 1) == 32);
    assert(pvrtc_size(32, 8, 1) == 64);
    assert(pvrtc_size(64, 4, 0) == 256);

    /* 4bpp, every block black A and white B: weights 0, 3, 5 and 8 */
    h.format = 2;
    h.width = 8;
    h.height = 8;
    o = v3(h);
    for (i = 0; i < 4; i++)
        pvrtc_block(data + o + i * 8, 0xe4e4e4e4u, 0xffff8000u);
    assert(decode(o + 32, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 0, 0, 0, 255);
    pixel(&im, 1, 0, 95, 95, 95, 255);
    pixel(&im, 2, 0, 159, 159, 159, 255);
    pixel(&im, 3, 0, 255, 255, 255, 255);
    pvr_free(&im);
    /* Punch-through: weight 4 and transparent at value 2 */
    for (i = 0; i < 4; i++)
        pvrtc_block(data + o + i * 8, 0xe4e4e4e4u, 0xffff8001u);
    h.format = 3;
    v3(h);
    assert(decode(o + 32, 0, &im) == CODEC_OK);
    pixel(&im, 1, 0, 127, 127, 127, 255);
    pixel(&im, 2, 0, 127, 127, 127, 0);
    pvr_free(&im);
    /* The RGB formats are opaque. */
    h.format = 2;
    v3(h);
    assert(decode(o + 32, 0, &im) == CODEC_OK);
    pixel(&im, 2, 0, 127, 127, 127, 255);
    pvr_free(&im);

    /* Colours blend between block centres, wrapping at the edges: block 0
       (top left) is red and the others blue. */
    for (i = 0; i < 4; i++)
        pvrtc_block(data + o + i * 8, 0, i ? 0x801f801fu : 0xfc00fc00u);
    assert(decode(o + 32, 0, &im) == CODEC_OK);
    pixel(&im, 2, 2, 255, 0, 0, 255);
    pixel(&im, 6, 6, 0, 0, 255, 255);
    /* Halfway between red and blue in both directions */
    pixel(&im, 4, 2, 127, 0, 127, 255);
    pixel(&im, 4, 4, 63, 0, 191, 255);
    pixel(&im, 0, 0, 63, 0, 191, 255);
    pvr_free(&im);

    /* Translucent colours: A in 3 bits, colours in 4 */
    for (i = 0; i < 4; i++)
        pvrtc_block(data + o + i * 8, 0, 0x7f004f00u);
    h.format = 3;
    v3(h);
    assert(decode(o + 32, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 255, 0, 0, 136);
    pvr_free(&im);

    /* Morton order: the second block in the file is the one below. */
    h.format = 2;
    h.width = 16;
    h.height = 8;
    o = v3(h);
    for (i = 0; i < 8; i++)
        pvrtc_block(data + o + i * 8, 0xffffffffu, i == 1 ? 0xfc008000u : 0x801f8000u);
    assert(decode(o + 64, 0, &im) == CODEC_OK);
    pixel(&im, 2, 6, 255, 0, 0, 255);
    pixel(&im, 6, 2, 0, 0, 255, 255);
    pvr_free(&im);
    /* The wider side's extra bits go on top: block 4 is 8 pixels across. */
    for (i = 0; i < 8; i++)
        pvrtc_block(data + o + i * 8, 0xffffffffu, i == 4 ? 0xfc008000u : 0x801f8000u);
    assert(decode(o + 64, 0, &im) == CODEC_OK);
    pixel(&im, 10, 2, 255, 0, 0, 255);
    pvr_free(&im);

    /* 2bpp, 1 bit per pixel: black or white */
    h.format = 0;
    h.width = 16;
    h.height = 8;
    o = v3(h);
    for (i = 0; i < 4; i++)
        pvrtc_block(data + o + i * 8, 0x0000000fu, 0xffff8000u);
    assert(decode(o + 32, 0, &im) == CODEC_OK);
    pixel(&im, 3, 0, 255, 255, 255, 255);
    pixel(&im, 4, 0, 0, 0, 0, 255);
    pvr_free(&im);
    /* Interpolated 2bpp: stored pixels weigh 8 (value 3) and the others
       average their neighbours. Pixel 0 has its low bit from bit 1. */
    for (i = 0; i < 4; i++)
        pvrtc_block(data + o + i * 8, 0xfffffffeu, 0xffff8001u);
    assert(decode(o + 32, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 255, 255, 255, 255);
    pixel(&im, 1, 0, 255, 255, 255, 255);
    pvr_free(&im);
    /* Stored values 0 around a stored 3 at (2,0): vertical only (bits 0
       and 20 set) or horizontal only (bit 0 alone). */
    for (i = 0; i < 4; i++)
        pvrtc_block(data + o + i * 8, 0x00100001u | 3u << 2, 0xffff8001u);
    assert(decode(o + 32, 0, &im) == CODEC_OK);
    pixel(&im, 2, 0, 255, 255, 255, 255);
    pixel(&im, 3, 0, 0, 0, 0, 255);
    pixel(&im, 2, 1, 127, 127, 127, 255);
    pvr_free(&im);
    for (i = 0; i < 4; i++)
        pvrtc_block(data + o + i * 8, 0x00000001u | 3u << 2, 0xffff8001u);
    assert(decode(o + 32, 0, &im) == CODEC_OK);
    pixel(&im, 3, 0, 127, 127, 127, 255);
    pixel(&im, 2, 1, 0, 0, 0, 255);
    pvr_free(&im);
    /* In those modes the centre pixel (4,2) takes its value from bit 21. */
    for (i = 0; i < 4; i++)
        pvrtc_block(data + o + i * 8, 0x00200001u, 0xffff8001u);
    assert(decode(o + 32, 0, &im) == CODEC_OK);
    pixel(&im, 4, 2, 255, 255, 255, 255);
    pixel(&im, 5, 2, 127, 127, 127, 255);
    pixel(&im, 4, 1, 0, 0, 0, 255);
    pvr_free(&im);
    /* Both: the four neighbours */
    for (i = 0; i < 4; i++)
        pvrtc_block(data + o + i * 8, 3u << 2, 0xffff8001u);
    assert(decode(o + 32, 0, &im) == CODEC_OK);
    pixel(&im, 3, 0, 63, 63, 63, 255);
    pvr_free(&im);

    /* Small mip levels still use two blocks each way. */
    h.format = 2;
    h.width = 2;
    h.height = 2;
    o = v3(h);
    assert(decode(o + 31, 0, &im) == CODEC_TRUNCATED);
    assert(decode(o + 32, 0, &im) == CODEC_OK);
    pvr_free(&im);
    /* Sides must be powers of two. */
    h.width = 12;
    v3(h);
    assert(decode(o + 256, 0, &im) == CODEC_INVALID);
}

static void test_malformed(void)
{
    struct pvr_image im;
    size_t i;
    unsigned long n;

    memset(data, 0, 64);
    assert(pvr_decode(data, 0, 0, &im) == CODEC_TRUNCATED);
    assert(im.rgba == NULL);
    memcpy(data, "PVR\3", 4);
    for (i = 0; i < 52; i++)
        assert(pvr_decode(data, i, 0, &im) == CODEC_TRUNCATED);
    assert(pvr_count(data, 51, &n) == CODEC_TRUNCATED && n == 0);
    /* Byte-swapped version 3 */
    memcpy(data, "\3RVP", 4);
    assert(pvr_decode(data, 64, 0, &im) == CODEC_INVALID);
    put32(data, 52);
    for (i = 4; i < 52; i++)
        assert(pvr_decode(data, i, 0, &im) == CODEC_TRUNCATED);
    put32(data, 44);
    assert(pvr_decode(data, 64, 0, &im) == CODEC_INVALID);
}

static void test_writer(void)
{
    static const uint8_t rgba[8] = { 10, 20, 30, 255, 40, 50, 60, 128 };
    uint8_t row[8];
    struct pvr_image im;

    assert(!pvr_row_has_alpha(rgba, 1));
    assert(pvr_row_has_alpha(rgba, 2));
    assert(!pvr_make_header(0, 1, 0, data));
    assert(!pvr_make_header(65536, 1, 0, data));

    assert(pvr_make_header(2, 1, 1, data));
    pvr_encode_row(rgba, 2, 1, data + PVR_HEADER_SIZE);
    assert(decode(PVR_HEADER_SIZE + 8, 0, &im) == CODEC_OK);
    assert(im.width == 2 && im.height == 1);
    assert(memcmp(im.rgba, rgba, 8) == 0);
    pvr_free(&im);

    assert(pvr_make_header(1, 2, 0, data));
    pvr_encode_row(rgba, 1, 0, row);
    memcpy(data + PVR_HEADER_SIZE, row, 3);
    memcpy(data + PVR_HEADER_SIZE + 3, row, 3);
    assert(decode(PVR_HEADER_SIZE + 6, 0, &im) == CODEC_OK);
    assert(im.width == 1 && im.height == 2);
    pixel(&im, 0, 1, 10, 20, 30, 255);
    pvr_free(&im);
    assert(decode(PVR_HEADER_SIZE + 5, 0, &im) == CODEC_TRUNCATED);
}

int main(void)
{
    test_v3_bytes();
    test_v3_packed();
    test_v3_structure();
    test_v3_order();
    test_v2();
    test_etc();
    test_etc_files();
    test_bc();
    test_pvrtc();
    test_malformed();
    test_writer();
    puts("pvr: all tests passed");
    return 0;
}
