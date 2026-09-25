#include "../formats/blp/decode.h"
#include "../formats/blp/encode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t data[1 << 20];

struct hdr {
    int version;            /* 1 or 2 */
    uint32_t type;          /* BLP1 compression or BLP2 type */
    uint8_t encoding, alpha_encoding;
    uint32_t alpha_bits, width, height, mips;
    uint32_t offset[16], size[16];
};

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* Header and palette entry i = (i, 2i, 3i, 255 - i) as BGRA; returns the
   offset just past the palette. */
static size_t header(struct hdr h)
{
    size_t dir, pal;
    unsigned i;

    memset(data, 0, sizeof data);
    if (h.version == 1) {
        memcpy(data, "BLP1", 4);
        put32(data + 4, h.type);
        put32(data + 8, h.alpha_bits);
        put32(data + 12, h.width);
        put32(data + 16, h.height);
        put32(data + 20, 5);
        put32(data + 24, h.mips);
        dir = 28;
        pal = 156;
    } else {
        memcpy(data, "BLP2", 4);
        put32(data + 4, h.type);
        data[8] = h.encoding;
        data[9] = (uint8_t)h.alpha_bits;
        data[10] = h.alpha_encoding;
        data[11] = (uint8_t)h.mips;
        put32(data + 12, h.width);
        put32(data + 16, h.height);
        dir = 20;
        pal = 148;
    }
    for (i = 0; i < 16; i++) {
        put32(data + dir + i * 4, h.offset[i]);
        put32(data + dir + 64 + i * 4, h.size[i]);
    }
    for (i = 0; i < 256; i++) {
        data[pal + i * 4] = (uint8_t)i;
        data[pal + i * 4 + 1] = (uint8_t)(2 * i);
        data[pal + i * 4 + 2] = (uint8_t)(3 * i);
        data[pal + i * 4 + 3] = (uint8_t)(255 - i);
    }
    return pal + 1024;
}

static void pixel(const struct blp_image *im, unsigned x, unsigned y,
                  unsigned r, unsigned g, unsigned b, unsigned a)
{
    const uint8_t *p = im->rgba + (y * im->width + x) * 4u;
    if (p[0] != r || p[1] != g || p[2] != b || p[3] != a) {
        fprintf(stderr, "pixel %u,%u is %u %u %u %u, expected %u %u %u %u\n",
                x, y, p[0], p[1], p[2], p[3], r, g, b, a);
        assert(0);
    }
}

/* Palette entry i as decoded, with alpha a. */
static void entry(const struct blp_image *im, unsigned x, unsigned y, unsigned i, unsigned a)
{
    pixel(im, x, y, (3 * i) & 255, (2 * i) & 255, i, a);
}

/* Every shorter prefix of a valid file is truncated. */
static void truncations(size_t length)
{
    struct blp_image im;
    size_t n;
    for (n = 0; n < length; n++) {
        assert(blp_decode(data, n, 0, &im) == CODEC_TRUNCATED);
        assert(im.rgba == NULL);
    }
}

static void test_palette(int version)
{
    static const unsigned bits[] = { 0, 1, 4, 8 };
    struct blp_image im;
    unsigned long count;
    unsigned b, i;

    for (b = 0; b < 4; b++) {
        struct hdr h = { 0 };
        size_t at, alpha_bytes = (10u * bits[b] + 7u) / 8u;
        h.version = version;
        h.type = 1;
        h.encoding = 1;
        h.alpha_bits = bits[b];
        h.width = 5;
        h.height = 2;
        at = header(h);
        h.offset[0] = (uint32_t)at;
        h.size[0] = (uint32_t)(10 + alpha_bytes);
        header(h);
        for (i = 0; i < 10; i++)
            data[at + i] = (uint8_t)(i * 20);
        switch (bits[b]) {
        case 1: data[at + 10] = 0x05; data[at + 11] = 0x02; break; /* pixels 0, 2, 9 */
        case 4: for (i = 0; i < 5; i++) data[at + 10 + i] = (uint8_t)(i * 0x21 + 0x10); break;
        case 8: for (i = 0; i < 10; i++) data[at + 10 + i] = (uint8_t)(i * 25); break;
        }
        assert(blp_count(data, at + 10 + alpha_bytes, &count) == CODEC_OK && count == 1);
        assert(blp_decode(data, at + 10 + alpha_bytes, 0, &im) == CODEC_OK);
        assert(im.width == 5 && im.height == 2);
        for (i = 0; i < 10; i++) {
            unsigned a = 255;
            switch (bits[b]) {
            case 1: a = (i == 0 || i == 2 || i == 9) ? 255 : 0; break;
            case 4: a = ((data[at + 10 + i / 2] >> (i % 2 * 4)) & 15) * 17; break;
            case 8: a = i * 25; break;
            }
            entry(&im, i % 5, i / 5, i * 20, a);
        }
        blp_free(&im);
        truncations(at + 10 + alpha_bytes);

        /* Pillow declares alpha but keeps it in the palette. */
        if (bits[b] != 0) {
            h.size[0] = 10;
            /* Its BLP1 files also point into the palette, 8 bytes early. */
            if (version == 1)
                h.offset[0] = 1172;
            header(h);
            for (i = 0; i < 10; i++)
                data[at + i] = (uint8_t)(i * 20);
            assert(blp_decode(data, at + 10, 0, &im) == CODEC_OK);
            for (i = 0; i < 10; i++)
                entry(&im, i % 5, i / 5, i * 20, 255 - i * 20);
            blp_free(&im);
        }
    }
}

