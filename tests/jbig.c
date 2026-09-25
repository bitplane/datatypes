#include "../formats/jbig/decode.h"
#include "../formats/jbig/dptable.h"
#include "../formats/jbig/encode.h"
#include "../formats/jbig/qm.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Made by netpbm's pnmtojbig and by JBIG-KIT's encoder from the patterns
   below: progressive coding with TP and DP, higher layers first with the
   two-line template, SDRST after every stripe, an AT move, and Gray-coded
   planes. */
static const uint8_t progressive[179] = {
    0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x25, 0x00, 0x00, 0x00, 0x1d,
    0x00, 0x00, 0x00, 0x02, 0x08, 0x00, 0x03, 0x1c, 0xf8, 0xae, 0xff, 0x02,
    0xdb, 0x4e, 0x80, 0xff, 0x02, 0xd4, 0xf7, 0x7a, 0xff, 0x02, 0x3d, 0xcd,
    0xf4, 0xff, 0x02, 0x32, 0x5e, 0xb1, 0x1e, 0x68, 0x25, 0x33, 0x80, 0xff,
    0x02, 0x00, 0xb6, 0x16, 0x89, 0xb7, 0x94, 0x28, 0xcf, 0xa6, 0xff, 0x02,
    0x14, 0x8d, 0x72, 0x3f, 0x28, 0x22, 0xca, 0x20, 0xff, 0x02, 0x26, 0x66,
    0x89, 0x5e, 0xc1, 0x81, 0xf0, 0xff, 0x02, 0x0d, 0x3b, 0xbc, 0x52, 0xfe,
    0x6d, 0x07, 0xef, 0xe9, 0x0c, 0x99, 0x9a, 0x5d, 0x65, 0x3e, 0xe0, 0x2a,
    0x80, 0x66, 0x86, 0xd6, 0xff, 0x02, 0x7d, 0xe3, 0x30, 0xfa, 0xff, 0x00,
    0xbc, 0xb4, 0xc1, 0xda, 0x40, 0x9a, 0x1e, 0xc0, 0xb1, 0xf2, 0xf3, 0x10,
    0xda, 0xa0, 0x61, 0x4b, 0x9a, 0x12, 0x43, 0x82, 0xbe, 0x0e, 0x8c, 0x71,
    0x93, 0x03, 0x30, 0xff, 0x02, 0xcd, 0x24, 0x33, 0xb2, 0xb0, 0xc2, 0x33,
    0x2a, 0x30, 0xbe, 0xe6, 0xb8, 0x80, 0x16, 0xe3, 0x44, 0x02, 0xbb, 0x08,
    0x29, 0x2b, 0x46, 0xf3, 0x44, 0x07, 0xcd, 0xf7, 0xcc, 0xff, 0x02, 0x09,
    0xde, 0x45, 0xed, 0xe3, 0x45, 0xd4, 0x0a, 0xda, 0x90, 0xff, 0x02,
};
static const uint8_t hitolo_seq_two_line[173] = {
    0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x25, 0x00, 0x00, 0x00, 0x1d,
    0x00, 0x00, 0x00, 0x03, 0x08, 0x00, 0x0c, 0x5c, 0x0d, 0x3b, 0xbc, 0x52,
    0xfe, 0x6d, 0x07, 0xef, 0xe9, 0x0c, 0x99, 0x9a, 0x85, 0x33, 0x85, 0x36,
    0x9d, 0x46, 0x34, 0xff, 0x02, 0xdc, 0x98, 0x27, 0x18, 0xc9, 0xc5, 0x58,
    0xff, 0x02, 0x69, 0x3c, 0x4f, 0x88, 0xf3, 0x97, 0x98, 0x1a, 0x61, 0xf5,
    0xff, 0x00, 0x6d, 0xb6, 0xf7, 0x8c, 0xad, 0x65, 0x5d, 0xff, 0x00, 0x31,
    0x8f, 0xa0, 0xff, 0x02, 0x5b, 0x68, 0x31, 0x5a, 0xbb, 0x17, 0xff, 0x02,
    0x9f, 0xac, 0x88, 0xb3, 0x94, 0x3f, 0x9b, 0x0d, 0x12, 0xb0, 0x10, 0x36,
    0xcc, 0x5b, 0x54, 0xc6, 0x1a, 0xe3, 0xad, 0x90, 0xe3, 0x4e, 0xff, 0x02,
    0x8c, 0x11, 0x1d, 0x4e, 0xf4, 0xd1, 0x4f, 0xff, 0x02, 0x37, 0x7c, 0xf3,
    0xd7, 0xa8, 0x09, 0xf4, 0xd0, 0x28, 0x19, 0x80, 0x1f, 0x86, 0xc2, 0x7c,
    0xf5, 0xcb, 0x67, 0x56, 0xea, 0xc9, 0x40, 0xff, 0x02, 0x1b, 0x01, 0x99,
    0xc9, 0x31, 0x1a, 0xc0, 0xff, 0x02, 0x48, 0x31, 0xa7, 0x5b, 0x08, 0xed,
    0x25, 0x4f, 0x07, 0x25, 0xa8, 0x24, 0xff, 0x02, 0x6b, 0x47, 0x90, 0x4b,
    0x7f, 0x4c, 0xc0, 0xff, 0x02,
};
static const uint8_t stripe_resets[181] = {
    0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x25, 0x00, 0x00, 0x00, 0x1d,
    0x00, 0x00, 0x00, 0x02, 0x08, 0x00, 0x00, 0x1c, 0xf8, 0xae, 0xff, 0x03,
    0xd2, 0x83, 0x7a, 0x80, 0xff, 0x03, 0xd2, 0x87, 0x68, 0x80, 0xff, 0x03,
    0xb8, 0xbb, 0x60, 0xff, 0x03, 0x32, 0x5e, 0xb1, 0x1e, 0x68, 0x25, 0x33,
    0x80, 0xff, 0x03, 0x00, 0x65, 0x7a, 0x66, 0xef, 0xad, 0x94, 0xe8, 0xff,
    0x03, 0x40, 0x81, 0x56, 0xed, 0xfb, 0xf0, 0xd9, 0x9e, 0x20, 0xff, 0x03,
    0x6f, 0x53, 0x03, 0x89, 0x03, 0xff, 0x03, 0x0d, 0x3b, 0xbc, 0x52, 0xfe,
    0x6d, 0x07, 0xef, 0xe9, 0x0c, 0x99, 0x9a, 0x5d, 0x65, 0x3e, 0xe0, 0x2a,
    0x80, 0x66, 0x86, 0xd6, 0xff, 0x03, 0xbf, 0xd0, 0x35, 0x72, 0x71, 0xc3,
    0x7d, 0x97, 0x42, 0x33, 0xb6, 0x56, 0x49, 0x04, 0xe3, 0xfe, 0xcd, 0x80,
    0xb3, 0x94, 0x39, 0xf2, 0x12, 0xa4, 0xb8, 0xc3, 0xb8, 0xe5, 0x7e, 0xf8,
    0x40, 0xff, 0x03, 0x47, 0xb1, 0x9e, 0x2b, 0xfd, 0xcd, 0x05, 0xaf, 0x04,
    0xbf, 0x57, 0x02, 0x74, 0x10, 0x8d, 0x0f, 0x91, 0x49, 0x7b, 0x56, 0x06,
    0x14, 0xd4, 0xe4, 0xb6, 0xfa, 0x74, 0xd4, 0x40, 0xff, 0x03, 0xa7, 0xce,
    0x32, 0x7f, 0x4d, 0x63, 0x19, 0xcb, 0x9f, 0x9b, 0x18, 0x84, 0xe6, 0xff,
    0x03,
};
static const uint8_t at_moves[49] = {
    0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x78, 0x00, 0x00, 0x00, 0x3c,
    0xff, 0xff, 0xff, 0xff, 0x10, 0x00, 0x03, 0x00, 0xff, 0x06, 0x00, 0x00,
    0x00, 0x15, 0x08, 0x00, 0x8b, 0x58, 0x84, 0x72, 0x7b, 0xbb, 0xbb, 0xbb,
    0x9d, 0xc8, 0xc3, 0x8f, 0x63, 0x20, 0x00, 0x00, 0x03, 0x55, 0x60, 0xff,
    0x02,
};
static const uint8_t three_planes[109] = {
    0x00, 0x01, 0x03, 0x00, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x0d,
    0x00, 0x00, 0x00, 0x04, 0x08, 0x00, 0x00, 0x1c, 0xcd, 0x12, 0x6a, 0x17,
    0x2a, 0xa0, 0xff, 0x02, 0x3e, 0x80, 0xff, 0x02, 0x60, 0xea, 0xba, 0x3f,
    0x4d, 0xb8, 0x48, 0xa7, 0xd0, 0x1c, 0xce, 0x40, 0xff, 0x02, 0x00, 0x01,
    0xa0, 0xff, 0x02, 0xfe, 0x0f, 0x68, 0xec, 0xe3, 0xff, 0x02, 0x00, 0xc0,
    0xff, 0x02, 0x76, 0x1e, 0xb8, 0xef, 0xd3, 0x3c, 0x50, 0xe6, 0x27, 0x8d,
    0xec, 0xff, 0x02, 0x00, 0x40, 0x30, 0xff, 0x02, 0xf8, 0xda, 0x90, 0x56,
    0xff, 0x02, 0x4f, 0x4f, 0x40, 0xff, 0x02, 0x93, 0x96, 0x23, 0x51, 0x11,
    0x0e, 0xd7, 0x27, 0x40, 0xff, 0x02, 0x01, 0x9e, 0x18, 0x3e, 0x20, 0xff,
    0x02,
};
static const uint8_t ten_planes[167] = {
    0x00, 0x01, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x05,
    0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x03, 0x1c, 0xff, 0x02, 0xa4, 0xff,
    0x02, 0xcb, 0xff, 0x02, 0xfe, 0x80, 0xff, 0x02, 0xd0, 0x80, 0xff, 0x02,
    0xe1, 0xff, 0x02, 0xcd, 0xe0, 0xff, 0x02, 0xe0, 0xc0, 0xff, 0x02, 0xe1,
    0xff, 0x02, 0xe0, 0xc0, 0xff, 0x02, 0xfd, 0xff, 0x02, 0xff, 0x02, 0x40,
    0xff, 0x02, 0xa8, 0xff, 0x02, 0xe0, 0xff, 0x02, 0xb0, 0xff, 0x02, 0xa8,
    0xff, 0x02, 0xb0, 0xff, 0x02, 0xc0, 0xff, 0x02, 0xd0, 0xff, 0x02, 0x21,
    0xff, 0x02, 0x68, 0xbd, 0x38, 0xff, 0x02, 0x60, 0xd9, 0x42, 0xff, 0x02,
    0x76, 0xc3, 0x69, 0x80, 0xff, 0x02, 0x0d, 0xc6, 0x06, 0xff, 0x02, 0x8c,
    0x10, 0x1e, 0x60, 0xff, 0x02, 0x9f, 0xae, 0x23, 0x80, 0xff, 0x02, 0xa2,
    0xa3, 0x6d, 0x80, 0xff, 0x02, 0x9c, 0xf0, 0x8f, 0x60, 0xff, 0x02, 0x9a,
    0x0e, 0xa0, 0xff, 0x02, 0xf4, 0x80, 0xff, 0x02, 0xd8, 0xff, 0x02, 0x28,
    0xff, 0x02, 0xb8, 0xff, 0x02, 0x0c, 0xff, 0x02, 0x90, 0xff, 0x02, 0x84,
    0xff, 0x02, 0x94, 0xff, 0x02, 0x88, 0xff, 0x02, 0x0a, 0xff, 0x02,
};

