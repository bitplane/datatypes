#include "../formats/palm/decode.h"
#include "../formats/palm/encode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COMPRESSED 0x8000u
#define COLORMAP 0x4000u
#define TRANSPARENT 0x2000u
#define DIRECT 0x0400u

static void be16(uint8_t *p, unsigned value)
{
    p[0] = (uint8_t)(value >> 8); p[1] = (uint8_t)value;
}

static void be32(uint8_t *p, unsigned long value)
{
    be16(p, (unsigned)(value >> 16)); be16(p + 2, (unsigned)value);
}

/* A version 0-2 header: 16 bytes. */
static size_t header(uint8_t *p, unsigned width, unsigned height, unsigned row_bytes,
                     unsigned flags, unsigned depth, unsigned version,
                     unsigned next_words, unsigned key, unsigned compression)
{
    memset(p, 0, 16);
    be16(p, width); be16(p + 2, height); be16(p + 4, row_bytes); be16(p + 6, flags);
    p[8] = (uint8_t)depth; p[9] = (uint8_t)version; be16(p + 10, next_words);
    p[12] = (uint8_t)key; p[13] = (uint8_t)compression;
    return 16;
}

/* A version 3 header: 24 bytes. */
static size_t header3(uint8_t *p, unsigned width, unsigned height, unsigned row_bytes,
                      unsigned flags, unsigned depth, unsigned format,
                      unsigned compression, unsigned long key, unsigned long next)
{
    memset(p, 0, 24);
    be16(p, width); be16(p + 2, height); be16(p + 4, row_bytes); be16(p + 6, flags);
    p[8] = (uint8_t)depth; p[9] = 3; p[10] = 24; p[11] = (uint8_t)format;
    p[13] = (uint8_t)compression; be16(p + 14, 72); be32(p + 16, key); be32(p + 20, next);
    return 24;
}

static enum codec_result decode(const uint8_t *data, size_t length, unsigned index)
{
    struct palm_image image;
    enum codec_result result = palm_decode(data, length, index, &image);
    if (result == CODEC_OK)
        palm_free(&image);
    else
        assert(image.rgba == NULL && image.width == 0 && image.height == 0);
    return result;
}

static void expect_only(const uint8_t *data, size_t length, unsigned index,
                        const uint8_t *pixels, unsigned width, unsigned height)
{
    struct palm_image image;
    assert(palm_decode(data, length, index, &image) == CODEC_OK);
    assert(image.width == width && image.height == height);
    assert(memcmp(image.rgba, pixels, (size_t)width * height * 4u) == 0);
    palm_free(&image);
}

static void expect(const uint8_t *data, size_t length, unsigned index,
                   const uint8_t *pixels, unsigned width, unsigned height)
{
    size_t cut;
    expect_only(data, length, index, pixels, width, height);
    /* Every shorter prefix is truncated, wherever it ends. */
    for (cut = 0; cut < length; cut++)
        assert(decode(data, cut, index) == CODEC_TRUNCATED);
}

static void gray(uint8_t *p, unsigned value, unsigned alpha)
{
    p[0] = p[1] = p[2] = (uint8_t)value; p[3] = (uint8_t)alpha;
}

static void rgb(uint8_t *p, unsigned r, unsigned g, unsigned b, unsigned a)
{
    p[0] = (uint8_t)r; p[1] = (uint8_t)g; p[2] = (uint8_t)b; p[3] = (uint8_t)a;
}