static void test_dxt(void)
{
    /* Red and blue endpoints; index rows 0, 1, 2, 3. */
    static const uint8_t colour[8] = { 0x00, 0xf8, 0x1f, 0x00, 0x00, 0x55, 0xaa, 0xff };
    /* Three-colour block: index 3 is transparent black with punch-through. */
    static const uint8_t three[8] = { 0x1f, 0x00, 0x00, 0xf8, 0xff, 0xff, 0xff, 0xff };
    struct blp_image im;
    struct hdr h = { 0 };
    size_t at;
    unsigned x, y;

    h.version = 2;
    h.type = 1;
    h.encoding = 2;
    h.width = 6;
    h.height = 5;
    at = header(h);
    h.offset[0] = (uint32_t)at;
    h.size[0] = 32;
    header(h);
    memcpy(data + at, colour, 8);
    memcpy(data + at + 8, three, 8);
    memcpy(data + at + 16, colour, 8);
    memcpy(data + at + 24, colour, 8);
    assert(blp_decode(data, at + 32, 0, &im) == CODEC_OK);
    assert(im.width == 6 && im.height == 5);
    pixel(&im, 0, 0, 255, 0, 0, 255);
    pixel(&im, 3, 1, 0, 0, 255, 255);
    pixel(&im, 4, 0, 0, 0, 0, 255);
    pixel(&im, 5, 3, 0, 0, 0, 255);
    pixel(&im, 0, 4, 255, 0, 0, 255);
    blp_free(&im);
    truncations(at + 32);

    h.alpha_bits = 1;
    header(h);
    memcpy(data + at, colour, 8);
    memcpy(data + at + 8, three, 8);
    assert(blp_decode(data, at + 32, 0, &im) == CODEC_OK);
    pixel(&im, 4, 0, 0, 0, 0, 0);
    blp_free(&im);

    /* DXT3 and DXT5: alpha counts only when the header declares it. */
    h.alpha_encoding = 1;
    h.alpha_bits = 8;
    h.width = 4;
    h.height = 4;
    h.size[0] = 16;
    header(h);
    memset(data + at, 0x77, 8);
    memcpy(data + at + 8, colour, 8);
    assert(blp_decode(data, at + 16, 0, &im) == CODEC_OK);
    for (y = 0; y < 4; y++)
        for (x = 0; x < 4; x++)
            assert(im.rgba[(y * 4 + x) * 4 + 3] == 0x77);
    pixel(&im, 0, 0, 255, 0, 0, 0x77);
    blp_free(&im);
    truncations(at + 16);
    data[9] = 0;
    assert(blp_decode(data, at + 16, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 255, 0, 0, 255);
    blp_free(&im);

    h.alpha_encoding = 7;
    header(h);
    data[at] = 200;
    data[at + 1] = 100;
    memcpy(data + at + 8, colour, 8);
    assert(blp_decode(data, at + 16, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 255, 0, 0, 200);
    blp_free(&im);

    h.alpha_encoding = 2;
    header(h);
    assert(blp_decode(data, at + 16, 0, &im) == CODEC_INVALID);
}

static void test_bgra(void)
{
    struct blp_image im;
    struct hdr h = { 0 };
    size_t at;

    h.version = 2;
    h.type = 1;
    h.encoding = 3;
    h.alpha_bits = 8;
    h.width = 2;
    h.height = 1;
    at = header(h);
    h.offset[0] = (uint32_t)at;
    h.size[0] = 8;
    header(h);
    memcpy(data + at, "\x10\x20\x30\x00\x40\x50\x60\x80", 8);
    assert(blp_decode(data, at + 8, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 0x30, 0x20, 0x10, 0);
    pixel(&im, 1, 0, 0x60, 0x50, 0x40, 0x80);
    blp_free(&im);
    truncations(at + 8);
    data[9] = 0;
    assert(blp_decode(data, at + 8, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 0x30, 0x20, 0x10, 255);
    blp_free(&im);
}

static void test_mips(void)
{
    struct blp_image im;
    struct hdr h = { 0 };
    unsigned long count;
    size_t at, end;
    unsigned i;

    h.version = 2;
    h.type = 1;
    h.encoding = 1;
    h.width = 8;
    h.height = 2;
    h.mips = 1;
    at = header(h);
    /* 8x2, 4x1, 2x1, 1x1; entries past 1x1 are ignored. */
    for (i = 0, end = at; i < 5; i++) {
        unsigned n = i < 4 ? (8u >> i) * (i ? 1u : 2u) : 1u;
        h.offset[i] = (uint32_t)end;
        h.size[i] = n;
        end += n;
    }
    header(h);
    for (i = 0; i < 16 + 4 + 2 + 1; i++)
        data[at + i] = (uint8_t)i;
    assert(blp_count(data, end, &count) == CODEC_OK && count == 4);
    assert(blp_decode(data, end, 1, &im) == CODEC_OK);
    assert(im.width == 4 && im.height == 1);
    entry(&im, 0, 0, 16, 255);
    entry(&im, 3, 0, 19, 255);
    blp_free(&im);
    assert(blp_decode(data, end, 3, &im) == CODEC_OK);
    assert(im.width == 1 && im.height == 1);
    entry(&im, 0, 0, 22, 255);
    blp_free(&im);
    assert(blp_decode(data, end, 4, &im) == CODEC_INVALID);
    assert(blp_decode(data, end, 0xffffffffu, &im) == CODEC_INVALID);

    /* A missing last level isn't counted, and decoding it is truncated. */
    assert(blp_count(data, at + 22, &count) == CODEC_OK && count == 3);
    assert(blp_decode(data, at + 22, 3, &im) == CODEC_TRUNCATED);
    assert(blp_decode(data, at + 22, 2, &im) == CODEC_OK);
    blp_free(&im);

    /* An empty directory entry ends the chain. */
    put32(data + 20 + 2 * 4, 0);
    assert(blp_count(data, end, &count) == CODEC_OK && count == 2);

    /* Without the mip flag only the first level counts. */
    header(h);
    data[11] = 0;
    assert(blp_count(data, end, &count) == CODEC_OK && count == 1);
    assert(blp_decode(data, end, 1, &im) == CODEC_INVALID);

    /* An offset past the end, including one that would wrap. */
    header(h);
    put32(data + 20 + 4, 0xfffffff0u);
    assert(blp_count(data, end, &count) == CODEC_OK && count == 1);
    assert(blp_decode(data, end, 1, &im) == CODEC_TRUNCATED);
}

static void test_invalid(void)
{
    struct blp_image im;
    struct hdr h = { 0 };
    unsigned long count;
    size_t at;

    h.version = 2;
    h.type = 1;
    h.encoding = 1;
    h.width = 4;
    h.height = 4;
    at = header(h);
    h.offset[0] = (uint32_t)at;
    h.size[0] = 16;
    header(h);
    assert(blp_decode(data, at + 16, 0, &im) == CODEC_OK);
    blp_free(&im);

    memcpy(data, "BLP0", 4);
    assert(blp_decode(data, at + 16, 0, &im) == CODEC_INVALID);
    assert(blp_count(data, at + 16, &count) == CODEC_INVALID && count == 0);
    memcpy(data, "PNG2", 4);
    assert(blp_decode(data, at + 16, 0, &im) == CODEC_INVALID);
    header(h);
    put32(data + 4, 0); /* JPEG */
    assert(blp_decode(data, at + 16, 0, &im) == CODEC_INVALID);
    header(h);
    data[8] = 4;
    assert(blp_decode(data, at + 16, 0, &im) == CODEC_INVALID);
    header(h);
    data[9] = 2;
    assert(blp_decode(data, at + 16, 0, &im) == CODEC_INVALID);
    header(h);
    put32(data + 20, 0);
    assert(blp_decode(data, at + 16, 0, &im) == CODEC_INVALID);
    header(h);
    put32(data + 12, 0);
    assert(blp_decode(data, at + 16, 0, &im) == CODEC_INVALID);
    header(h);
    put32(data + 12, 65536);
    put32(data + 16, 1);
    assert(blp_decode(data, at + 16, 0, &im) == CODEC_TOO_LARGE);
    header(h);
    put32(data + 12, 0xffffffffu);
    assert(blp_decode(data, at + 16, 0, &im) == CODEC_TOO_LARGE);
    header(h);
    put32(data + 12, 4096);
    put32(data + 16, 4097);
    assert(blp_decode(data, at + 16, 0, &im) == CODEC_TOO_LARGE);
    header(h);
    put32(data + 12, 4096);
    put32(data + 16, 4096);
    assert(blp_decode(data, at + 16, 0, &im) == CODEC_TRUNCATED);

    h.version = 1;
    at = header(h);
    h.offset[0] = (uint32_t)at;
    header(h);
    assert(blp_decode(data, at + 16, 0, &im) == CODEC_OK);
    blp_free(&im);
    put32(data + 4, 0); /* JPEG */
    assert(blp_decode(data, at + 16, 0, &im) == CODEC_INVALID);
    header(h);
    put32(data + 8, 16);
    assert(blp_decode(data, at + 16, 0, &im) == CODEC_INVALID);
}

/* Encode rgba through the writer's three passes and decode it back. */
static size_t encode(const uint8_t *rgba, unsigned width, unsigned height,
                     struct blp_palette *palette)
{
    size_t at;
    unsigned y;

    blp_palette_init(palette);
    for (y = 0; y < height; y++)
        blp_palette_add_row(palette, rgba + y * width * 4u, width);
    assert(blp_make_header(width, height, palette, data));
    at = BLP_HEADER_SIZE;
    for (y = 0; y < height; y++) {
        blp_encode_row(palette, rgba + y * width * 4u, width, data + at);
        at += blp_row_size(palette, width);
    }
    if (!palette->full && palette->alpha)
        for (y = 0; y < height; y++, at += width)
            blp_encode_alpha_row(rgba + y * width * 4u, width, data + at);
    return at;
}

static void round_trip(const uint8_t *rgba, unsigned width, unsigned height,
                       int full, int alpha)
{
    struct blp_palette palette;
    struct blp_image im;
    size_t length = encode(rgba, width, height, &palette);

    assert(palette.full == full && palette.alpha == alpha);
    assert(data[8] == (full ? 3 : 1) && data[9] == (alpha ? 8 : 0));
    assert(blp_decode(data, length, 0, &im) == CODEC_OK);
    assert(im.width == width && im.height == height);
    assert(memcmp(im.rgba, rgba, (size_t)width * height * 4u) == 0);
    blp_free(&im);
}

static void test_encode(void)
{
    static uint8_t rgba[40 * 30 * 4];
    struct blp_palette palette;
    uint8_t h[BLP_HEADER_SIZE];
    unsigned i;

    /* 256 colours fit a palette; 257 don't. */
    for (i = 0; i < 16 * 16; i++) {
        rgba[i * 4] = (uint8_t)i;
        rgba[i * 4 + 1] = (uint8_t)(i * 7);
        rgba[i * 4 + 2] = (uint8_t)(i >> 2);
        rgba[i * 4 + 3] = 255;
    }
    round_trip(rgba, 16, 16, 0, 0);
    round_trip(rgba, 8, 32, 0, 0);
    rgba[3] = 0;
    round_trip(rgba, 16, 16, 0, 1);
    for (i = 0; i < 40 * 30; i++) {
        rgba[i * 4] = (uint8_t)i;
        rgba[i * 4 + 1] = (uint8_t)(i >> 8);
        rgba[i * 4 + 2] = (uint8_t)(i * 3);
        rgba[i * 4 + 3] = 255;
    }
    round_trip(rgba, 40, 30, 1, 0);
    round_trip(rgba, 17, 17, 1, 0);
    rgba[7] = 1;
    round_trip(rgba, 40, 30, 1, 1);
    /* Transparent pixels keep their colour. */
    memset(rgba, 0, 4 * 4);
    rgba[4] = 9;
    round_trip(rgba, 2, 1, 0, 1);

    blp_palette_init(&palette);
    assert(!blp_make_header(0, 1, &palette, h));
    assert(!blp_make_header(65536, 1, &palette, h));
    assert(blp_make_header(65535, 256, &palette, h));
    palette.full = 1;
    assert(!blp_make_header(65535, 65535, &palette, h));
}

int main(void)
{
    test_palette(1);
    test_palette(2);
    test_dxt();
    test_bgra();
    test_mips();
    test_invalid();
    test_encode();
    puts("blp: ok");
    return 0;
}