#define ESC 0xff
#define SPEC_WIDTH 1960u
#define SPEC_HEIGHT 1951u
#define SPEC_STRIDE ((SPEC_WIDTH + 7u) / 8u)

static uint8_t buffer[1 << 16];

static int pattern(unsigned x, unsigned y)
{
    return ((x * x + 3u * y) % 7u < 2u) != (x >= 12u && x < 30u && y >= 8u && y < 22u);
}

static int periodic(unsigned x, unsigned y)
{
    (void)y;
    return x % 8u == 3u;
}

static void expect_bits(const uint8_t *data, size_t length, unsigned width, unsigned height,
                        int (*expected)(unsigned, unsigned))
{
    struct jbig_image image;
    unsigned x, y;
    assert(jbig_decode(data, length, &image) == CODEC_OK);
    assert(image.width == width && image.height == height && image.planes == 1);
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++)
            assert(image.pixels[(size_t)y * width + x] == expected(x, y));
    jbig_free(&image);
}

static void expect_error(const uint8_t *data, size_t length, enum codec_result expected)
{
    struct jbig_image image;
    assert(jbig_decode(data, length, &image) == expected);
    assert(image.pixels == NULL && image.width == 0 && image.height == 0);
}

static void put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

/* A copy of data with extra bytes put in at offset. */
static size_t splice(const uint8_t *data, size_t length, size_t offset,
                     const uint8_t *extra, size_t extra_length)
{
    memcpy(buffer, data, offset);
    memcpy(buffer + offset, extra, extra_length);
    memcpy(buffer + offset + extra_length, data + offset, length - offset);
    return length + extra_length;
}

