#include "../formats/pam/decode.h"
#include "../formats/pam/encode.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t buffer[4096];

/* A PAM header followed by raw bytes, in buffer. */
static size_t pam(const char *header, const uint8_t *raster, size_t raster_length)
{
    size_t length = strlen(header);
    memcpy(buffer, header, length);
    memcpy(buffer + length, raster, raster_length);
    return length + raster_length;
}

static void put_float(uint8_t *p, float value, int little)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    if (little) {
        p[0] = (uint8_t)bits; p[1] = (uint8_t)(bits >> 8);
        p[2] = (uint8_t)(bits >> 16); p[3] = (uint8_t)(bits >> 24);
    } else {
        p[0] = (uint8_t)(bits >> 24); p[1] = (uint8_t)(bits >> 16);
        p[2] = (uint8_t)(bits >> 8); p[3] = (uint8_t)bits;
    }
}

/* A float map of the given samples, in buffer. */
static size_t pfm(const char *header, const float *samples, size_t count, int little)
{
    size_t length = strlen(header), i;
    memcpy(buffer, header, length);
    for (i = 0; i < count; i++)
        put_float(buffer + length + 4 * i, samples[i], little);
    return length + 4 * count;
}

static void expect_image(const uint8_t *data, size_t length, unsigned index,
                         const uint8_t *pixels, unsigned width, unsigned height)
{
    struct pam_image image;
    assert(pam_decode(data, length, index, &image) == CODEC_OK);
    assert(image.width == width && image.height == height);
    if (memcmp(image.rgba, pixels, (size_t)width * height * 4u) != 0) {
        size_t i;
        for (i = 0; i < (size_t)width * height * 4u; i++)
            fprintf(stderr, "%u%s", image.rgba[i], i % 4 == 3 ? " | " : ",");
        fprintf(stderr, "\n");
        assert(0);
    }
    pam_free(&image);
}

static void expect(const uint8_t *data, size_t length,
                   const uint8_t *pixels, unsigned width, unsigned height)
{
    expect_image(data, length, 0, pixels, width, height);
}

static enum codec_result decode(const uint8_t *data, size_t length, unsigned index)
{
    struct pam_image image;
    enum codec_result result = pam_decode(data, length, index, &image);
    if (result == CODEC_OK) {
        assert(image.rgba != NULL);
        pam_free(&image);
    } else
        assert(image.rgba == NULL && image.width == 0 && image.height == 0);
    return result;
}

/* Every proper prefix of a valid single image is truncated. */
static void expect_truncated_prefixes(const uint8_t *data, size_t length)
{
    size_t i;
    for (i = 0; i < length; i++) {
        assert(decode(data, i, 0) == CODEC_TRUNCATED);
        assert(pam_count(data, i) == 0);
    }
}

static enum codec_result decode_header(const char *header, size_t raster)
{
    static const uint8_t zero[256];
    assert(raster <= sizeof zero);
    return decode(buffer, pam(header, zero, raster), 0);
}

