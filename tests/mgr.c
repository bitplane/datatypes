#include "../formats/mgr/decode.h"
#include "../formats/mgr/encode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t file[MGR_HEADER_SIZE + 4 * 64 * 64 + 64];
static size_t file_size;

/* A header with the given magic; depth is only written for "yz". */
static void start(const char *magic, unsigned width, unsigned height, int depth)
{
    memset(file, 0, sizeof file);
    file[0] = (uint8_t)magic[0];
    file[1] = (uint8_t)magic[1];
    file[2] = (uint8_t)(' ' + (width >> 6));
    file[3] = (uint8_t)(' ' + (width & 63u));
    file[4] = (uint8_t)(' ' + (height >> 6));
    file[5] = (uint8_t)(' ' + (height & 63u));
    file_size = MGR_OLD_HEADER_SIZE;
    if (magic[0] == 'y') {
        file[6] = (uint8_t)(' ' + depth);
        file[7] = ' ';
        file_size = MGR_HEADER_SIZE;
    }
}

static void add(const uint8_t *bytes, size_t count)
{
    assert(file_size + count <= sizeof file);
    memcpy(file + file_size, bytes, count);
    file_size += count;
}

static enum codec_result decode(struct mgr_image *image)
{
    return mgr_decode(file, file_size, image);
}

/* Every shorter copy of the file is truncated. */
static void expect_truncated_prefixes(void)
{
    struct mgr_image image;
    size_t full = file_size;
    for (file_size = 0; file_size < full; file_size++) {
        assert(decode(&image) == CODEC_TRUNCATED);
        assert(image.pixels == NULL && image.width == 0);
    }
    file_size = full;
}

/* Rows of `row_bytes` whose pixels are black where (x + 2y) % 3 == 0, with
   every pad bit set so that padding can't leak into the image. */
static void add_pattern(unsigned width, unsigned height, size_t row_bytes)
{
    unsigned x, y;
    for (y = 0; y < height; y++) {
        uint8_t row[16];
        assert(row_bytes <= sizeof row);
        memset(row, 0xff, sizeof row);
        for (x = 0; x < width; x++)
            if ((x + 2 * y) % 3 != 0)
                row[x / 8] &= (uint8_t)~(0x80u >> (x % 8));
        add(row, row_bytes);
    }
}

static void expect_pattern(unsigned width, unsigned height)
{
    struct mgr_image image;
    unsigned x, y;
    assert(decode(&image) == CODEC_OK);
    assert(image.width == width && image.height == height);
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++)
            assert(image.pixels[y * width + x] == ((x + 2 * y) % 3 == 0));
    mgr_free(&image);
    assert(image.pixels == NULL);
}

static void set_pixel(uint8_t *p, unsigned r, unsigned g, unsigned b, unsigned a)
{
    p[0] = (uint8_t)r; p[1] = (uint8_t)g; p[2] = (uint8_t)b; p[3] = (uint8_t)a;
}