/* Bytes written by the encoder, and the stuffing among them. */
struct collected { uint8_t *data; size_t length, capacity, stuffed; int fail_after; };

static int collect(void *state, uint8_t byte)
{
    struct collected *c = state;
    if (c->fail_after >= 0 && c->length >= (size_t)c->fail_after)
        return 0;
    assert(c->length < c->capacity);
    if (byte == 0 && c->length > 0 && c->data[c->length - 1] == 0xff)
        c->stuffed++;
    c->data[c->length++] = byte;
    return 1;
}

static void unhex(const char *hex, uint8_t *out, size_t length)
{
    size_t i = 0;
    unsigned value;
    for (; *hex != '\0' && i < length; hex++) {
        if (*hex == ' ')
            continue;
        assert(sscanf(hex, "%2x", &value) == 1);
        out[i++] = (uint8_t)value;
        hex++;
    }
    assert(i == length);
}

/* The arithmetic coder test of T.82 7.1. */
static void test_arithmetic_coder(void)
{
    uint8_t pix[32], cx[32], pscd[30], out[64], contexts[2];
    struct collected c = { out, 0, sizeof out, 0, -1 };
    struct qm_encoder e;
    struct qm_decoder d;
    unsigned i;

    unhex("05e0 0000 8b00 01c4 1700 0034 7fff 1a3f 951b 05d8 1d17 e770 0000 0000 0656 0e6a",
          pix, sizeof pix);
    unhex("0fe0 0000 0f00 00f0 ff00 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000",
          cx, sizeof cx);
    unhex("6989 995c 32ea faa0 d5ff 0052 7fff 00ff 00ff 00c0 0000 003f ff00 2d20 8291",
          pscd, sizeof pscd);
    memset(contexts, 0, sizeof contexts);
    qm_encode_init(&e, collect, &c);
    for (i = 0; i < 256; i++)
        qm_encode(&e, &contexts[cx[i / 8] >> (7 - i % 8) & 1], pix[i / 8] >> (7 - i % 8) & 1);
    assert(qm_encode_flush(&e));
    assert(c.length == sizeof pscd && memcmp(out, pscd, sizeof pscd) == 0);

    memset(contexts, 0, sizeof contexts);
    qm_decode_init(&d, pscd, sizeof pscd);
    for (i = 0; i < 256; i++)
        assert(qm_decode(&d, &contexts[cx[i / 8] >> (7 - i % 8) & 1]) ==
               (pix[i / 8] >> (7 - i % 8) & 1));
}