static void test_pam_tuple_types(void)
{
    const uint8_t bw[8] = {0,0,0,255, 255,255,255,255};
    const uint8_t bwa[8] = {0,0,0,255, 255,255,255,0};
    const uint8_t gray[8] = {10,10,10,255, 200,200,200,255};
    const uint8_t gray_alpha[8] = {10,10,10,128, 200,200,200,0};
    const uint8_t rgb[8] = {1,2,3,255, 4,5,6,255};
    const uint8_t rgba[8] = {1,2,3,128, 4,5,6,0};
    const uint8_t cmyk[8] = {255,0,0,255, 127,127,127,255};
    const uint8_t cmyka[8] = {255,0,0,128, 127,127,127,255};
    size_t length;

    length = pam("P7\nWIDTH 2\nHEIGHT 1\nDEPTH 1\nMAXVAL 1\n"
                 "TUPLTYPE BLACKANDWHITE\nENDHDR\n", (const uint8_t *)"\0\1", 2);
    expect(buffer, length, bw, 2, 1);
    expect_truncated_prefixes(buffer, length);
    length = pam("P7\nWIDTH 2\nHEIGHT 1\nDEPTH 2\nMAXVAL 1\n"
                 "TUPLTYPE BLACKANDWHITE_ALPHA\nENDHDR\n",
                 (const uint8_t *)"\0\1\1\0", 4);
    expect(buffer, length, bwa, 2, 1);
    /* BLACKANDWHITE with a larger maxval is scaled like gray. */
    length = pam("P7\nWIDTH 2\nHEIGHT 1\nDEPTH 1\nMAXVAL 255\n"
                 "TUPLTYPE BLACKANDWHITE\nENDHDR\n", (const uint8_t *)"\0\xff", 2);
    expect(buffer, length, bw, 2, 1);
    length = pam("P7\nWIDTH 2\nHEIGHT 1\nDEPTH 1\nMAXVAL 255\n"
                 "TUPLTYPE GRAYSCALE\nENDHDR\n", (const uint8_t *)"\x0a\xc8", 2);
    expect(buffer, length, gray, 2, 1);
    expect_truncated_prefixes(buffer, length);
    length = pam("P7\nWIDTH 2\nHEIGHT 1\nDEPTH 2\nMAXVAL 255\n"
                 "TUPLTYPE GRAYSCALE_ALPHA\nENDHDR\n",
                 (const uint8_t *)"\x0a\x80\xc8\0", 4);
    expect(buffer, length, gray_alpha, 2, 1);
    length = pam("P7\nWIDTH 2\nHEIGHT 1\nDEPTH 3\nMAXVAL 255\n"
                 "TUPLTYPE RGB\nENDHDR\n", (const uint8_t *)"\1\2\3\4\5\6", 6);
    expect(buffer, length, rgb, 2, 1);
    expect_truncated_prefixes(buffer, length);
    length = pam("P7\nWIDTH 2\nHEIGHT 1\nDEPTH 4\nMAXVAL 255\n"
                 "TUPLTYPE RGB_ALPHA\nENDHDR\n",
                 (const uint8_t *)"\1\2\3\x80\4\5\6\0", 8);
    expect(buffer, length, rgba, 2, 1);
    length = pam("P7\nWIDTH 2\nHEIGHT 1\nDEPTH 4\nMAXVAL 255\n"
                 "TUPLTYPE CMYK\nENDHDR\n",
                 (const uint8_t *)"\0\xff\xff\0\0\0\0\x80", 8);
    expect(buffer, length, cmyk, 2, 1);
    length = pam("P7\nWIDTH 2\nHEIGHT 1\nDEPTH 5\nMAXVAL 255\n"
                 "TUPLTYPE CMYK_ALPHA\nENDHDR\n",
                 (const uint8_t *)"\0\xff\xff\0\x80\0\0\0\x80\xff", 10);
    expect(buffer, length, cmyka, 2, 1);
}

static void test_pam_alpha(void)
{
    const uint8_t opaque[8] = {1,2,3,255, 4,5,6,255};
    const uint8_t gray[8] = {10,10,10,255, 200,200,200,255};
    const uint8_t transparent[8] = {1,2,3,0, 4,5,6,0};
    const uint8_t gray_transparent[8] = {10,10,10,0, 200,200,200,0};
    size_t length;

    /* Declared alpha that is zero everywhere stays transparent. */
    length = pam("P7\nWIDTH 2\nHEIGHT 1\nDEPTH 4\nMAXVAL 255\n"
                 "TUPLTYPE RGB_ALPHA\nENDHDR\n",
                 (const uint8_t *)"\1\2\3\0\4\5\6\0", 8);
    expect(buffer, length, transparent, 2, 1);
    length = pam("P7\nWIDTH 2\nHEIGHT 1\nDEPTH 2\nMAXVAL 255\n"
                 "TUPLTYPE GRAYSCALE_ALPHA\nENDHDR\n",
                 (const uint8_t *)"\x0a\0\xc8\0", 4);
    expect(buffer, length, gray_transparent, 2, 1);
    /* Without a tuple type saying so, extra planes are not alpha. */
    length = pam("P7\nWIDTH 2\nHEIGHT 1\nDEPTH 4\nMAXVAL 255\nENDHDR\n",
                 (const uint8_t *)"\1\2\3\x80\4\5\6\0", 8);
    expect(buffer, length, opaque, 2, 1);
    length = pam("P7\nWIDTH 2\nHEIGHT 1\nDEPTH 2\nMAXVAL 255\nENDHDR\n",
                 (const uint8_t *)"\x0a\x80\xc8\0", 4);
    expect(buffer, length, gray, 2, 1);
}

