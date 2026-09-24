#include "../formats/macpaint/decode.h"
#include "../formats/macpaint/encode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROW_BYTES (MACPAINT_WIDTH / 8)
#define PACKED_SIZE (ROW_BYTES * MACPAINT_HEIGHT)
#define FILE_MAX (MACPAINT_MACBINARY_SIZE + MACPAINT_HEADER_SIZE + \
                  MACPAINT_HEIGHT * MACPAINT_ROW_MAX + 1024)

static uint8_t file[FILE_MAX];
static size_t file_size;

static void start(uint32_t version, int macbinary)
{
    memset(file, 0, sizeof file);
    file_size = 0;
    if (macbinary) {
        file[1] = 5;
        memcpy(file + 2, "hello", 5);
        memcpy(file + 65, "PNTGMPNT", 8);
        file_size = MACPAINT_MACBINARY_SIZE;
    }
    file[file_size] = (uint8_t)(version >> 24);
    file[file_size + 1] = (uint8_t)(version >> 16);
    file[file_size + 2] = (uint8_t)(version >> 8);
    file[file_size + 3] = (uint8_t)version;
    /* Version 2 headers carry 38 patterns; readers ignore them. */
    if (version != 0)
        memset(file + file_size + 4, 0xaa, 38 * 8);
    file_size += MACPAINT_HEADER_SIZE;
}

static void add(const uint8_t *bytes, size_t count)
{
    assert(file_size + count <= sizeof file);
    memcpy(file + file_size, bytes, count);
    file_size += count;
}

/* Blank rows: each is a run of 72 zero bytes. */
static void add_blank_rows(unsigned count)
{
    static const uint8_t blank[] = {0xb9, 0x00};
    while (count-- > 0)
        add(blank, sizeof blank);
}

static enum codec_result decode(struct macpaint_image *image)
{
    return macpaint_decode(file, file_size, image);
}

/* Unpacked byte `at` of the bitmap, rebuilt from the decoded pixels. */
static uint8_t byte_at(const struct macpaint_image *image, size_t at)
{
    uint8_t value = 0;
    unsigned bit;
    for (bit = 0; bit < 8; bit++) {
        uint8_t p = image->pixels[at * 8u + bit];
        assert(p <= 1);
        value = (uint8_t)(value << 1 | p);
    }
    return value;
}

static void expect_bytes(const uint8_t *expected)
{
    struct macpaint_image image;
    size_t i;
    assert(decode(&image) == CODEC_OK);
    assert(image.width == MACPAINT_WIDTH && image.height == MACPAINT_HEIGHT);
    for (i = 0; i < PACKED_SIZE; i++)
        assert(byte_at(&image, i) == expected[i]);
    macpaint_free(&image);
}

/* Shorter copies of the current file, every `step` bytes from empty and
   one byte short, are truncated. */
static void expect_truncated_prefixes(size_t step)
{
    struct macpaint_image image;
    size_t full = file_size, length;
    for (length = 0; length < full; length = length + step < full - 1 ? length + step : length + 1) {
        file_size = length;
        assert(decode(&image) == CODEC_TRUNCATED);
        assert(image.pixels == NULL && image.width == 0);
    }
    file_size = full;
}

static void set_pixel(uint8_t *rgba, unsigned x, int black)
{
    uint8_t *p = rgba + x * 4u;
    p[0] = p[1] = p[2] = black ? 0 : 255;
    p[3] = 255;
}

/* A known bitmap, packed by the encoder, must decode to itself. */
static void round_trip(int macbinary)
{
    static uint8_t rgba[MACPAINT_WIDTH * 4], expected[PACKED_SIZE];
    uint8_t row[MACPAINT_ROW_MAX];
    unsigned x, y;
    size_t size;

    start(0, macbinary);
    memset(expected, 0, sizeof expected);
    srand(1234);
    for (y = 0; y < MACPAINT_HEIGHT; y++) {
        for (x = 0; x < MACPAINT_WIDTH; x++) {
            int black;
            switch (y % 4) {
            case 0: black = rand() & 1; break;          /* noise: literals */
            case 1: black = (x / 40u + y) & 1; break;   /* long runs */
            case 2: black = (x % 16u) < 3; break;       /* mixed */
            default: black = (x / 8u % 3u) == 0; break; /* short runs */
            }
            set_pixel(rgba, x, black);
            if (black)
                expected[y * ROW_BYTES + x / 8u] |= (uint8_t)(0x80u >> (x % 8u));
        }
        size = macpaint_encode_row(rgba, MACPAINT_WIDTH, row, sizeof row);
        assert(size >= 2 && size <= MACPAINT_ROW_MAX);
        add(row, size);
    }
    expect_bytes(expected);
    expect_truncated_prefixes(97);
}