static void test_default_dp_table(void)
{
    static const unsigned sizes[4] = { 256, 512, 2048, 4096 }, hits[4] = { 20, 108, 526, 1044 };
    unsigned phase, i, entry = 0;
    for (phase = 0; phase < 4; phase++) {
        unsigned count = 0;
        for (i = 0; i < sizes[phase]; i++, entry++) {
            unsigned value = jbig_default_dp[entry / 4] >> (6 - 2 * (entry % 4)) & 3u;
            assert(value <= 2);
            count += value != 2;
        }
        assert(count == hits[phase]);
    }
}

/* The artificial test image of T.82 Figure 38. */
static void make_spec_image(uint8_t *bits)
{
    unsigned prsg = 1, i, j;
    int repeat[8] = { 0 };
    memset(bits, 0, (size_t)SPEC_STRIDE * SPEC_HEIGHT);
    for (j = 0; j < SPEC_HEIGHT; j++)
        for (i = 0; i < SPEC_WIDTH; i++) {
            int pixel = 0;
            if (j >= 192 && (j < 1023 || (i >> 3 & 3u) == 0)) {
                unsigned sum = (prsg & 1u) + (prsg >> 2 & 1u) + (prsg >> 11 & 1u) + (prsg >> 15 & 1u);
                prsg = (prsg << 1 | (sum & 1u)) & 0xffffu;
                pixel = repeat[i & 7] = (prsg & 3u) == 0;
            } else if (j >= 192) {
                pixel = repeat[i & 7];
            }
            if (pixel)
                bits[(size_t)j * SPEC_STRIDE + i / 8] |= (uint8_t)(0x80u >> (i % 8));
        }
}