static void test_pam_depths(void)
{
    const uint8_t gray[8] = {10,10,10,255, 200,200,200,255};
    const uint8_t rgb[8] = {1,2,3,255, 4,5,6,255};
    const uint8_t rgb_extra[8] = {1,2,3,255, 9,4,5,255};
    const uint8_t scaled[8] = {0,128,255,255, 1,254,255,255};
    const uint8_t sevenths[8] = {109,109,109,255, 146,146,146,255};
    const uint8_t clamped[8] = {128,128,128,255, 255,255,255,255};
    const uint8_t wide[8] = {18,128,255,255, 0,1,253,255};
    size_t length;

    /* No tuple type: 1 or 2 planes are gray, 3 or more are RGB. */
    length = pam("P7\nWIDTH 2\nHEIGHT 1\nDEPTH 1\nMAXVAL 255\nENDHDR\n",
                 (const uint8_t *)"\x0a\xc8", 2);
    expect(buffer, length, gray, 2, 1);
    length = pam("P7\nWIDTH 2\nHEIGHT 1\nDEPTH 3\nMAXVAL 255\n"
                 "TUPLTYPE SOMETHING_ELSE\nENDHDR\n",
                 (const uint8_t *)"\1\2\3\4\5\6", 6);
    expect(buffer, length, rgb, 2, 1);
    /* Planes beyond those the type uses are skipped. */
    length = pam("P7\nWIDTH 2\nHEIGHT 1\nDEPTH 5\nMAXVAL 255\nENDHDR\n",
                 (const uint8_t *)"\1\2\3\7\7\4\5\6\7\7", 10);
    expect(buffer, length, rgb, 2, 1);
    length = pam("P7\nWIDTH 2\nHEIGHT 1\nDEPTH 3\nMAXVAL 255\n"
                 "TUPLTYPE GRAYSCALE\nENDHDR\n",
                 (const uint8_t *)"\x0a\1\2\xc8\3\4", 6);
    expect(buffer, length, gray, 2, 1);
    /* Repeated TUPLTYPE lines join with a space, giving an unknown type. */
    length = pam("P7\nWIDTH 2\nHEIGHT 1\nDEPTH 4\nMAXVAL 255\n"
                 "TUPLTYPE RGB\nTUPLTYPE _ALPHA\nENDHDR\n",
                 (const uint8_t *)"\1\2\3\x80\x09\4\5\0", 8);
    expect(buffer, length, rgb_extra, 2, 1);
    /* Other maxvals round to nearest, as netpbm's pamdepth does. */
    length = pam("P7\nWIDTH 2\nHEIGHT 1\nDEPTH 3\nMAXVAL 1000\n"
                 "TUPLTYPE RGB\nENDHDR\n",
                 (const uint8_t *)"\0\0\x01\xf4\x03\xe8\0\x02\x03\xe6\x03\xe7", 12);
    expect(buffer, length, scaled, 2, 1);
    expect_truncated_prefixes(buffer, length);
    length = pam("P7\nWIDTH 2\nHEIGHT 1\nDEPTH 1\nMAXVAL 7\n"
                 "TUPLTYPE GRAYSCALE\nENDHDR\n", (const uint8_t *)"\3\4", 2);
    expect(buffer, length, sevenths, 2, 1);
    /* Samples above maxval are clamped. */
    length = pam("P7\nWIDTH 2\nHEIGHT 1\nDEPTH 1\nMAXVAL 100\n"
                 "TUPLTYPE GRAYSCALE\nENDHDR\n", (const uint8_t *)"\x32\xc8", 2);
    expect(buffer, length, clamped, 2, 1);
    length = pam("P7\nWIDTH 2\nHEIGHT 1\nDEPTH 3\nMAXVAL 65535\n"
                 "TUPLTYPE RGB\nENDHDR\n",
                 (const uint8_t *)"\x12\x34\x80\x00\xff\xff\x00\x80\x01\x00\xfe\x00", 12);
    expect(buffer, length, wide, 2, 1);
}

