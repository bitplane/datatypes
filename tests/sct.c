#include "../formats/sct/decode.h"
#include "../formats/sct/encode.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t data[2048 + 4 * 10 * 8 + 16];
static size_t size;

static void put_long(uint8_t *p, const char *text)
{
    memset(p, ' ', 12);
    memcpy(p, text, strlen(text));
}

/* Sample of separation s (0 = C .. 3 = K) at (x, y). */
static uint8_t value(unsigned x, unsigned y, unsigned s)
{
    return (uint8_t)(x * 37u + y * 11u + s * 89u + 3u);
}

/* A CT file whose stored separations are those in mask, with padding bytes
   set to junk to prove they're skipped. */
static void make(unsigned width, unsigned height, unsigned count, unsigned mask)
{
    char text[16];
    unsigned x, y, s, row = (width + 1u) & ~1u;

    memset(data, 0, sizeof data);
    memset(data, ' ', 80);
    memcpy(data, "test", 4);
    data[80] = 'C';
    data[81] = 'T';
    data[1024] = 1;
    data[1025] = (uint8_t)count;
    data[1026] = (uint8_t)(mask >> 8);
    data[1027] = (uint8_t)mask;
    memcpy(data + 1028, "+.13888889E-01", 14);
    memcpy(data + 1042, "+.13888889E-01", 14);
    sprintf(text, "+%011u", height);
    put_long(data + 1056, text);
    sprintf(text, "+%011u", width);
    put_long(data + 1068, text);
    size = 2048;
    for (y = 0; y < height; y++)
        for (s = 0; s < 4; s++) {
            if (!(mask >> s & 1u))
                continue;
            for (x = 0; x < row; x++) {
                assert(size < sizeof data);
                data[size++] = x < width ? value(x, y, s) : 0xaa;
            }
        }
}

static uint8_t scale(unsigned a, unsigned b)
{
    return (uint8_t)((a * b + 127u) / 255u);
}

/* Decode with mask (0 meaning "as in the file") and check every pixel. */
static void check(unsigned width, unsigned height, unsigned mask)
{
    struct sct_image image;
    unsigned x, y, s;

    assert(sct_decode(data, size, &image) == CODEC_OK);
    assert(image.width == width && image.height == height);
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++) {
            const uint8_t *p = image.rgba + ((size_t)y * width + x) * 4u;
            unsigned ink[4];
            for (s = 0; s < 4; s++)
                ink[s] = (mask >> s & 1u) ? value(x, y, s) : 255u;
            for (s = 0; s < 3; s++)
                assert(p[s] == scale(ink[s], ink[3]));
            assert(p[3] == 255);
        }
    sct_free(&image);
}

static void expect(enum codec_result want)
{
    struct sct_image image;
    assert(sct_decode(data, size, &image) == want);
    if (want == CODEC_OK)
        sct_free(&image);
    else
        assert(image.rgba == NULL);
}

static void long_field(size_t offset, const char *text, enum codec_result want)
{
    make(3, 2, 3, 7);
    memset(data + offset, ' ', 12);
    memcpy(data + offset, text, strlen(text) > 12 ? 12 : strlen(text));
    expect(want);
}

static void float_field(unsigned pixels, const char *want)
{
    uint8_t header[SCT_HEADER_SIZE];
    assert(sct_make_header(pixels, 1, 0, header));
    assert(memcmp(header + 1042, want, 14) == 0);
}

static void test_variants(void)
{
    unsigned mask;

    /* Every combination of the four separations, even and odd widths. */
    for (mask = 1; mask < 16; mask++) {
        unsigned count = (mask & 1u) + (mask >> 1 & 1u) + (mask >> 2 & 1u) +
                         (mask >> 3 & 1u);
        make(4, 3, count, mask);
        check(4, 3, mask);
        make(5, 3, count, mask);
        check(5, 3, mask);
        make(1, 1, count, mask);
        check(1, 1, mask);
    }
    /* CMY is RGB, and K alone is gray, byte for byte. */
    make(3, 1, 3, 7);
    {
        struct sct_image image;
        assert(sct_decode(data, size, &image) == CODEC_OK);
        assert(image.rgba[0] == value(0, 0, 0) &&
               image.rgba[1] == value(0, 0, 1) &&
               image.rgba[2] == value(0, 0, 2));
        sct_free(&image);
    }
    /* An empty mask takes the usual meaning of 1, 3 and 4 separations. */
    make(3, 2, 1, 8);
    data[1027] = 0;
    check(3, 2, 8);
    make(3, 2, 3, 7);
    data[1027] = 0;
    check(3, 2, 7);
    make(3, 2, 4, 15);
    data[1027] = 0;
    check(3, 2, 15);
    /* Trailing data is ignored. */
    make(3, 2, 4, 15);
    size += 16;
    check(3, 2, 15);
    /* Units, physical size, scan direction and the name are not checked. */
    make(3, 2, 3, 7);
    data[1024] = 7;
    memset(data + 1028, 0, 28);
    data[1080] = 5;
    memset(data, 0, 80);
    check(3, 2, 7);
}