/* Encode the spec's image, check the byte counts of Table 29 where it gives
   them, and decode it back. */
static void spec_round_trip(const uint8_t *bits, unsigned options, size_t scd, size_t pscd)
{
    struct collected c;
    struct jbig_encoder e;
    struct jbig_image image;
    unsigned y, x;

    c.capacity = 600000;
    c.data = malloc(c.capacity);
    c.length = c.stuffed = 0;
    c.fail_after = -1;
    assert(c.data != NULL);
    jbig_make_header(c.data, SPEC_WIDTH, SPEC_HEIGHT, options);
    c.length = JBIG_HEADER_SIZE;
    assert(jbig_encoder_init(&e, SPEC_WIDTH, SPEC_HEIGHT, options, collect, &c));
    for (y = 0; y < SPEC_HEIGHT; y++)
        jbig_encode_bits(&e, bits + (size_t)y * SPEC_STRIDE);
    assert(jbig_encoder_end(&e));
    assert(c.data[c.length - 2] == ESC && c.data[c.length - 1] == 0x02);
    if (pscd != 0) {
        assert(c.length - JBIG_HEADER_SIZE - 2 == pscd);
        assert(pscd - c.stuffed == scd);
    }
    assert(jbig_decode(c.data, c.length, &image) == CODEC_OK);
    assert(image.width == SPEC_WIDTH && image.height == SPEC_HEIGHT);
    for (y = 0; y < SPEC_HEIGHT; y++)
        for (x = 0; x < SPEC_WIDTH; x++)
            assert(image.pixels[(size_t)y * SPEC_WIDTH + x] ==
                   (bits[(size_t)y * SPEC_STRIDE + x / 8] >> (7 - x % 8) & 1));
    jbig_free(&image);
    free(c.data);
}

static void test_spec_image(void)
{
    uint8_t *bits = malloc((size_t)SPEC_STRIDE * SPEC_HEIGHT);
    assert(bits != NULL);
    make_spec_image(bits);
    spec_round_trip(bits, 0, 316094, 317362);
    spec_round_trip(bits, JBIG_TWO_LINE, 315887, 317110);
    spec_round_trip(bits, JBIG_TYPICAL, 0, 0);
    spec_round_trip(bits, JBIG_TYPICAL | JBIG_TWO_LINE, 0, 0);
    free(bits);
}

static void test_vectors(void)
{
    struct jbig_image image;
    unsigned x, y;

    expect_bits(progressive, sizeof progressive, 37, 29, pattern);
    expect_bits(hitolo_seq_two_line, sizeof hitolo_seq_two_line, 37, 29, pattern);
    expect_bits(stripe_resets, sizeof stripe_resets, 37, 29, pattern);
    expect_bits(at_moves, sizeof at_moves, 120, 60, periodic);

    assert(jbig_decode(three_planes, sizeof three_planes, &image) == CODEC_OK);
    assert(image.width == 19 && image.height == 13 && image.planes == 3);
    for (y = 0; y < 13; y++)
        for (x = 0; x < 19; x++)
            assert(image.pixels[y * 19 + x] == ((x + 2 * y) % 8 * 255 + 3) / 7);
    jbig_free(&image);

    assert(jbig_decode(ten_planes, sizeof ten_planes, &image) == CODEC_OK);
    assert(image.width == 7 && image.height == 5 && image.planes == 10);
    for (y = 0; y < 5; y++)
        for (x = 0; x < 7; x++)
            assert(image.pixels[y * 7 + x] == ((x * 37 + y * 101) % 1024 * 255 + 511) / 1023);
    jbig_free(&image);
}