static void test_pam_header_syntax(void)
{
    const uint8_t gray[4] = {16,16,16,255};
    size_t length;

    /* Comments, blank lines, tabs, CR and unknown keywords are accepted. */
    length = pam("P7\n# comment\n\n  WIDTH\t1 \r\nHEIGHT 1\n#x\nDEPTH 1\n"
                 "IGNORED keyword\nMAXVAL 255\nTUPLTYPE GRAYSCALE\nENDHDR\r\n",
                 (const uint8_t *)"\x10", 1);
    expect(buffer, length, gray, 1, 1);
    length = pam("P7 WIDTH 1\nHEIGHT 1\nDEPTH 1\nMAXVAL 255\nENDHDR\n",
                 (const uint8_t *)"\x10", 1);
    expect(buffer, length, gray, 1, 1);
    /* A later value replaces an earlier one. */
    length = pam("P7\nWIDTH 9\nWIDTH 1\nHEIGHT 1\nDEPTH 1\nMAXVAL 255\nENDHDR\n",
                 (const uint8_t *)"\x10", 1);
    expect(buffer, length, gray, 1, 1);
    /* Bytes after the raster are ignored. */
    length = pam("P7\nWIDTH 1\nHEIGHT 1\nDEPTH 1\nMAXVAL 255\nENDHDR\n",
                 (const uint8_t *)"\x10garbage", 8);
    expect(buffer, length, gray, 1, 1);
    assert(pam_count(buffer, length) == 1);

    assert(decode_header("P7\nHEIGHT 1\nDEPTH 1\nMAXVAL 255\nENDHDR\n", 1) == CODEC_INVALID);
    assert(decode_header("P7\nWIDTH 1\nDEPTH 1\nMAXVAL 255\nENDHDR\n", 1) == CODEC_INVALID);
    assert(decode_header("P7\nWIDTH 1\nHEIGHT 1\nMAXVAL 255\nENDHDR\n", 1) == CODEC_INVALID);
    assert(decode_header("P7\nWIDTH 1\nHEIGHT 1\nDEPTH 1\nENDHDR\n", 1) == CODEC_INVALID);
    assert(decode_header("P7\nWIDTH 0\nHEIGHT 1\nDEPTH 1\nMAXVAL 255\nENDHDR\n", 1) == CODEC_INVALID);
    assert(decode_header("P7\nWIDTH 1\nHEIGHT 0\nDEPTH 1\nMAXVAL 255\nENDHDR\n", 1) == CODEC_INVALID);
    assert(decode_header("P7\nWIDTH 1\nHEIGHT 1\nDEPTH 0\nMAXVAL 255\nENDHDR\n", 1) == CODEC_INVALID);
    assert(decode_header("P7\nWIDTH 1\nHEIGHT 1\nDEPTH 1\nMAXVAL 0\nENDHDR\n", 1) == CODEC_INVALID);
    assert(decode_header("P7\nWIDTH 1\nHEIGHT 1\nDEPTH 1\nMAXVAL 65536\nENDHDR\n", 2) == CODEC_INVALID);
    assert(decode_header("P7\nWIDTH -1\nHEIGHT 1\nDEPTH 1\nMAXVAL 255\nENDHDR\n", 1) == CODEC_INVALID);
    assert(decode_header("P7\nWIDTH 1x\nHEIGHT 1\nDEPTH 1\nMAXVAL 255\nENDHDR\n", 1) == CODEC_INVALID);
    assert(decode_header("P7\nWIDTH\nHEIGHT 1\nDEPTH 1\nMAXVAL 255\nENDHDR\n", 1) == CODEC_INVALID);
    assert(decode_header("P7\nWIDTH 1\nHEIGHT 1\nDEPTH 1\nMAXVAL 255\nENDHDR x\n", 1) == CODEC_INVALID);
    assert(decode_header("P7\nwidth 1\nHEIGHT 1\nDEPTH 1\nMAXVAL 255\nENDHDR\n", 1) == CODEC_INVALID);
    assert(decode_header("P7x\nWIDTH 1\nHEIGHT 1\nDEPTH 1\nMAXVAL 255\nENDHDR\n", 1) == CODEC_INVALID);
    /* An XV thumbnail is not PAM. */
    assert(decode_header("P7 332\n#XVVERSION:Version 2.28\n#END_OF_COMMENTS\n1 1 255\n", 1)
           == CODEC_TRUNCATED);
    /* Known tuple types need their planes. */
    assert(decode_header("P7\nWIDTH 1\nHEIGHT 1\nDEPTH 1\nMAXVAL 255\n"
                         "TUPLTYPE GRAYSCALE_ALPHA\nENDHDR\n", 2) == CODEC_INVALID);
    assert(decode_header("P7\nWIDTH 1\nHEIGHT 1\nDEPTH 3\nMAXVAL 255\n"
                         "TUPLTYPE RGB_ALPHA\nENDHDR\n", 4) == CODEC_INVALID);
    assert(decode_header("P7\nWIDTH 1\nHEIGHT 1\nDEPTH 2\nMAXVAL 255\n"
                         "TUPLTYPE RGB\nENDHDR\n", 3) == CODEC_INVALID);
    assert(decode_header("P7\nWIDTH 1\nHEIGHT 1\nDEPTH 4\nMAXVAL 255\n"
                         "TUPLTYPE CMYK_ALPHA\nENDHDR\n", 5) == CODEC_INVALID);
    /* A tuple type too long to be a known one is just unknown. */
    assert(decode_header("P7\nWIDTH 1\nHEIGHT 1\nDEPTH 1\nMAXVAL 255\nTUPLTYPE "
                         "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
                         "TUPLTYPE B\nENDHDR\n", 1) == CODEC_OK);
    /* Limits: 65535 per side, 16M pixels, 65535 planes. */
    assert(decode_header("P7\nWIDTH 65536\nHEIGHT 1\nDEPTH 1\nMAXVAL 255\nENDHDR\n", 0) == CODEC_TOO_LARGE);
    assert(decode_header("P7\nWIDTH 1\nHEIGHT 99999999999999999999\nDEPTH 1\nMAXVAL 255\nENDHDR\n", 0) == CODEC_TOO_LARGE);
    assert(decode_header("P7\nWIDTH 4097\nHEIGHT 4096\nDEPTH 1\nMAXVAL 255\nENDHDR\n", 0) == CODEC_TOO_LARGE);
    assert(decode_header("P7\nWIDTH 65535\nHEIGHT 65535\nDEPTH 1\nMAXVAL 255\nENDHDR\n", 0) == CODEC_TOO_LARGE);
    assert(decode_header("P7\nWIDTH 1\nHEIGHT 1\nDEPTH 65536\nMAXVAL 255\nENDHDR\n", 0) == CODEC_TOO_LARGE);
    /* Sizes that would overflow a 32-bit size_t are just truncated data. */
    assert(decode_header("P7\nWIDTH 4096\nHEIGHT 4096\nDEPTH 65535\nMAXVAL 65535\nENDHDR\n", 16) == CODEC_TRUNCATED);
    assert(decode_header("P7\nWIDTH 4096\nHEIGHT 4096\nDEPTH 1\nMAXVAL 255\nENDHDR\n", 16) == CODEC_TRUNCATED);
    assert(decode_header("P7\nWIDTH 1\nHEIGHT 1\nDEPTH 1\nMAXVAL 255\n", 0) == CODEC_TRUNCATED);
    assert(decode_header("P7\nWIDTH 1\nHEIGHT 1\nDEPTH 1\nMAXVAL 255\nENDHDR", 0) == CODEC_TRUNCATED);
}