static void test_indexed(void)
{
    uint8_t d[256], px[64];
    size_t n;
    unsigned i;

    /* Version 0, 1-bit: 0 is white, 1 is black. Row padding is ignored. */
    n = header(d, 3, 2, 2, 0, 0, 0, 0, 0, 0);
    memcpy(d + n, "\xa0\xff\x5f\xff", 4);
    gray(px, 0, 255); gray(px + 4, 255, 255); gray(px + 8, 0, 255);
    gray(px + 12, 255, 255); gray(px + 16, 0, 255); gray(px + 20, 255, 255);
    expect(d, n + 4, 0, px, 3, 2);

    /* 2-bit and 4-bit default to gray ramps from white. */
    n = header(d, 4, 1, 2, 0, 2, 1, 0, 0, 0);
    memcpy(d + n, "\x1b\x00", 2);
    gray(px, 255, 255); gray(px + 4, 170, 255); gray(px + 8, 85, 255); gray(px + 12, 0, 255);
    expect(d, n + 2, 0, px, 4, 1);
    n = header(d, 3, 1, 2, 0, 4, 1, 0, 0, 0);
    memcpy(d + n, "\x0f\x70", 2);
    gray(px, 255, 255); gray(px + 4, 0, 255); gray(px + 8, 136, 255);
    expect(d, n + 2, 0, px, 3, 1);

    /* 8-bit without a table uses the Palm system palette. */
    n = header(d, 10, 1, 10, 0, 8, 1, 0, 0, 0);
    memcpy(d + n, "\x00\x05\x06\x6c\xd6\xd7\xe1\xe5\xe6\xff", 10);
    rgb(px, 255, 255, 255, 255); rgb(px + 4, 255, 0, 255, 255);
    rgb(px + 8, 255, 255, 204, 255); rgb(px + 12, 255, 255, 102, 255);
    rgb(px + 16, 0, 51, 0, 255); rgb(px + 20, 17, 17, 17, 255);
    rgb(px + 24, 192, 192, 192, 255); rgb(px + 28, 0, 128, 128, 255);
    rgb(px + 32, 0, 0, 0, 255); rgb(px + 36, 0, 0, 0, 255);
    expect(d, n + 10, 0, px, 10, 1);

    /* A colour table is used in order whatever its index bytes say; indexes
       past its end are black. It applies at any depth. */
    n = header(d, 3, 1, 4, COLORMAP, 8, 1, 0, 0, 0);
    be16(d + n, 2); memcpy(d + n + 2, "\x07\x10\x20\x30\x07\x40\x50\x60", 8);
    memcpy(d + n + 10, "\x01\x00\x02\x00", 4);
    rgb(px, 0x40, 0x50, 0x60, 255); rgb(px + 4, 0x10, 0x20, 0x30, 255); rgb(px + 8, 0, 0, 0, 255);
    expect(d, n + 14, 0, px, 3, 1);
    n = header(d, 2, 1, 2, COLORMAP, 1, 1, 0, 0, 0);
    be16(d + n, 2); memcpy(d + n + 2, "\0\x11\x22\x33\x01\x44\x55\x66", 8);
    memcpy(d + n + 10, "\x40\x00", 2);
    rgb(px, 0x11, 0x22, 0x33, 255); rgb(px + 4, 0x44, 0x55, 0x66, 255);
    expect_only(d, n + 12, 0, px, 2, 1);

    /* ImageMagick sets the colour table flag on 1, 2 and 4-bit bitmaps but
       writes no table. Below 8 bits a table needs 1 to 2^depth entries and
       room for the rows after it, or the flag is ignored. */
    n = header(d, 3, 2, 2, COLORMAP, 1, 0, 0, 0, 0);
    memcpy(d + n, "\xa0\x00\x40\x00", 4);
    gray(px, 0, 255); gray(px + 4, 255, 255); gray(px + 8, 0, 255);
    gray(px + 12, 255, 255); gray(px + 16, 0, 255); gray(px + 20, 255, 255);
    expect(d, n + 4, 0, px, 3, 2);
    memcpy(d + n, "\x00\x00\x00\x00", 4);
    for (i = 0; i < 6; i++) gray(px + i * 4, 255, 255);
    expect(d, n + 4, 0, px, 3, 2);
    n = header(d, 4, 1, 2, COLORMAP, 2, 1, 0, 0, 0);
    memcpy(d + n, "\x00\x05\x1b\x00", 4);
    memset(d + n + 4, 0x77, 20);
    rgb(px, 255, 255, 255, 255); rgb(px + 4, 255, 255, 255, 255);
    rgb(px + 8, 255, 255, 255, 255); rgb(px + 12, 255, 255, 255, 255);
    expect_only(d, n + 24, 0, px, 4, 1);
    /* Compressed, the table must fit before the end of the bitmap. */
    n = header(d, 4, 1, 2, COLORMAP | COMPRESSED, 2, 2, 0, 0, 1);
    memcpy(d + n, "\x00\x04\x02\x1b", 4);
    gray(px, 255, 255); gray(px + 4, 170, 255); gray(px + 8, 85, 255); gray(px + 12, 0, 255);
    expect(d, n + 4, 0, px, 4, 1);

    /* A table larger than 256 entries is skipped past. */
    n = header(d, 1, 1, 2, COLORMAP, 8, 1, 0, 0, 0);
    {
        uint8_t *big = calloc(16 + 2 + 300 * 4 + 2, 1);
        assert(big != NULL);
        memcpy(big, d, 16);
        be16(big + 16, 300);
        big[18 + 1] = 9;
        big[18 + 300 * 4] = 0;
        rgb(px, 9, 0, 0, 255);
        expect(big, 16 + 2 + 300 * 4 + 2, 0, px, 1, 1);
        free(big);
    }

    /* Transparent index: only with the flag. */
    n = header(d, 3, 1, 4, TRANSPARENT, 8, 2, 0, 5, 0);
    memcpy(d + n, "\x05\x00\x05\x00", 4);
    rgb(px, 255, 0, 255, 0); rgb(px + 4, 255, 255, 255, 255); rgb(px + 8, 255, 0, 255, 0);
    expect(d, n + 4, 0, px, 3, 1);
    d[6] = 0;
    for (i = 0; i < 3; i++) px[i * 4 + 3] = 255;
    expect(d, n + 4, 0, px, 3, 1);
    /* The flag is explicit, so a key covering every pixel hides them all. */
    n = header(d, 2, 1, 2, TRANSPARENT, 8, 2, 0, 0, 0);
    memset(d + n, 0, 2);
    rgb(px, 255, 255, 255, 0); rgb(px + 4, 255, 255, 255, 0);
    expect(d, n + 2, 0, px, 2, 1);
}