/* Structures the reference encoders don't write. */
static void test_optional_structures(void)
{
    static const uint8_t floating[] = {
        ESC, 0x07, 0, 0, 0, 3, ESC, ESC, 0x02,  /* a comment holding markers */
        ESC, 0x06, 0, 0, 0, 0, 0, 0,            /* AT to its default place */
    };
    static const uint8_t trailing[] = { 0x12, ESC, 0x00, 0x34 };
    uint8_t table[JBIG_DP_TABLE_SIZE];
    size_t length;

    /* The default DP table given as a private one, and DPLAST without one. */
    length = splice(progressive, sizeof progressive, JBIG_HEADER_SIZE,
                    jbig_default_dp, JBIG_DP_TABLE_SIZE);
    buffer[19] |= 0x02;
    expect_bits(buffer, length, 37, 29, pattern);
    expect_error(buffer, JBIG_HEADER_SIZE + JBIG_DP_TABLE_SIZE - 1, CODEC_TRUNCATED);
    memcpy(buffer, progressive, sizeof progressive);
    buffer[19] |= 0x03;
    expect_bits(buffer, sizeof progressive, 37, 29, pattern);
    /* A private table that never predicts: the data no longer fits it. */
    memset(table, 0xaa, sizeof table);
    length = splice(progressive, sizeof progressive, JBIG_HEADER_SIZE, table, sizeof table);
    buffer[19] |= 0x02;
    {
        struct jbig_image image;
        if (jbig_decode(buffer, length, &image) == CODEC_OK)
            jbig_free(&image);
    }

    length = splice(stripe_resets, sizeof stripe_resets, JBIG_HEADER_SIZE, floating,
                    sizeof floating);
    expect_bits(buffer, length, 37, 29, pattern);
    /* Between the first two stripes. */
    length = splice(stripe_resets, sizeof stripe_resets, JBIG_HEADER_SIZE + 4, floating,
                    sizeof floating);
    expect_bits(buffer, length, 37, 29, pattern);
    length = splice(at_moves, sizeof at_moves, sizeof at_moves, trailing, sizeof trailing);
    expect_bits(buffer, length, 120, 60, periodic);
}

/* A picture coded in one stripe by our encoder, with only its first lines. */
static size_t encode_pattern(unsigned lines, unsigned height, unsigned options)
{
    struct collected c = { buffer, 0, sizeof buffer, 0, -1 };
    struct jbig_encoder e;
    uint8_t row[5];
    unsigned x, y;

    jbig_make_header(buffer, 37, height, options);
    c.length = JBIG_HEADER_SIZE;
    assert(jbig_encoder_init(&e, 37, height, options, collect, &c));
    for (y = 0; y < lines; y++) {
        memset(row, 0, sizeof row);
        for (x = 0; x < 37; x++)
            if (pattern(x, y))
                row[x / 8] |= (uint8_t)(0x80u >> (x % 8));
        jbig_encode_bits(&e, row);
    }
    assert(jbig_encoder_end(&e));
    return c.length;
}

static void test_new_length(void)
{
    uint8_t newlen[6] = { ESC, 0x05, 0, 0, 0, 20 };
    size_t length;

    /* After the last stripe. */
    length = encode_pattern(20, 29, JBIG_TYPICAL);
    memcpy(buffer + length, newlen, sizeof newlen);
    expect_bits(buffer, length + sizeof newlen, 37, 20, pattern);
    /* An unknown length, given only at the end. */
    put32(buffer + 8, 0xffffffffu);
    expect_bits(buffer, length + sizeof newlen, 37, 20, pattern);
    expect_error(buffer, length, CODEC_TOO_LARGE);
    /* Just before the stripe's SDNORM. */
    length = encode_pattern(20, 29, 0);
    memcpy(buffer + length - 2, newlen, sizeof newlen);
    buffer[length + 4] = ESC;
    buffer[length + 5] = 0x02;
    expect_bits(buffer, length + sizeof newlen, 37, 20, pattern);
    /* Only the first NEWLEN counts; a greater length is ignored. */
    length = encode_pattern(29, 29, 0);
    newlen[5] = 40;
    memcpy(buffer + length, newlen, sizeof newlen);
    expect_bits(buffer, length + sizeof newlen, 37, 29, pattern);
    newlen[5] = 0;
    memcpy(buffer + length, newlen, sizeof newlen);
    expect_error(buffer, length + sizeof newlen, CODEC_INVALID);
}