static void test_float_maps(void)
{
    /* Rows are stored bottom up. */
    const float colour[12] = {0.0f,0.5f,1.0f, 1.0f,0.0f,0.0f,
                              0.0f,1.0f,0.0f, 0.25f,0.25f,0.25f};
    const uint8_t colour_rgba[16] = {0,255,0,255, 137,137,137,255,
                                     0,188,255,255, 255,0,0,255};
    const float with_alpha[16] = {0.0f,0.5f,1.0f,0.5f, 1.0f,0.0f,0.0f,1.0f,
                                  0.0f,1.0f,0.0f,0.0f, 0.25f,0.25f,0.25f,1.0f};
    const uint8_t with_alpha_rgba[16] = {0,255,0,0, 137,137,137,255,
                                         0,188,255,128, 255,0,0,255};
    /* Clamping, the linear toe of the curve, and non-finite values. */
    const float gray[8] = {-1.0f, 0.0031308f, 0.0001f, 0.18f, 0.99f, 2.0f,
                           1.0f / 0.0f, 0.0f / 0.0f};
    const uint8_t gray_rgba[32] = {0,0,0,255, 10,10,10,255, 0,0,0,255,
                                   118,118,118,255, 254,254,254,255,
                                   255,255,255,255, 255,255,255,255, 0,0,0,255};
    const float zero_alpha[4] = {1.0f, 1.0f, 1.0f, 0.0f};
    const uint8_t white[4] = {255,255,255,0};
    size_t length;

    length = pfm("PF\n2 2\n-1.0\n", colour, 12, 1);
    expect(buffer, length, colour_rgba, 2, 2);
    expect_truncated_prefixes(buffer, length);
    length = pfm("PF\n2 2\n1.0\n", colour, 12, 0);
    expect(buffer, length, colour_rgba, 2, 2);
    /* The scale's size is ignored; only its sign matters. */
    length = pfm("PF\n2 2\n-4.5e1\n", colour, 12, 1);
    expect(buffer, length, colour_rgba, 2, 2);
    length = pfm("PF\n2 2\n+0.5\n", colour, 12, 0);
    expect(buffer, length, colour_rgba, 2, 2);
    length = pfm("PF4\n2 2\n-1.0\n", with_alpha, 16, 1);
    expect(buffer, length, with_alpha_rgba, 2, 2);
    expect_truncated_prefixes(buffer, length);
    length = pfm("PF4\n1 1\n-1.0\n", zero_alpha, 4, 1);
    expect(buffer, length, white, 1, 1);
    assert(decode(buffer, pfm("PF\n1 1\n0.0\n", colour, 3, 0), 0) == CODEC_INVALID);
    length = pfm("Pf\n8 1\n-1.0\n", gray, 8, 1);
    expect(buffer, length, gray_rgba, 8, 1);
    /* Any whitespace separates the numbers, and one byte ends the header. */
    length = pfm("Pf \t8\r\n\n1 -1 ", gray, 8, 1);
    expect(buffer, length, gray_rgba, 8, 1);

    length = pfm("Pf\n1 1\n-1.0\n", gray, 1, 1);
    assert(decode(buffer, length, 0) == CODEC_OK);
    assert(decode(buffer, pfm("Pf\n1 1\n-1.0x\n", gray, 1, 1), 0) == CODEC_INVALID);
    assert(decode(buffer, pfm("Pf\n1 1\n-\n", gray, 1, 1), 0) == CODEC_INVALID);
    assert(decode(buffer, pfm("Pf\n1 1\n.\n", gray, 1, 1), 0) == CODEC_INVALID);
    assert(decode(buffer, pfm("Pf\n1 1\n1e\n", gray, 1, 1), 0) == CODEC_INVALID);
    assert(decode(buffer, pfm("Pf\n1 1\nnan\n", gray, 1, 1), 0) == CODEC_INVALID);
    assert(decode(buffer, pfm("Pf\n# c\n1 1\n-1.0\n", gray, 1, 1), 0) == CODEC_INVALID);
    assert(decode(buffer, pfm("Pf\n0 1\n-1.0\n", gray, 1, 1), 0) == CODEC_INVALID);
    assert(decode(buffer, pfm("Pf\n1 -1\n-1.0\n", gray, 1, 1), 0) == CODEC_INVALID);
    assert(decode(buffer, pfm("Pf1 1\n-1.0\n", gray, 1, 1), 0) == CODEC_INVALID);
    assert(decode(buffer, pfm("PF5\n1 1\n-1.0\n", gray, 3, 1), 0) == CODEC_INVALID);
    assert(decode(buffer, pfm("Pf4\n1 1\n-1.0\n", gray, 4, 1), 0) == CODEC_INVALID);
    assert(decode(buffer, pfm("Pf\n65536 1\n-1.0\n", gray, 1, 1), 0) == CODEC_TOO_LARGE);
    assert(decode(buffer, pfm("PF4\n4096 4096\n-1.0\n", gray, 1, 1), 0) == CODEC_TRUNCATED);
    assert(decode(buffer, pfm("PF\n2 2\n-1.0\n", colour, 11, 1), 0) == CODEC_TRUNCATED);
}