static void test_direct(void)
{
    uint8_t d[128], px[32];
    size_t n;

    /* Version 2, 16-bit: 5:6:5 big-endian, scaled up by 255/31 and 255/63.
       The transparent colour is 8-bit RGB matched after reduction to 5:6:5. */
    n = header(d, 3, 1, 6, DIRECT | TRANSPARENT, 16, 2, 0, 0, 0);
    memcpy(d + n, "\x05\x06\x05\x00\x00\x08\x04\x08", 8);
    memcpy(d + n + 8, "\xf8\x00\x08\x21\x07\xe0", 6);
    rgb(px, 255, 0, 0, 255); rgb(px + 4, 8, 4, 8, 0); rgb(px + 8, 0, 255, 0, 255);
    expect(d, n + 14, 0, px, 3, 1);
    /* Only 5:6:5 exists. */
    d[n] = 4;
    assert(decode(d, n + 14, 0) == CODEC_INVALID);
    /* The depth decides, not the flag: ImageMagick flags its 8-bit system
       palette bitmaps as direct colour. */
    n = header(d, 1, 1, 2, 0, 16, 2, 0, 0, 0);
    memcpy(d + n, "\x05\x06\x05\x00\x00\x00\x00\x00\x07\xe0", 10);
    rgb(px, 0, 255, 0, 255);
    expect(d, n + 10, 0, px, 1, 1);
    n = header(d, 1, 1, 2, DIRECT, 8, 2, 0, 0, 0);
    memcpy(d + n, "\x05\x00", 2);
    rgb(px, 255, 0, 255, 255);
    expect(d, n + 2, 0, px, 1, 1);

    /* Version 3: the pixel format says 5:6:5 and the key is a 5:6:5 value. */
    n = header3(d, 2, 1, 4, TRANSPARENT, 16, 1, 0, 0xffff001ful, 0);
    memcpy(d + n, "\x00\x1f\xff\xff", 4);
    rgb(px, 0, 0, 255, 0); rgb(px + 4, 255, 255, 255, 255);
    expect(d, n + 4, 0, px, 2, 1);
    /* Little-endian formats and mismatched depths are rejected. */
    d[11] = 3;
    assert(decode(d, n + 4, 0) == CODEC_INVALID);
    d[11] = 2;
    assert(decode(d, n + 4, 0) == CODEC_INVALID);
    d[11] = 0;
    assert(decode(d, n + 4, 0) == CODEC_INVALID);
    n = header3(d, 1, 1, 2, DIRECT, 8, 0, 0, 0, 0);
    memcpy(d + n, "\x05\x00", 2);
    rgb(px, 255, 0, 255, 255);
    expect(d, n + 2, 0, px, 1, 1);

    /* Version 3 indexed, with a longer header than 24 bytes. */
    n = header3(d, 2, 1, 2, TRANSPARENT, 8, 0, 0, 0x06, 0);
    d[10] = 28;
    memcpy(d + 24, "\x99\x99\x99\x99\x06\x00", 6);
    rgb(px, 255, 255, 204, 0); rgb(px + 4, 255, 255, 255, 255);
    expect(d, 30, 0, px, 2, 1);
    /* A size byte below 24 means 24. */
    d[10] = 0;
    memcpy(d + 24, "\x06\x00", 2);
    expect(d, 26, 0, px, 2, 1);
}