int main(void)
{
    static uint8_t rgba[(MGR_MAX_SIDE + 1) * 4];
    struct mgr_image image;
    uint8_t header[MGR_HEADER_SIZE], row[MGR_ROW_MAX + 1];
    size_t i;
    unsigned x;

    /* Each layout pads rows to its own boundary: 8, 16 and 32 bits. */
    {
        struct { const char *magic; unsigned width; size_t row_bytes; } cases[] = {
            {"yz", 1, 1}, {"yz", 8, 1}, {"yz", 9, 2}, {"yz", 70, 9},
            {"zz", 1, 2}, {"zz", 16, 2}, {"zz", 17, 4}, {"zz", 70, 10},
            {"xz", 1, 4}, {"xz", 32, 4}, {"xz", 33, 8}, {"xz", 70, 12},
        };
        for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
            start(cases[i].magic, cases[i].width, 5, 1);
            add_pattern(cases[i].width, 5, cases[i].row_bytes);
            expect_pattern(cases[i].width, 5);
            expect_truncated_prefixes();
            /* Trailing bytes are ignored. */
            add((const uint8_t *)"junk", 4);
            expect_pattern(cases[i].width, 5);
        }
    }
    /* The top left pixel is the high bit of the first byte, and a set bit
       is black. */
    {
        static const uint8_t bits[] = {0x80, 0x01};
        start("yz", 16, 1, 1);
        add(bits, sizeof bits);
        assert(decode(&image) == CODEC_OK);
        assert(image.pixels[0] == 1 && image.pixels[1] == 0 && image.pixels[15] == 1);
        mgr_free(&image);
    }
    /* Sides use both characters: 64 * 1 + 2 = 66 wide, 64 + 0 = 64 high. */
    start("yz", 66, 64, 1);
    assert(file[2] == '!' && file[3] == '"' && file[4] == '!' && file[5] == ' ');
    add_pattern(66, 64, 9);
    expect_pattern(66, 64);
    /* The largest side, 4095, is "__". */
    start("yz", MGR_MAX_SIDE, 1, 1);
    assert(file[2] == '_' && file[3] == '_');
    file_size += (MGR_MAX_SIDE + 7) / 8;
    assert(file_size <= sizeof file);
    assert(decode(&image) == CODEC_OK && image.width == MGR_MAX_SIDE);
    mgr_free(&image);
    file_size--;
    assert(decode(&image) == CODEC_TRUNCATED);
    /* A low character above ' ' + 63 is read as netpbm reads it. */
    start("yz", 1, 1, 1);
    file[3] = ' ' + 100;
    file_size += 13;
    assert(decode(&image) == CODEC_OK && image.width == 100);
    mgr_free(&image);

    /* Header errors. */
    {
        start("yz", 8, 2, 1);
        add_pattern(8, 2, 1);
        assert(decode(&image) == CODEC_OK);
        mgr_free(&image);
        /* Bad and unsupported magic, including 8-bit "zy" pixmaps. */
        {
            static const char *bad[] = {"zy", "yy", "az", "ZZ", "\0z", "z\0"};
            for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
                start("yz", 8, 2, 1);
                add_pattern(8, 2, 1);
                file[0] = (uint8_t)bad[i][0];
                file[1] = (uint8_t)bad[i][1];
                assert(decode(&image) == CODEC_INVALID);
                assert(image.pixels == NULL);
            }
        }
        /* Depths other than 1, including 8-bit colour. */
        {
            static const int depths[] = {0, 2, 8, 24, 32, -32};
            for (i = 0; i < sizeof depths / sizeof depths[0]; i++) {
                start("yz", 8, 2, depths[i]);
                add_pattern(8, 2, 8);
                assert(decode(&image) == CODEC_INVALID);
            }
        }
        /* Zero sides, sides over 4095 and characters below ' '. */
        start("yz", 0, 2, 1);
        add_pattern(8, 2, 2);
        assert(decode(&image) == CODEC_INVALID);
        start("yz", 2, 0, 1);
        add_pattern(8, 2, 2);
        assert(decode(&image) == CODEC_INVALID);
        start("zz", 8, 2, 1);
        add_pattern(8, 2, 2);
        file[2] = ' ' + 64;
        assert(decode(&image) == CODEC_INVALID);
        start("zz", 8, 2, 1);
        add_pattern(8, 2, 2);
        file[4] = 0xff;
        assert(decode(&image) == CODEC_INVALID);
        /* A high low character is a legal size, so the rows are missing. */
        start("zz", 8, 2, 1);
        add_pattern(8, 2, 2);
        file[5] = 0xff;
        assert(decode(&image) == CODEC_TRUNCATED);
        for (i = 2; i < 6; i++) {
            start("xz", 8, 2, 1);
            add_pattern(8, 2, 4);
            file[i] = ' ' - 1;
            assert(decode(&image) == CODEC_INVALID);
        }
        assert(mgr_decode(NULL, 0, &image) == CODEC_TRUNCATED);
        assert(mgr_decode(file, file_size, NULL) == CODEC_INVALID);
    }

    /* Headers. */
    memset(header, 0, sizeof header);
    assert(mgr_make_header(header, 66, 4095));
    assert(memcmp(header, "yz!\"__! ", 8) == 0);
    assert(mgr_make_header(header, 1, 1));
    assert(memcmp(header, "yz ! !! ", 8) == 0);
    assert(!mgr_make_header(header, 0, 1));
    assert(!mgr_make_header(header, 1, 0));
    assert(!mgr_make_header(header, MGR_MAX_SIDE + 1, 1));
    assert(!mgr_make_header(header, 1, MGR_MAX_SIDE + 1));

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
            assert(mgr_encode_row(rgba, 1, row, sizeof row) == 1);
            assert(row[0] == (cases[i].black ? 0x80 : 0));
        }
    }
    /* Pad bits are clear. */
    for (x = 0; x < 9; x++)
        set_pixel(rgba + x * 4, 0, 0, 0, 255);
    memset(row, 0x55, sizeof row);
    assert(mgr_encode_row(rgba, 9, row, sizeof row) == 2);
    assert(row[0] == 0xff && row[1] == 0x80 && row[2] == 0x55);
    for (x = 0; x < MGR_MAX_SIDE + 1; x++)
        set_pixel(rgba + x * 4, 0, 0, 0, 255);
    assert(mgr_encode_row(rgba, MGR_MAX_SIDE, row, sizeof row) == MGR_ROW_MAX);
    assert(row[MGR_ROW_MAX - 1] == 0xfe);
    /* Argument errors. */
    assert(mgr_encode_row(rgba, MGR_MAX_SIDE + 1, row, sizeof row) == 0);
    assert(mgr_encode_row(rgba, 0, row, sizeof row) == 0);
    assert(mgr_encode_row(rgba, 9, row, 1) == 0);
    assert(mgr_encode_row(NULL, 1, row, sizeof row) == 0);
    assert(mgr_encode_row(rgba, 1, NULL, sizeof row) == 0);

    /* Encoder output decodes to itself. */
    {
        unsigned width = 37, height = 11, y;
        size_t size;
        srand(99);
        start("yz", 1, 1, 1);
        assert(mgr_make_header(file, width, height));
        file_size = MGR_HEADER_SIZE;
        {
            static uint8_t expected[37 * 11];
            for (y = 0; y < height; y++) {
                for (x = 0; x < width; x++) {
                    int black = rand() & 1;
                    set_pixel(rgba + x * 4, black ? 0 : 255, black ? 0 : 255, black ? 0 : 255, 255);
                    expected[y * width + x] = (uint8_t)black;
                }
                size = mgr_encode_row(rgba, width, row, sizeof row);
                assert(size == 5);
                add(row, size);
            }
            assert(decode(&image) == CODEC_OK);
            assert(image.width == width && image.height == height);
            assert(memcmp(image.pixels, expected, sizeof expected) == 0);
            mgr_free(&image);
            expect_truncated_prefixes();
        }
    }
    puts("mgr codec tests passed");
    return 0;
}