static size_t raw(const char *header, const uint8_t *raster, size_t raster_length)
{
    return pam(header, raster, raster_length);
}

static void test_half_maps(void)
{
    /* 0, 0.5, 1, subnormal, -2, infinity, NaN, 0.25 as little-endian halves. */
    const uint8_t gray[16] = {0x00,0x00, 0x00,0x38, 0x00,0x3c, 0x01,0x00,
                              0x00,0xc0, 0x00,0x7c, 0x01,0x7e, 0x00,0x34};
    const uint8_t gray_rgba[32] = {0,0,0,255, 188,188,188,255, 255,255,255,255,
                                   0,0,0,255, 0,0,0,255, 255,255,255,255,
                                   0,0,0,255, 137,137,137,255};
    /* Big endian, bottom row (0, 0.5, 1) then top row (0, 1, 0.25). */
    const uint8_t colour[12] = {0x00,0x00, 0x38,0x00, 0x3c,0x00,
                                0x00,0x00, 0x3c,0x00, 0x34,0x00};
    const uint8_t colour_rgba[8] = {0,255,137,255, 0,188,255,255};
    size_t length;

    length = raw("Ph\n8 1\n-1\n", gray, sizeof gray);
    expect(buffer, length, gray_rgba, 8, 1);
    expect_truncated_prefixes(buffer, length);
    length = raw("PH\n1 2\n1\n", colour, sizeof colour);
    expect(buffer, length, colour_rgba, 1, 2);
    expect_truncated_prefixes(buffer, length);
}