static void test_bad_headers(void)
{
    uint8_t header[JBIG_HEADER_SIZE + 2];
    size_t i;

    for (i = 0; i < JBIG_HEADER_SIZE; i++)
        expect_error(at_moves, i, CODEC_TRUNCATED);
#define EXPECT_PATCHED(offset, value, result) \
    do { \
        memcpy(header, at_moves, JBIG_HEADER_SIZE); \
        header[offset] = (value); \
        header[JBIG_HEADER_SIZE] = ESC; \
        header[JBIG_HEADER_SIZE + 1] = 0x02; \
        expect_error(header, sizeof header, result); \
    } while (0)
    EXPECT_PATCHED(0, 1, CODEC_INVALID);      /* DL: continues another BIE */
    EXPECT_PATCHED(2, 0, CODEC_INVALID);      /* no planes */
    EXPECT_PATCHED(2, 17, CODEC_INVALID);     /* more planes than 16 bits */
    EXPECT_PATCHED(18, 0x01, CODEC_INVALID);  /* SMID alone */
    EXPECT_PATCHED(18, 0x07, CODEC_INVALID);  /* SEQ, ILEAVE and SMID */
    EXPECT_PATCHED(5, 1, CODEC_TOO_LARGE);    /* 65656 wide */
    memcpy(header, at_moves, JBIG_HEADER_SIZE);
    put32(header + 4, 0);
    expect_error(header, JBIG_HEADER_SIZE, CODEC_INVALID);
    put32(header + 4, 5000);
    put32(header + 8, 0);
    expect_error(header, JBIG_HEADER_SIZE, CODEC_INVALID);
    put32(header + 8, 5000);
    expect_error(header, JBIG_HEADER_SIZE, CODEC_TOO_LARGE);
    put32(header + 8, 60);
    put32(header + 12, 0);
    expect_error(header, JBIG_HEADER_SIZE, CODEC_INVALID);
#undef EXPECT_PATCHED
}

static void test_bad_data(void)
{
    static const uint8_t one_pixel[] = {
        0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, ESC, 0x02,
    };
    const uint8_t *vectors[] = { progressive, hitolo_seq_two_line, stripe_resets, at_moves,
                                 three_planes, ten_planes };
    const size_t lengths[] = { sizeof progressive, sizeof hitolo_seq_two_line,
                               sizeof stripe_resets, sizeof at_moves, sizeof three_planes,
                               sizeof ten_planes };
    struct jbig_image image;
    size_t v, i;

    /* An empty stripe decodes as zero bytes of coded data. */
    assert(jbig_decode(one_pixel, sizeof one_pixel, &image) == CODEC_OK);
    assert(image.width == 1 && image.height == 1);
    jbig_free(&image);

    for (v = 0; v < sizeof vectors / sizeof *vectors; v++)
        for (i = JBIG_HEADER_SIZE; i < lengths[v]; i++)
            expect_error(vectors[v], i, CODEC_TRUNCATED);

    memcpy(buffer, at_moves, sizeof at_moves);
    buffer[26] = 0xff; /* an AT pixel to the right on the same line */
    expect_error(buffer, sizeof at_moves, CODEC_INVALID);
    buffer[26] = 0x08;
    buffer[21] = 0x04; /* ABORT */
    expect_error(buffer, sizeof at_moves, CODEC_TRUNCATED);
    buffer[21] = 0x01; /* RESERVE */
    expect_error(buffer, sizeof at_moves, CODEC_INVALID);
    buffer[21] = 0x08; /* undefined */
    expect_error(buffer, sizeof at_moves, CODEC_INVALID);
    memcpy(buffer, at_moves, sizeof at_moves);
    buffer[30] = ESC; /* a marker inside a stripe */
    buffer[31] = 0x06;
    expect_error(buffer, sizeof at_moves, CODEC_INVALID);
    /* An unterminated comment. */
    memcpy(buffer, at_moves, JBIG_HEADER_SIZE);
    memcpy(buffer + JBIG_HEADER_SIZE, "\xff\x07\x00\x00\x01\x00", 6);
    expect_error(buffer, JBIG_HEADER_SIZE + 6 + 255, CODEC_TRUNCATED);
}