static void test_malformed(void)
{
    struct sct_image image;
    size_t full;

    assert(sct_decode(NULL, 0, &image) == CODEC_TRUNCATED);
    assert(sct_decode(data, 0, NULL) == CODEC_INVALID);
    make(3, 2, 3, 7);
    full = size;
    /* Truncation at each boundary. */
    size = 81;
    expect(CODEC_TRUNCATED);
    size = 82;
    expect(CODEC_TRUNCATED);
    size = 1024;
    expect(CODEC_TRUNCATED);
    size = 2047;
    expect(CODEC_TRUNCATED);
    size = 2048;
    expect(CODEC_TRUNCATED);
    size = full - 1;
    expect(CODEC_TRUNCATED);
    /* The padding byte of the last row is still required. */
    make(3, 2, 1, 8);
    size--;
    expect(CODEC_TRUNCATED);
    /* Other HandShake types, and no magic. */
    make(3, 2, 3, 7);
    memcpy(data + 80, "LW", 2);
    expect(CODEC_INVALID);
    memcpy(data + 80, "ct", 2);
    expect(CODEC_INVALID);
    size = 81;
    memcpy(data + 80, "LW", 2);
    expect(CODEC_TRUNCATED);
    /* Separation counts and masks that disagree or go beyond CMYK. */
    make(3, 2, 3, 7);
    data[1025] = 4;
    expect(CODEC_INVALID);
    data[1025] = 0;
    expect(CODEC_INVALID);
    data[1025] = 16;
    expect(CODEC_INVALID);
    make(3, 2, 3, 7);
    data[1026] = 0x10;
    expect(CODEC_INVALID);
    make(3, 2, 3, 7);
    data[1027] = 0x17;
    data[1025] = 4;
    expect(CODEC_INVALID);
    make(3, 2, 2, 3);
    data[1027] = 0;
    expect(CODEC_INVALID);
    make(3, 2, 0, 0);
    expect(CODEC_INVALID);
    /* Longs. */
    long_field(1068, "+00000000003", CODEC_OK);
    long_field(1068, "3", CODEC_OK);
    long_field(1068, "  +3", CODEC_OK);
    long_field(1056, "000000000002", CODEC_OK);
    make(3, 2, 3, 7);
    memcpy(data + 1068, "+3\0\0\0\0\0\0\0\0\0\0", 12);
    expect(CODEC_OK);
    long_field(1068, "-00000000003", CODEC_INVALID);
    long_field(1068, "+00000000000", CODEC_INVALID);
    long_field(1068, "", CODEC_INVALID);
    long_field(1068, "+", CODEC_INVALID);
    long_field(1068, "+3x", CODEC_INVALID);
    long_field(1068, "+3 3", CODEC_INVALID);
    long_field(1068, "0x3", CODEC_INVALID);
    long_field(1056, "+.2E+01", CODEC_INVALID);
    /* Sizes beyond the limits, before any data is read. */
    long_field(1068, "+00000065536", CODEC_TOO_LARGE);
    long_field(1056, "+99999999999", CODEC_TOO_LARGE);
    long_field(1068, "999999999999", CODEC_TOO_LARGE);
    make(1, 1, 3, 7);
    put_long(data + 1056, "4097");
    put_long(data + 1068, "4097");
    expect(CODEC_TOO_LARGE);
    put_long(data + 1056, "4096");
    put_long(data + 1068, "4096");
    expect(CODEC_TRUNCATED);
    put_long(data + 1056, "65535");
    put_long(data + 1068, "256");
    expect(CODEC_TRUNCATED);
    put_long(data + 1056, "65535");
    put_long(data + 1068, "257");
    expect(CODEC_TOO_LARGE);
}