int main(void)
{
    static uint8_t expected[PACKED_SIZE];
    struct macpaint_image image;
    uint8_t row[MACPAINT_ROW_MAX], rgba[(MACPAINT_WIDTH + 8) * 4];
    size_t size, i;
    unsigned x;

    /* Versions 0, 2 and 3, with and without MacBinary. The first row has a
       literal whose first pixel is black, so bit order and offset show. */
    {
        static const uint8_t first[] = {2, 0x80, 0x01, 0xc3, 0xbc, 0xff};
        /* Some MacBinary files carry junk versions; the version is ignored. */
        uint32_t versions[] = {0, 2, 3, 0xfca8700bu};
        int macbinary;
        for (i = 0; i < 4; i++) {
            for (macbinary = i == 3; macbinary <= 1; macbinary++) {
                start(versions[i], macbinary);
                add(first, sizeof first);
                add_blank_rows(MACPAINT_HEIGHT - 1);
                memset(expected, 0, sizeof expected);
                expected[0] = 0x80; expected[1] = 0x01; expected[2] = 0xc3;
                memset(expected + 3, 0xff, 69);
                expect_bytes(expected);
                assert(decode(&image) == CODEC_OK);
                assert(image.pixels[0] == 1 && image.pixels[1] == 0 && image.pixels[15] == 1);
                macpaint_free(&image);
            }
        }
        /* Truncated in the MacBinary header, the MacPaint header and every
           place in the packed data. */
        expect_truncated_prefixes(1);
    }

    /* Runs cross rows: 100 black bytes, then 44 white, fill two rows. */
    {
        static const uint8_t runs[] = {0x9d, 0xff, 0xd5, 0x00};
        start(0, 0);
        add(runs, sizeof runs);
        add_blank_rows(MACPAINT_HEIGHT - 2);
        memset(expected, 0, sizeof expected);
        memset(expected, 0xff, 100);
        expect_bytes(expected);
    }
    /* A literal also crosses rows. */
    {
        start(0, 0);
        add_blank_rows(1);
        {
            uint8_t literal[1 + 100];
            literal[0] = 99;
            for (i = 0; i < 100; i++)
                literal[1 + i] = (uint8_t)(i * 37u + 1u);
            add(literal, sizeof literal);
            memset(expected, 0, sizeof expected);
            for (i = 0; i < 100; i++)
                expected[ROW_BYTES + i] = literal[1 + i];
        }
        {
            static const uint8_t rest[] = {0xd5, 0x00};
            add(rest, sizeof rest);
        }
        add_blank_rows(MACPAINT_HEIGHT - 3);
        expect_bytes(expected);
    }
    /* 0x80 repeats the next byte 129 times, as ImageMagick and netpbm read it. */
    {
        static const uint8_t runs[] = {0x80, 0x5a, 0xef, 0x00};
        start(0, 0);
        add(runs, sizeof runs);
        add_blank_rows(MACPAINT_HEIGHT - 2);
        memset(expected, 0, sizeof expected);
        memset(expected, 0x5a, 129);
        expect_bytes(expected);
    }
    /* A run past the last pixel is clipped, and trailing bytes are ignored. */
    {
        static const uint8_t runs[] = {0x81, 0xaa, 0x81, 0xaa, 0x12, 0x34};
        start(0, 0);
        add_blank_rows(MACPAINT_HEIGHT - 1);
        add(runs, sizeof runs);
        memset(expected, 0, sizeof expected);
        memset(expected + PACKED_SIZE - ROW_BYTES, 0xaa, ROW_BYTES);
        expect_bytes(expected);
    }
    /* A literal past the last pixel is clipped, and only the bytes that land
       in the picture need to be present. */
    {
        uint8_t literal[1 + ROW_BYTES];
        start(0, 0);
        add_blank_rows(MACPAINT_HEIGHT - 1);
        {
            static const uint8_t lead[] = {0xfb, 0x00}; /* 6 white bytes */
            add(lead, sizeof lead);
        }
        literal[0] = 127;
        memset(literal + 1, 0x3c, sizeof literal - 1);
        add(literal, 1 + 66);
        memset(expected, 0, sizeof expected);
        memset(expected + PACKED_SIZE - 66, 0x3c, 66);
        expect_bytes(expected);
        file_size--;
        assert(decode(&image) == CODEC_TRUNCATED);
    }

    /* Header errors. */
    {
        start(0, 0);
        add_blank_rows(MACPAINT_HEIGHT);
        file[0] = 1;
        assert(decode(&image) == CODEC_INVALID);
        assert(image.pixels == NULL);
        file[0] = 0;
        /* A MacBinary flag with no MacBinary header shifts the data 128
           bytes, leaving too little. */
        file[1] = 3;
        assert(decode(&image) == CODEC_TRUNCATED);
        file[1] = 0;
        assert(decode(&image) == CODEC_OK);
        macpaint_free(&image);
        assert(macpaint_decode(NULL, 0, &image) == CODEC_TRUNCATED);
        assert(macpaint_decode(file, 1, &image) == CODEC_TRUNCATED);
        assert(macpaint_decode(file, MACPAINT_HEADER_SIZE, &image) == CODEC_TRUNCATED);
        assert(macpaint_decode(file, file_size, NULL) == CODEC_INVALID);
    }

    /* Threshold and compositing over white. */
    {
        struct { uint8_t r, g, b, a; int black; } cases[] = {
            {255, 255, 255, 255, 0}, {0, 0, 0, 255, 1},
            {128, 128, 128, 255, 0}, {127, 127, 127, 255, 1},
            {0, 0, 0, 0, 0}, {0, 0, 0, 128, 1}, {0, 0, 0, 127, 0},
            {255, 0, 0, 255, 1}, {0, 255, 0, 255, 0}, {0, 0, 255, 255, 1},
        };
        for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
            rgba[0] = cases[i].r; rgba[1] = cases[i].g;
            rgba[2] = cases[i].b; rgba[3] = cases[i].a;
            size = macpaint_encode_row(rgba, 1, row, sizeof row);
            /* Black: a literal of one byte, then a run of 71 white bytes.
               White: a run of 72 white bytes. */
            if (cases[i].black)
                assert(size == 4 && row[0] == 0 && row[1] == 0x80 &&
                       row[2] == 0xba && row[3] == 0);
            else
                assert(size == 2 && row[0] == 0xb9 && row[1] == 0);
        }
    }
    /* Blank, narrow and wide rows. */
    assert(macpaint_encode_row(NULL, 0, row, sizeof row) == 2);
    assert(row[0] == 0xb9 && row[1] == 0);
    for (x = 0; x < MACPAINT_WIDTH + 8; x++)
        set_pixel(rgba, x, x >= MACPAINT_WIDTH);
    assert(macpaint_encode_row(rgba, MACPAINT_WIDTH + 8, row, sizeof row) == 2);
    assert(row[0] == 0xb9 && row[1] == 0);
    for (x = 0; x < MACPAINT_WIDTH; x++)
        set_pixel(rgba, x, 1);
    assert(macpaint_encode_row(rgba, MACPAINT_WIDTH, row, sizeof row) == 2);
    assert(row[0] == 0xb9 && row[1] == 0xff);
    assert(macpaint_encode_row(rgba, 8, row, sizeof row) == 4);
    assert(row[0] == 0 && row[1] == 0xff && row[2] == 0xba && row[3] == 0);
    /* Alternating bytes need a single 72-byte literal: the worst case. */
    for (x = 0; x < MACPAINT_WIDTH; x++)
        set_pixel(rgba, x, (x / 8u) & 1u);
    assert(macpaint_encode_row(rgba, MACPAINT_WIDTH, row, sizeof row) == MACPAINT_ROW_MAX);
    assert(row[0] == 71 && row[1] == 0 && row[2] == 0xff);
    /* Pairs stay in literals; triples become runs. */
    for (x = 0; x < MACPAINT_WIDTH; x++)
        set_pixel(rgba, x, (x / 16u) & 1u);
    size = macpaint_encode_row(rgba, MACPAINT_WIDTH, row, sizeof row);
    assert(size == MACPAINT_ROW_MAX && row[0] == 71);
    for (x = 0; x < MACPAINT_WIDTH; x++)
        set_pixel(rgba, x, (x / 24u) & 1u);
    size = macpaint_encode_row(rgba, MACPAINT_WIDTH, row, sizeof row);
    assert(size == 48 && row[0] == 0xfe && row[1] == 0 && row[2] == 0xfe && row[3] == 0xff);
    /* Argument errors. */
    assert(macpaint_encode_row(rgba, 1, row, MACPAINT_ROW_MAX - 1) == 0);
    assert(macpaint_encode_row(rgba, 1, NULL, sizeof row) == 0);
    assert(macpaint_encode_row(NULL, 1, row, sizeof row) == 0);
    {
        uint8_t header[MACPAINT_HEADER_SIZE];
        memset(header, 0x55, sizeof header);
        macpaint_make_header(header);
        for (i = 0; i < sizeof header; i++)
            assert(header[i] == 0);
    }

    round_trip(0);
    round_trip(1);
    puts("macpaint codec tests passed");
    return 0;
}