/* Mutated vectors must fail cleanly or decode something. */
static void test_mutations(void)
{
    const uint8_t *vectors[] = { progressive, hitolo_seq_two_line, stripe_resets, at_moves,
                                 three_planes, ten_planes };
    const size_t lengths[] = { sizeof progressive, sizeof hitolo_seq_two_line,
                               sizeof stripe_resets, sizeof at_moves, sizeof three_planes,
                               sizeof ten_planes };
    uint32_t seed = 12345;
    size_t v;
    int round, change;

    for (v = 0; v < sizeof vectors / sizeof *vectors; v++)
        for (round = 0; round < 3000; round++) {
            struct jbig_image image;
            memcpy(buffer, vectors[v], lengths[v]);
            for (change = 0; change < 1 + round % 4; change++) {
                seed = seed * 1103515245u + 12345u;
                buffer[(seed >> 8) % lengths[v]] = (uint8_t)(seed >> 24);
            }
            /* Keep the sizes small so that rounds stay quick. */
            if (buffer[4] | buffer[5] | buffer[6] | buffer[8] | buffer[9] | buffer[10])
                continue;
            if (jbig_decode(buffer, lengths[v], &image) == CODEC_OK)
                jbig_free(&image);
        }
}

static int round_trip_bit(unsigned x, unsigned y)
{
    return (x * 5u + y * y * 3u) % 7u < 3u;
}

static void test_encoder(void)
{
    /* Black, white, transparent black, dark grey, light grey. */
    static const uint8_t rgba[20] = {
        0, 0, 0, 255, 255, 255, 255, 255, 0, 0, 0, 0, 100, 100, 100, 255, 200, 200, 200, 255,
    };
    static const unsigned options[4] = { 0, JBIG_TYPICAL, JBIG_TWO_LINE,
                                         JBIG_TYPICAL | JBIG_TWO_LINE };
    struct collected c;
    struct jbig_encoder e;
    struct jbig_image image;
    uint8_t row[3];
    unsigned width, height, o, x, y;

    c.data = buffer;
    c.capacity = sizeof buffer;
    c.length = JBIG_HEADER_SIZE;
    c.stuffed = 0;
    c.fail_after = -1;
    jbig_make_header(buffer, 5, 2, JBIG_TYPICAL);
    assert(jbig_encoder_init(&e, 5, 2, JBIG_TYPICAL, collect, &c));
    jbig_encode_rgba(&e, rgba);
    jbig_encode_rgba(&e, rgba);
    assert(jbig_encoder_end(&e));
    assert(jbig_decode(buffer, c.length, &image) == CODEC_OK);
    assert(image.width == 5 && image.height == 2);
    assert(memcmp(image.pixels, "\1\0\0\1\0\1\0\0\1\0", 10) == 0);
    jbig_free(&image);

    for (o = 0; o < 4; o++)
        for (width = 1; width <= 17; width++)
            for (height = 1; height <= 5; height++) {
                c.length = JBIG_HEADER_SIZE;
                jbig_make_header(buffer, width, height, options[o]);
                assert(jbig_encoder_init(&e, width, height, options[o], collect, &c));
                for (y = 0; y < height; y++) {
                    /* Set padding bits too: the encoder must ignore them. */
                    memset(row, 0xff, sizeof row);
                    for (x = 0; x < width; x++)
                        if (!round_trip_bit(x, y / 2))
                            row[x / 8] &= (uint8_t)~(0x80u >> (x % 8));
                    jbig_encode_bits(&e, row);
                }
                assert(jbig_encoder_end(&e));
                assert(jbig_decode(buffer, c.length, &image) == CODEC_OK);
                assert(image.width == width && image.height == height);
                for (y = 0; y < height; y++)
                    for (x = 0; x < width; x++)
                        assert(image.pixels[y * width + x] == round_trip_bit(x, y / 2));
                jbig_free(&image);
            }

    /* A failing sink is reported. */
    c.length = 0;
    c.fail_after = 3;
    assert(jbig_encoder_init(&e, 17, 5, 0, collect, &c));
    for (y = 0; y < 5; y++)
        jbig_encode_bits(&e, (const uint8_t *)"\x5a\xa5\x33");
    assert(!jbig_encoder_end(&e));
}

int main(void)
{
    test_arithmetic_coder();
    test_default_dp_table();
    test_vectors();
    test_optional_structures();
    test_new_length();
    test_bad_headers();
    test_bad_data();
    test_mutations();
    test_encoder();
    test_spec_image();
    puts("jbig: ok");
    return 0;
}