/* Encode rgba, decode the result and compare it with want. */
static void roundtrip(const uint8_t *rgba, unsigned width, unsigned height,
                      int want_gray, const uint8_t *want)
{
    struct sct_image image;
    unsigned y;
    int gray = 1;

    for (y = 0; y < height; y++)
        gray = gray && sct_row_is_gray(rgba + (size_t)y * width * 4u, width);
    assert(gray == want_gray);
    assert(sct_make_header(width, height, gray, data));
    size = SCT_HEADER_SIZE;
    for (y = 0; y < height; y++) {
        assert(size + sct_line_size(width, gray) <= sizeof data);
        sct_encode_row(rgba + (size_t)y * width * 4u, width, gray, data + size);
        size += sct_line_size(width, gray);
    }
    assert(data[1025] == (gray ? 1 : 3) && data[1027] == (gray ? 8 : 7));
    assert(sct_decode(data, size, &image) == CODEC_OK);
    assert(image.width == width && image.height == height);
    assert(memcmp(image.rgba, want, (size_t)width * height * 4u) == 0);
    sct_free(&image);
}

static void test_encoder(void)
{
    uint8_t rgba[5 * 3 * 4], want[5 * 3 * 4], header[SCT_HEADER_SIZE];
    unsigned i;

    /* Opaque colour, odd width: exact. */
    for (i = 0; i < 5 * 3; i++) {
        rgba[i * 4] = (uint8_t)(i * 17);
        rgba[i * 4 + 1] = (uint8_t)(255 - i * 9);
        rgba[i * 4 + 2] = (uint8_t)(i * 3);
        rgba[i * 4 + 3] = 255;
    }
    roundtrip(rgba, 5, 3, 0, rgba);
    for (i = 0; i < 5 * 3; i++)
        rgba[i * 4 + 1] = rgba[i * 4 + 2] = rgba[i * 4];
    roundtrip(rgba, 5, 3, 1, rgba);
    roundtrip(rgba, 4, 3, 1, rgba);
    assert(size == 2048 + 4 * 3);
    roundtrip(rgba, 5, 3, 1, rgba);
    assert(size == 2048 + 6 * 3);
    /* Alpha is composited over white; colour hidden by alpha 0 is gray. */
    for (i = 0; i < 5 * 3; i++) {
        rgba[i * 4] = 200;
        rgba[i * 4 + 1] = 10;
        rgba[i * 4 + 2] = 90;
        rgba[i * 4 + 3] = 0;
        memset(want + i * 4, 255, 4);
    }
    roundtrip(rgba, 5, 3, 1, want);
    rgba[3] = 128;
    want[0] = (uint8_t)((200 * 128 + 255 * 127 + 127) / 255);
    want[1] = (uint8_t)((10 * 128 + 255 * 127 + 127) / 255);
    want[2] = (uint8_t)((90 * 128 + 255 * 127 + 127) / 255);
    roundtrip(rgba, 5, 3, 0, want);
    /* The header matches the layout real files use. */
    assert(sct_make_header(512, 300, 0, header));
    assert(memcmp(header + 80, "CT", 2) == 0 && header[79] == ' ');
    assert(header[82] == 0 && header[1023] == 0 && header[2047] == 0);
    assert(header[1024] == 1);
    assert(memcmp(header + 1028, "+.41666667E+01", 14) == 0);
    assert(memcmp(header + 1042, "+.71111111E+01", 14) == 0);
    assert(memcmp(header + 1056, "+00000000300", 12) == 0);
    assert(memcmp(header + 1068, "+00000000512", 12) == 0);
    assert(header[1080] == 0);
    float_field(1, "+.13888889E-01");
    float_field(7, "+.97222222E-01");
    float_field(8, "+.11111111E+00");
    float_field(71, "+.98611111E+00");
    float_field(72, "+.10000000E+01");
    float_field(720, "+.10000000E+02");
    float_field(7200, "+.10000000E+03");
    float_field(65535, "+.91020833E+03");
    assert(!sct_make_header(0, 1, 0, header));
    assert(!sct_make_header(1, 0, 0, header));
    assert(!sct_make_header(65536, 1, 0, header));
    assert(!sct_make_header(1, 65536, 1, header));
}

int main(void)
{
    test_variants();
    test_malformed();
    test_encoder();
    puts("sct: ok");
    return 0;
}