static void test_compression(void)
{
    uint8_t d[512], px[512], *p;
    size_t n, i;

    /* Scanline: a flag bit per byte says it is new; other bytes repeat the row
       above. The first row is read in full, whatever its flags. Size fields
       are two bytes before version 3. */
    n = header(d, 10, 3, 10, COMPRESSED, 8, 2, 0, 0, 0);
    p = d + n;
    memcpy(p, "\x00\x00", 2); p += 2;
    *p++ = 0x00; memcpy(p, "\x01\x02\x03\x04\x05\x06\x07\x08", 8); p += 8;
    *p++ = 0x00; memcpy(p, "\x09\x0a", 2); p += 2;
    *p++ = 0x81; *p++ = 0x11; *p++ = 0x18;
    *p++ = 0x40; *p++ = 0x1a;
    *p++ = 0x00; *p++ = 0x00;
    {
        static const uint8_t rows[30] = {
            1,2,3,4,5,6,7,8,9,10, 0x11,2,3,4,5,6,7,0x18,9,0x1a, 0x11,2,3,4,5,6,7,0x18,9,0x1a};
        uint8_t pal[256][3];
        for (i = 0; i < 30; i++) {
            /* The system palette's first entries, computed as in the codec. */
            unsigned v = rows[i], j = v % 108u;
            pal[v][0] = (uint8_t)(255u - 51u * (j / 18u));
            pal[v][1] = (uint8_t)(255u - 51u * (j % 6u));
            pal[v][2] = (uint8_t)(255u - 51u * ((v / 108u) * 3u + (j % 18u) / 6u));
            rgb(px + i * 4, pal[v][0], pal[v][1], pal[v][2], 255);
        }
        expect(d, (size_t)(p - d), 0, px, 10, 3);
    }

    /* RLE: count and value pairs. A run past the row is cut short, and a
       zero count adds nothing. */
    n = header(d, 4, 2, 4, COMPRESSED, 8, 2, 0, 0, 1);
    p = d + n;
    memcpy(p, "\x00\x00", 2); p += 2;
    memcpy(p, "\x00\x07\x02\x00\x09\x05", 6); p += 6;
    memcpy(p, "\x04\x05", 2); p += 2;
    rgb(px, 255, 255, 255, 255); rgb(px + 4, 255, 255, 255, 255);
    for (i = 2; i < 8; i++) rgb(px + i * 4, 255, 0, 255, 255);
    expect(d, (size_t)(p - d), 0, px, 4, 2);

    /* PackBits: n < 128 is n + 1 literals, n >= 128 is 257 - n repeats. */
    n = header(d, 5, 1, 6, COMPRESSED, 8, 2, 0, 0, 2);
    p = d + n;
    memcpy(p, "\x00\x00", 2); p += 2;
    memcpy(p, "\x01\x05\x00\xfe\x06\x00\x05", 7); p += 7;
    rgb(px, 255, 0, 255, 255); rgb(px + 4, 255, 255, 255, 255);
    for (i = 2; i < 5; i++) rgb(px + i * 4, 255, 255, 204, 255);
    expect(d, (size_t)(p - d), 0, px, 5, 1);
    /* 0x80 repeats 129 times; the overrun is dropped. So is a literal's. */
    n = header(d, 2, 2, 2, COMPRESSED, 8, 2, 0, 0, 2);
    p = d + n;
    memcpy(p, "\x00\x00", 2); p += 2;
    memcpy(p, "\x80\x05", 2); p += 2;
    memcpy(p, "\x02\x06\x06\x07", 4); p += 4;
    rgb(px, 255, 0, 255, 255); rgb(px + 4, 255, 0, 255, 255);
    rgb(px + 8, 255, 255, 204, 255); rgb(px + 12, 255, 255, 204, 255);
    expect(d, (size_t)(p - d), 0, px, 2, 2);

    /* 16-bit PackBits works on words. Version 3 has a 4-byte size. */
    n = header3(d, 3, 1, 6, COMPRESSED, 16, 1, 2, 0, 0);
    p = d + n;
    memcpy(p, "\0\0\0\0", 4); p += 4;
    memcpy(p, "\xff\xf8\x00\x00\x00\x1f", 6); p += 6;
    rgb(px, 255, 0, 0, 255); rgb(px + 4, 255, 0, 0, 255); rgb(px + 8, 0, 0, 255, 255);
    expect(d, (size_t)(p - d), 0, px, 3, 1);
    /* An odd row length cuts the last word in half. */
    n = header3(d, 1, 1, 3, COMPRESSED, 16, 1, 2, 0, 0);
    p = d + n;
    memcpy(p, "\0\0\0\0", 4); p += 4;
    memcpy(p, "\xff\x07\xe0", 3); p += 3;
    rgb(px, 0, 255, 0, 255);
    expect(d, (size_t)(p - d), 0, px, 1, 1);

    /* Compressed with type 0xff means no compression and no size field. */
    n = header(d, 1, 1, 2, COMPRESSED, 8, 2, 0, 0, 0xff);
    memcpy(d + n, "\x05\x00", 2);
    rgb(px, 255, 0, 255, 255);
    expect(d, n + 2, 0, px, 1, 1);
    /* Unknown compression. */
    d[13] = 3;
    assert(decode(d, n + 2, 0) == CODEC_INVALID);
    /* Without the flag the type is ignored. */
    d[6] = 0;
    expect(d, n + 2, 0, px, 1, 1);
}