static void test_streams(void)
{
    static const char first[] = "P7\nWIDTH 1\nHEIGHT 1\nDEPTH 1\nMAXVAL 255\n"
                                "TUPLTYPE GRAYSCALE\nENDHDR\n\x10";
    static const char second[] = "P7\nWIDTH 2\nHEIGHT 1\nDEPTH 3\nMAXVAL 255\n"
                                 "TUPLTYPE RGB\nENDHDR\n\1\2\3\4\5\6";
    static const char third[] = "\n\nPf\n1 1\n1\n\x3f\x80\0\0";
    const uint8_t gray[4] = {16,16,16,255};
    const uint8_t rgb[8] = {1,2,3,255, 4,5,6,255};
    const uint8_t white[4] = {255,255,255,255};
    uint8_t stream[256];
    size_t first_length = sizeof first - 1, second_length = sizeof second - 1;
    size_t length = first_length + second_length, i;

    memcpy(stream, first, first_length);
    memcpy(stream + first_length, second, second_length);
    assert(pam_count(stream, length) == 2);
    expect_image(stream, length, 0, gray, 1, 1);
    expect_image(stream, length, 1, rgb, 2, 1);
    assert(decode(stream, length, 2) == CODEC_INVALID);
    assert(decode(stream, length, 65535) == CODEC_INVALID);
    /* A truncated later image leaves the earlier ones. */
    for (i = first_length; i < length; i++) {
        assert(pam_count(stream, i) == 1);
        expect_image(stream, i, 0, gray, 1, 1);
        assert(decode(stream, i, 1) ==
               (i == first_length ? CODEC_INVALID : CODEC_TRUNCATED));
    }
    /* Whitespace between images, then a big-endian float map. */
    memcpy(stream + length, third, sizeof third - 1);
    length += sizeof third - 1;
    assert(pam_count(stream, length) == 3);
    expect_image(stream, length, 2, white, 1, 1);
    /* Trailing garbage ends the count but doesn't spoil earlier images. */
    memcpy(stream + length, "junk", 4);
    assert(pam_count(stream, length + 4) == 3);
    expect_image(stream, length + 4, 2, white, 1, 1);
    assert(decode(stream, length + 4, 3) == CODEC_INVALID);
    /* A bad first image means nothing loads. */
    stream[1] = '8';
    assert(pam_count(stream, length) == 0);
    assert(decode(stream, length, 0) == CODEC_INVALID);
    assert(decode(stream, length, 1) == CODEC_INVALID);

    assert(pam_count(NULL, 0) == 0);
    assert(pam_count(stream, 0) == 0);
    assert(decode(NULL, 0, 0) == CODEC_TRUNCATED);
    assert(decode((const uint8_t *)"P", 1, 0) == CODEC_TRUNCATED);
    assert(decode((const uint8_t *)"Q7\n", 3, 0) == CODEC_INVALID);
    assert(decode((const uint8_t *)"P6\n", 3, 0) == CODEC_INVALID);
    assert(pam_decode(stream, 0, 0, NULL) == CODEC_INVALID);
}

