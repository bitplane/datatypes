#include "../formats/cmuwm/decode.h"
#include "../formats/cmuwm/encode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t file[CMUWM_LONG_HEADER_SIZE + 64 * 64 + 64];
static size_t file_size;

static void put(uint8_t *p, uint32_t value, size_t size, int little)
{
    size_t i;
    for (i = 0; i < size; i++)
        p[little ? i : size - 1 - i] = (uint8_t)(value >> (8 * i));
}

/* A header in either byte order, 14 bytes with a 16-bit depth or 16 with a
   32-bit one. */
static void start(unsigned width, unsigned height, uint32_t depth, int little, int long_header)
{
    memset(file, 0, sizeof file);
    put(file, 0xf10040bbUL, 4, little);
    put(file + 4, width, 4, little);
    put(file + 8, height, 4, little);
    if (long_header) {
        put(file + 12, depth, 4, little);
        file_size = CMUWM_LONG_HEADER_SIZE;
    } else {
        put(file + 12, depth, 2, little);
        file_size = CMUWM_HEADER_SIZE;
    }
}

static void add(const uint8_t *bytes, size_t count)
{
    assert(file_size + count <= sizeof file);
    memcpy(file + file_size, bytes, count);
    file_size += count;
}

static enum codec_result decode(struct cmuwm_image *image)
{
    return cmuwm_decode(file, file_size, image);
}

/* Shorter copies are truncated, except that dropping one or two bytes from a
   16-byte header file leaves a valid 14-byte header one: `slack` skips them. */
static void expect_truncated_prefixes(size_t slack)
{
    struct cmuwm_image image;
    size_t full = file_size;
    for (file_size = 0; file_size < full - slack; file_size++) {
        assert(decode(&image) == CODEC_TRUNCATED);
        assert(image.pixels == NULL && image.width == 0);
    }
    file_size = full;
}

/* Black where (x + 2y) % 3 == 0. A clear bit is black; pad bits are clear
   here, the opposite of what writers use, so they can't leak in. */
static void add_pattern(unsigned width, unsigned height)
{
    unsigned x, y;
    size_t row_bytes = (width + 7) / 8;
    for (y = 0; y < height; y++) {
        uint8_t row[16];
        assert(row_bytes <= sizeof row);
        memset(row, 0, sizeof row);
        for (x = 0; x < width; x++)
            if ((x + 2 * y) % 3 != 0)
                row[x / 8] |= (uint8_t)(0x80u >> (x % 8));
        add(row, row_bytes);
    }
}

static void expect_pattern(unsigned width, unsigned height)
{
    struct cmuwm_image image;
    unsigned x, y;
    assert(decode(&image) == CODEC_OK);
    assert(image.width == width && image.height == height);
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++)
            assert(image.pixels[y * width + x] == ((x + 2 * y) % 3 == 0));
    cmuwm_free(&image);
    assert(image.pixels == NULL);
}

static void set_pixel(uint8_t *p, unsigned r, unsigned g, unsigned b, unsigned a)
{
    p[0] = (uint8_t)r; p[1] = (uint8_t)g; p[2] = (uint8_t)b; p[3] = (uint8_t)a;
}