static void test_family(void)
{
    uint8_t d[256], px[256];
    size_t n = 0;
    unsigned count, best, i;

    /* 1-bit 2x2, then 8-bit 2x2, a separator, then version 3 4x4 at 2-bit. */
    n += header(d + n, 2, 2, 2, 0, 1, 1, 5, 0, 0);
    memcpy(d + n, "\x80\x00\x40\x00", 4); n += 4;
    assert(n == 20);
    n += header(d + n, 2, 2, 2, 0, 8, 1, 6, 0, 0);
    memcpy(d + n, "\x05\x00\x00\x05", 4); n += 4;
    memset(d + n, 0, 4); n += 4;
    assert(n == 44);
    memset(d + n, 0, 16); d[n + 8] = 0xff; d[n + 9] = 1; n += 16;
    n += header3(d + n, 4, 4, 2, 0, 2, 0, 0, 0, 0);
    memset(d + n, 0xe4, 8); n += 8;

    assert(palm_count(d, n, &count) == CODEC_OK && count == 3);
    assert(palm_best(d, n, &best) == CODEC_OK && best == 2);
    gray(px, 0, 255); gray(px + 4, 255, 255); gray(px + 8, 255, 255); gray(px + 12, 0, 255);
    expect_only(d, n, 0, px, 2, 2);
    rgb(px, 255, 0, 255, 255); rgb(px + 4, 255, 255, 255, 255);
    rgb(px + 8, 255, 255, 255, 255); rgb(px + 12, 255, 0, 255, 255);
    expect_only(d, n, 1, px, 2, 2);
    for (i = 0; i < 16; i++) gray(px + i * 4, (i % 4u) * 85u, 255);
    expect_only(d, n, 2, px, 4, 4);
    /* A family cut short ends early; one cut inside a bitmap is truncated. */
    for (i = 0; i < n; i++) {
        assert(decode(d, i, 0) == (i < 20 ? CODEC_TRUNCATED : CODEC_OK));
        assert(decode(d, i, 2) == (i < 16 || i >= 84 ? CODEC_TRUNCATED : CODEC_INVALID));
    }
    assert(decode(d, n, 3) == CODEC_INVALID);
    assert(decode(d, n, 0xffffffffu) == CODEC_INVALID);

    /* Same area: the deeper bitmap wins; the first one on a tie. */
    d[20 + 8] = 1;
    assert(palm_best(d, n, &best) == CODEC_OK && best == 2);
    d[60] = 0; d[61] = 2; d[62] = 0; d[63] = 2;
    assert(palm_best(d, n, &best) == CODEC_OK && best == 2);
    d[60 + 8] = 1;
    assert(palm_best(d, n, &best) == CODEC_OK && best == 0);
    d[20 + 8] = 8;
    assert(palm_best(d, n, &best) == CODEC_OK && best == 1);

    /* A broken bitmap ends the family: the ones before it still load. */
    d[20 + 9] = 9;
    assert(palm_count(d, n, &count) == CODEC_OK && count == 1);
    assert(palm_best(d, n, &best) == CODEC_OK && best == 0);
    assert(decode(d, n, 1) == CODEC_INVALID);
    d[20 + 9] = 1;
    /* An offset to the end of the file (as pnmtopalm -offset writes) or past
       it ends the family too. */
    be16(d + 10, (unsigned)(n / 4u));
    assert(palm_count(d, n, &count) == CODEC_OK && count == 1);
    be16(d + 10, 0xffff);
    assert(palm_count(d, n, &count) == CODEC_OK && count == 1);
    /* Only a first bitmap's errors are the file's. */
    d[9] = 7;
    assert(palm_count(d, n, &count) == CODEC_INVALID && count == 0);
    assert(palm_best(d, n, &best) == CODEC_INVALID);
    assert(palm_count(d, 10, &count) == CODEC_TRUNCATED);
    /* Separators alone are not a picture. */
    memset(d, 0, 32); d[8] = d[24] = 0xff;
    assert(palm_count(d, 32, &count) == CODEC_TRUNCATED);
}