static void round_trip(const uint8_t *rgba, unsigned width, unsigned height,
                       unsigned channels, const char *type)
{
    uint8_t file[1024], row[64];
    size_t length, size;
    unsigned needs = 0, y;
    char expect_type[64];

    for (y = 0; y < height; y++)
        needs |= pam_row_needs(rgba + (size_t)y * width * 4u, width);
    assert(pam_channels(needs) == channels);
    length = pam_make_header(width, height, channels, file, PAM_HEADER_CAPACITY);
    assert(length != 0);
    snprintf(expect_type, sizeof expect_type, "TUPLTYPE %s\nENDHDR\n", type);
    assert(memcmp(file + length - strlen(expect_type), expect_type,
                  strlen(expect_type)) == 0);
    for (y = 0; y < height; y++) {
        size = pam_encode_row(rgba + (size_t)y * width * 4u, width, channels,
                              row, sizeof row);
        assert(size == (size_t)width * channels);
        memcpy(file + length, row, size);
        length += size;
    }
    expect(file, length, rgba, width, height);
}

static void test_encoder(void)
{
    const uint8_t gray[16] = {0,0,0,255, 9,9,9,255, 128,128,128,255, 255,255,255,255};
    const uint8_t gray_alpha[16] = {0,0,0,255, 9,9,9,0, 128,128,128,7, 255,255,255,255};
    const uint8_t rgb[16] = {1,2,3,255, 9,9,9,255, 128,128,128,255, 255,0,255,255};
    const uint8_t rgba[16] = {1,2,3,255, 9,9,9,1, 128,128,128,255, 255,0,255,0};
    uint8_t header[PAM_HEADER_CAPACITY], row[8];

    round_trip(gray, 2, 2, 1, "GRAYSCALE");
    round_trip(gray_alpha, 4, 1, 2, "GRAYSCALE_ALPHA");
    round_trip(rgb, 1, 4, 3, "RGB");
    round_trip(rgba, 2, 2, 4, "RGB_ALPHA");
    assert(pam_make_header(65535, 65535, 4, header, sizeof header) != 0);
    assert(pam_make_header(65535, 65535, 4, header, 20) == 0);
    assert(pam_make_header(0, 1, 1, header, sizeof header) == 0);
    assert(pam_make_header(1, 1, 5, header, sizeof header) == 0);
    assert(pam_encode_row(rgba, 4, 3, row, sizeof row) == 0);
    assert(pam_encode_row(rgba, 2, 4, row, sizeof row) == 8);
    assert(pam_encode_row(rgba, 2, 0, row, sizeof row) == 0);
}

int main(void)
{
    test_pam_tuple_types();
    test_pam_alpha();
    test_pam_depths();
    test_pam_header_syntax();
    test_float_maps();
    test_half_maps();
    test_streams();
    test_encoder();
    puts("pam tests passed");
    return 0;
}