int main(void)
{
    static uint8_t rgba[(CMUWM_MAX_SIDE + 1) * 4], row[CMUWM_ROW_MAX + 1];
    struct cmuwm_image image;
    uint8_t header[CMUWM_HEADER_SIZE];
    static const unsigned widths[] = {1, 7, 8, 9, 70};
    size_t i;
    unsigned x;
    int little, long_header;

    /* Both byte orders and both header sizes. */
    for (little = 0; little <= 1; little++) {
        for (long_header = 0; long_header <= 1; long_header++) {
            for (i = 0; i < sizeof widths / sizeof widths[0]; i++) {
                start(widths[i], 5, 1, little, long_header);
                add_pattern(widths[i], 5);
                expect_pattern(widths[i], 5);
                expect_truncated_prefixes(long_header ? 2 : 0);
            }
            /* A 16-byte header may keep a 16-bit depth, as the Andrew
               Toolkit writes it from a padded struct. */
            if (long_header) {
                start(9, 5, 1, little, 0);
                file_size += 2;
                file[14] = 0x12;
                file[15] = 0x34;
                add_pattern(9, 5);
                expect_pattern(9, 5);
            }
        }
    }
    /* One trailing byte leaves the header at 14 bytes. */
    start(9, 5, 1, 0, 0);
    add_pattern(9, 5);
    add((const uint8_t *)"x", 1);
    expect_pattern(9, 5);
    /* Two or more make it 16, so a 14-byte header with trailing data shifts. */
    start(9, 5, 1, 0, 0);
    add((const uint8_t *)"xx", 2);
    add_pattern(9, 5);
    expect_pattern(9, 5);
    start(9, 5, 1, 0, 1);
    add_pattern(9, 5);
    add((const uint8_t *)"trailing", 8);
    expect_pattern(9, 5);
    /* A set bit is white and the high bit is the left pixel. */
    {
        static const uint8_t bits[] = {0x7f, 0xfe};
        start(16, 1, 1, 0, 0);
        add(bits, sizeof bits);
        assert(decode(&image) == CODEC_OK);
        assert(image.pixels[0] == 1 && image.pixels[1] == 0 && image.pixels[15] == 1);
        cmuwm_free(&image);
    }
    /* The pixel limit: 4096 x 4096 is allowed, one more row isn't. */
    {
        static uint8_t big[CMUWM_HEADER_SIZE + 512 * 4096];
        memcpy(big, file, CMUWM_HEADER_SIZE);
        put(big + 4, 4096, 4, 0);
        put(big + 8, 4096, 4, 0);
        assert(cmuwm_decode(big, sizeof big, &image) == CODEC_OK);
        assert(image.width == 4096 && image.height == 4096);
        cmuwm_free(&image);
        put(big + 8, 4097, 4, 0);
        assert(cmuwm_decode(big, sizeof big, &image) == CODEC_TOO_LARGE);
        put(big + 4, 65535, 4, 0);
        put(big + 8, 1, 4, 0);
        assert(cmuwm_decode(big, sizeof big, &image) == CODEC_OK);
        cmuwm_free(&image);
    }

    /* Header errors. */
    {
        static const uint32_t sizes[][2] = {
            {0, 1}, {1, 0}, {65536, 1}, {1, 65536}, {0xffffffffUL, 1},
            {1, 0xffffffffUL}, {0x80000000UL, 0x80000000UL},
        };
        for (little = 0; little <= 1; little++) {
            for (i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
                start(1, 1, 1, little, 0);
                put(file + 4, sizes[i][0], 4, little);
                put(file + 8, sizes[i][1], 4, little);
                add_pattern(8, 8);
                assert(decode(&image) == (sizes[i][0] == 0 || sizes[i][1] == 0 ?
                                          CODEC_INVALID : CODEC_TOO_LARGE));
                assert(image.pixels == NULL);
            }
            /* Depths other than 1. */
            start(8, 2, 0, little, 0);
            add_pattern(8, 2);
            assert(decode(&image) == CODEC_INVALID);
            start(8, 2, 8, little, 0);
            add_pattern(8, 2);
            assert(decode(&image) == CODEC_INVALID);
            start(8, 2, 0x20001UL, little, 1);
            add_pattern(8, 2);
            assert(decode(&image) == (little ? CODEC_OK : CODEC_INVALID));
            cmuwm_free(&image);
            /* A 32-bit depth needs a 16-byte header. */
            start(8, 2, 1, little, 1);
            file_size = CMUWM_HEADER_SIZE;
            add_pattern(8, 2);
            assert(decode(&image) == (little ? CODEC_OK : CODEC_INVALID));
            cmuwm_free(&image);
        }
        /* Bad magic, including a mixed byte order. */
        {
            static const uint8_t bad[][4] = {
                {0xf1, 0x00, 0x40, 0xba}, {0x00, 0x40, 0x00, 0xf1},
                {0xbb, 0x00, 0x40, 0xf1}, {0x40, 0xbb, 0xf1, 0x00},
            };
            for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
                start(8, 2, 1, 0, 0);
                add_pattern(8, 2);
                memcpy(file, bad[i], 4);
                assert(decode(&image) == CODEC_INVALID);
                file_size = 3;
                assert(decode(&image) == CODEC_TRUNCATED);
            }
        }
        assert(cmuwm_decode(NULL, 0, &image) == CODEC_TRUNCATED);
        assert(cmuwm_decode(file, file_size, NULL) == CODEC_INVALID);
    }

    /* Headers. */
    {
        static const uint8_t expected[CMUWM_HEADER_SIZE] = {
            0xf1, 0x00, 0x40, 0xbb, 0, 0, 0x01, 0x02, 0, 0, 0xfd, 0xe8, 0, 1,
        };
        assert(cmuwm_make_header(header, 258, 65000));
        assert(memcmp(header, expected, sizeof expected) == 0);
        assert(!cmuwm_make_header(header, 0, 1));
        assert(!cmuwm_make_header(header, 1, 0));
        assert(!cmuwm_make_header(header, 65536, 1));
        assert(!cmuwm_make_header(header, 4097, 4096));
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
            set_pixel(rgba, cases[i].r, cases[i].g, cases[i].b, cases[i].a);
            assert(cmuwm_encode_row(rgba, 1, row, sizeof row) == 1);
            assert(row[0] == (cases[i].black ? 0x7f : 0xff));
        }
    }
    /* Pad bits are set, as netpbm and the Andrew Toolkit write them. */
    for (x = 0; x < CMUWM_MAX_SIDE + 1; x++)
        set_pixel(rgba + x * 4, 0, 0, 0, 255);
    memset(row, 0x55, sizeof row);
    assert(cmuwm_encode_row(rgba, 9, row, sizeof row) == 2);
    assert(row[0] == 0x00 && row[1] == 0x7f && row[2] == 0x55);
    assert(cmuwm_encode_row(rgba, CMUWM_MAX_SIDE, row, sizeof row) == CMUWM_ROW_MAX);
    assert(row[CMUWM_ROW_MAX - 1] == 0x01);
    /* Argument errors. */
    assert(cmuwm_encode_row(rgba, CMUWM_MAX_SIDE + 1, row, sizeof row) == 0);
    assert(cmuwm_encode_row(rgba, 0, row, sizeof row) == 0);
    assert(cmuwm_encode_row(rgba, 9, row, 1) == 0);
    assert(cmuwm_encode_row(NULL, 1, row, sizeof row) == 0);
    assert(cmuwm_encode_row(rgba, 1, NULL, sizeof row) == 0);

    /* Encoder output decodes to itself. */
    {
        static uint8_t expected[37 * 11];
        unsigned width = 37, height = 11, y;
        size_t size;
        srand(99);
        assert(cmuwm_make_header(file, width, height));
        file_size = CMUWM_HEADER_SIZE;
        for (y = 0; y < height; y++) {
            for (x = 0; x < width; x++) {
                int black = rand() & 1;
                set_pixel(rgba + x * 4, black ? 0 : 255, black ? 0 : 255, black ? 0 : 255, 255);
                expected[y * width + x] = (uint8_t)black;
            }
            size = cmuwm_encode_row(rgba, width, row, sizeof row);
            assert(size == 5);
            add(row, size);
        }
        assert(decode(&image) == CODEC_OK);
        assert(image.width == width && image.height == height);
        assert(memcmp(image.pixels, expected, sizeof expected) == 0);
        cmuwm_free(&image);
        expect_truncated_prefixes(0);
    }
    puts("cmuwm codec tests passed");
    return 0;
}