static void test_malformed(void)
{
    uint8_t d[64];
    size_t n;

    n = header(d, 1, 1, 2, 0, 8, 1, 0, 0, 0);
    d[n] = d[n + 1] = 0;
    assert(decode(d, n + 2, 0) == CODEC_OK);
    d[1] = 0;
    assert(decode(d, n + 2, 0) == CODEC_INVALID);
    d[1] = 1; d[3] = 0;
    assert(decode(d, n + 2, 0) == CODEC_INVALID);
    d[3] = 1; d[8] = 3;
    assert(decode(d, n + 2, 0) == CODEC_INVALID);
    d[8] = 32;
    assert(decode(d, n + 2, 0) == CODEC_INVALID);
    d[8] = 8; d[9] = 4;
    assert(decode(d, n + 2, 0) == CODEC_INVALID);
    d[9] = 1;
    /* Rows too short for the width. */
    n = header(d, 17, 1, 2, 0, 1, 0, 0, 0, 0);
    assert(decode(d, n + 4, 0) == CODEC_INVALID);
    n = header(d, 3, 1, 5, DIRECT, 16, 2, 0, 0, 0);
    assert(decode(d, 60, 0) == CODEC_INVALID);
    /* More than 16M pixels. */
    n = header(d, 65535, 65535, 65535, 0, 1, 0, 0, 0, 0);
    assert(decode(d, 64, 0) == CODEC_TOO_LARGE);
    n = header(d, 4097, 4096, 4097, 0, 8, 1, 0, 0, 0);
    assert(decode(d, 64, 0) == CODEC_TOO_LARGE);
    assert(decode(NULL, 0, 0) == CODEC_TRUNCATED);
    assert(palm_decode(d, 64, 0, NULL) == CODEC_INVALID);
}

static void round_trip(const uint8_t *rgba, unsigned width, unsigned height,
                       const uint8_t *want, unsigned depth, unsigned version, unsigned flags)
{
    static struct palm_encoder e;
    uint8_t *out = malloc(PALM_HEADER_MAX + (size_t)(width * 2u + 2u) * height);
    size_t n, row, y;
    struct palm_image image;

    assert(out != NULL);
    palm_encode_begin(&e, width, height);
    for (y = 0; y < height; y++)
        palm_encode_scan(&e, rgba + y * width * 4u);
    n = palm_encode_header(&e, out, PALM_HEADER_MAX);
    assert(n != 0);
    assert(out[8] == depth && out[9] == version && (unsigned)(out[6] << 8 | out[7]) == flags);
    row = palm_encode_row_size(&e);
    assert(row % 2 == 0);
    for (y = 0; y < height; y++, n += row)
        assert(palm_encode_row(&e, rgba + y * width * 4u, out + n, row) == row);
    assert(palm_decode(out, n, 0, &image) == CODEC_OK);
    assert(image.width == width && image.height == height);
    assert(memcmp(image.rgba, want, (size_t)width * height * 4u) == 0);
    palm_free(&image);
    free(out);
}

static void test_encode(void)
{
    static uint8_t in[600 * 4], want[600 * 4];
    static struct palm_encoder e;
    uint8_t header_out[PALM_HEADER_MAX], row[8];
    unsigned i;

    /* Few colours: 8-bit with a table, lossless; odd widths pad rows. */
    for (i = 0; i < 15; i++)
        rgb(in + i * 4, i * 17u, 255u - i, i & 1 ? 0 : 200, 255);
    round_trip(in, 5, 3, in, 8, 1, COLORMAP);

    /* Zero alpha becomes the key, which gets a colour no pixel has. Partial
       alpha is composited over white. */
    memcpy(want, in, 15 * 4);
    rgb(in, 255, 0, 255, 0);
    rgb(in + 4, 0, 0, 0, 128);
    rgb(want + 4, 127, 127, 127, 255);
    rgb(in + 8, 255, 0, 255, 255);
    memcpy(want + 8, in + 8, 4);
    {
        struct palm_image image;
        uint8_t out[PALM_HEADER_MAX + 64];
        size_t n, y;
        palm_encode_begin(&e, 5, 3);
        for (y = 0; y < 3; y++)
            palm_encode_scan(&e, in + y * 20u);
        n = palm_encode_header(&e, out, sizeof out);
        for (y = 0; y < 3; y++, n += 6)
            assert(palm_encode_row(&e, in + y * 20u, out + n, 6) == 6);
        assert(palm_decode(out, n, 0, &image) == CODEC_OK);
        assert(image.rgba[3] == 0);
        /* The key's colour is not one the image uses. */
        assert(memcmp(image.rgba, "\xff\x00\xff", 3) != 0);
        assert(image.rgba[4 * 2 + 3] == 255);
        assert(memcmp(image.rgba + 4, want + 4, 56) == 0);
        palm_free(&image);
    }

    /* More than 256 colours: 16-bit RGB565, rounded to the nearest level. */
    for (i = 0; i < 600; i++) {
        rgb(in + i * 4, i % 256u, i / 3u, 255u - i % 200u, 255);
        rgb(want + i * 4, (((i % 256u) * 31u + 127u) / 255u) * 255u / 31u,
            (((i / 3u) * 63u + 127u) / 255u) * 255u / 63u,
            (((255u - i % 200u) * 31u + 127u) / 255u) * 255u / 31u, 255);
    }
    round_trip(in, 30, 20, want, 16, 2, DIRECT);
    /* With transparency the key is the first unused 5:6:5 value: black. */
    rgb(in, 0, 0, 0, 0); rgb(want, 0, 0, 0, 0);
    round_trip(in, 30, 20, want, 16, 2, DIRECT | TRANSPARENT);
    /* 256 colours plus a key don't fit in 8 bits. */
    for (i = 0; i < 257; i++) {
        rgb(in + i * 4, i, 7, 7, i == 256 ? 0 : 255);
        rgb(want + i * 4, ((i * 31u + 127u) / 255u) * 255u / 31u, 8, 8, i == 256 ? 0 : 255);
    }
    rgb(want + 256 * 4, 0, 0, 0, 0);
    round_trip(in, 257, 1, want, 16, 2, DIRECT | TRANSPARENT);
    /* Exactly 256 opaque colours still fit. */
    round_trip(in, 256, 1, in, 8, 1, COLORMAP);

    /* An all-transparent image still saves. */
    rgb(in, 1, 2, 3, 0);
    palm_encode_begin(&e, 1, 1);
    palm_encode_scan(&e, in);
    assert(palm_encode_header(&e, header_out, sizeof header_out) != 0);
    assert(palm_encode_row(&e, in, row, sizeof row) == 2 && row[0] == e.key);

    /* Limits: rows must fit in 16 bits, and the buffers must be big enough. */
    palm_encode_begin(&e, 65535, 1);
    assert(palm_encode_header(&e, header_out, sizeof header_out) == 0);
    palm_encode_begin(&e, 65534, 1);
    assert(palm_encode_header(&e, header_out, sizeof header_out) != 0);
    palm_encode_begin(&e, 0, 1);
    assert(palm_encode_header(&e, header_out, sizeof header_out) == 0);
    palm_encode_begin(&e, 1, 1);
    rgb(in, 1, 2, 3, 255);
    palm_encode_scan(&e, in);
    assert(palm_encode_header(&e, header_out, PALM_HEADER_MAX - 1) == 0);
    assert(palm_encode_header(&e, header_out, sizeof header_out) == 16 + 2 + 4);
    assert(palm_encode_row(&e, in, row, 1) == 0);
    /* A colour the scan didn't see can't be encoded. */
    rgb(in, 9, 9, 9, 255);
    assert(palm_encode_row(&e, in, row, sizeof row) == 0);
}

int main(void)
{
    test_indexed();
    test_direct();
    test_compression();
    test_family();
    test_malformed();
    test_encode();
    puts("palm tests passed");
    return 0;
}
