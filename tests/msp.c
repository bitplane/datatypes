#include "../formats/msp/decode.h"
#include "../formats/msp/encode.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t data[4096];

static void put16(uint8_t *p, unsigned value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

/* A header with a valid checksum. */
static void header(const char *magic, unsigned width, unsigned height)
{
    unsigned checksum = 0, i;
    memset(data, 0, 32);
    memcpy(data, magic, 4);
    put16(data + 4, width);
    put16(data + 6, height);
    put16(data + 8, 1); put16(data + 10, 1);
    for (i = 0; i < 12; i++)
        checksum ^= data[i * 2u] | ((unsigned)data[i * 2u + 1u] << 8);
    put16(data + 24, checksum);
}

static void check_row(const struct msp_image *image, unsigned y, const char *expect)
{
    unsigned x;
    for (x = 0; x < image->width; x++)
        assert(image->pixels[y * image->width + x] == (unsigned)(expect[x] == '#' ? 0 : 1));
}

int main(void)
{
    struct msp_image image;
    uint8_t rgba[12 * 4], row[2];
    size_t n, size;
    unsigned x, i;

    /* Version 1: raw rows, most significant bit first, 1 is white. Padding bits are ignored. */
    header("DanM", 10, 2);
    data[32] = 0x5a; data[33] = 0xbf;
    data[34] = 0x00; data[35] = 0x40;
    assert(msp_decode(data, 36, &image) == CODEC_OK);
    assert(image.width == 10 && image.height == 2);
    check_row(&image, 0, "#.#..#.#.#");
    check_row(&image, 1, "#########.");
    msp_free(&image);
    assert(image.pixels == NULL && image.width == 0);

    /* Trailing bytes and a bad checksum are ignored. */
    data[24] ^= 0x55;
    assert(msp_decode(data, 40, &image) == CODEC_OK);
    check_row(&image, 1, "#########.");
    msp_free(&image);

    /* Version 1 truncation, in the header or the pixels. */
    for (n = 0; n < 36; n++) {
        assert(msp_decode(data, n, &image) == CODEC_TRUNCATED);
        assert(image.pixels == NULL && image.width == 0);
    }

    /* Version 2: a fill run, a literal run, then a row the map says is empty (white). */
    header("LinS", 12, 3);
    put16(data + 32, 6); put16(data + 34, 0); put16(data + 36, 4);
    memcpy(data + 38, "\x00\x01\x0f\x01\xf5", 5);
    /* Two bytes of the next row's run: the map, not the runs, sets row sizes. */
    data[43] = 0x00;
    memcpy(data + 44, "\x02\x33\x03\xaa\xbb", 5);
    size = 44 + 4;
    assert(msp_decode(data, size, &image) == CODEC_INVALID);
    put16(data + 32, 5);
    memcpy(data + 43, "\x00\x02\x33", 3);
    put16(data + 36, 3);
    size = 46;
    assert(msp_decode(data, size, &image) == CODEC_OK);
    assert(image.width == 12 && image.height == 3);
    check_row(&image, 0, "####........");
    check_row(&image, 1, "............");
    check_row(&image, 2, "##..##..##..");
    msp_free(&image);

    /* Version 2 truncation: header, row map, row data. */
    for (n = 0; n < size; n++)
        assert(msp_decode(data, n, &image) == CODEC_TRUNCATED);

    /* A row that ends early is white after its last run. */
    header("LinS", 16, 1);
    put16(data + 32, 3);
    memcpy(data + 34, "\x00\x01\x00", 3);
    assert(msp_decode(data, 37, &image) == CODEC_OK);
    check_row(&image, 0, "########........");
    msp_free(&image);

    /* Runs past the end of the row are cut off, fill and literal alike. */
    put16(data + 32, 5);
    memcpy(data + 34, "\x00\xff\x00\x01\x00", 5);
    assert(msp_decode(data, 39, &image) == CODEC_OK);
    check_row(&image, 0, "################");
    msp_free(&image);
    put16(data + 32, 7);
    memcpy(data + 34, "\x03\x0f\xf0\x00\x00\x02\x00", 7);
    assert(msp_decode(data, 41, &image) == CODEC_OK);
    check_row(&image, 0, "####........####");
    msp_free(&image);

    /* A run that needs more bytes than its row holds. */
    put16(data + 32, 2);
    memcpy(data + 34, "\x00\x05\x00", 3);
    assert(msp_decode(data, 37, &image) == CODEC_INVALID);
    assert(image.pixels == NULL && image.width == 0);
    put16(data + 32, 1);
    assert(msp_decode(data, 37, &image) == CODEC_INVALID);
    put16(data + 32, 3);
    memcpy(data + 34, "\x03\x00\x00\x00", 4);
    assert(msp_decode(data, 38, &image) == CODEC_INVALID);
    /* The failure can come on a later row, after memory is allocated. */
    header("LinS", 8, 2);
    put16(data + 32, 3); put16(data + 34, 1);
    memcpy(data + 36, "\x00\x01\x00\x02", 4);
    assert(msp_decode(data, 40, &image) == CODEC_INVALID);
    assert(image.pixels == NULL && image.width == 0 && image.height == 0);

    /* Bad magic, including a case change, and the other version's key halves. */
    header("DanS", 8, 1);
    assert(msp_decode(data, 64, &image) == CODEC_INVALID);
    header("LinM", 8, 1);
    assert(msp_decode(data, 64, &image) == CODEC_INVALID);
    header("danM", 8, 1);
    assert(msp_decode(data, 64, &image) == CODEC_INVALID);
    assert(msp_decode(data, 2, &image) == CODEC_TRUNCATED);
    assert(msp_decode(NULL, 64, &image) == CODEC_TRUNCATED);
    assert(msp_decode(data, 64, NULL) == CODEC_INVALID);

    /* Empty and oversized dimensions. */
    header("DanM", 0, 1);
    assert(msp_decode(data, sizeof data, &image) == CODEC_INVALID);
    header("LinS", 1, 0);
    assert(msp_decode(data, sizeof data, &image) == CODEC_INVALID);
    header("DanM", 4097, 4096);
    assert(msp_decode(data, sizeof data, &image) == CODEC_TOO_LARGE);
    header("LinS", 65535, 65535);
    assert(msp_decode(data, sizeof data, &image) == CODEC_TOO_LARGE);
    assert(image.pixels == NULL);
    /* At the limit the size check passes, so the missing data is what fails. */
    header("DanM", 4096, 4096);
    assert(msp_decode(data, sizeof data, &image) == CODEC_TRUNCATED);
    header("LinS", 65535, 256);
    assert(msp_decode(data, sizeof data, &image) == CODEC_TRUNCATED);
    /* A row map whose sizes add up past the end of the file. */
    header("LinS", 8, 2);
    put16(data + 32, 0xffff); put16(data + 34, 0xffff);
    assert(msp_decode(data, sizeof data, &image) == CODEC_TRUNCATED);

    /* The header matches what Pillow writes, and its checksum clears the XOR. */
    assert(!msp_make_header(0, 1, data));
    assert(!msp_make_header(1, 0, data));
    assert(!msp_make_header(65536, 1, data));
    assert(!msp_make_header(4097, 4096, data));
    assert(!msp_make_header(1, 1, NULL));
    assert(msp_make_header(12, 2, data));
    assert(memcmp(data, "DanM\x0c\x00\x02\x00\x01\x00\x01\x00\x01\x00\x01\x00"
                        "\x0c\x00\x02\x00\x00\x00\x00\x00", 24) == 0);
    for (i = 0, x = 0; i < 16; i++)
        x ^= data[i * 2u] | ((unsigned)data[i * 2u + 1u] << 8);
    assert(x == 0);
    for (i = 26; i < 32; i++)
        assert(data[i] == 0);

    /* Rows threshold on luminance after compositing over white. */
    for (x = 0; x < 12; x++) {
        rgba[x * 4u] = rgba[x * 4u + 1u] = rgba[x * 4u + 2u] = (uint8_t)(x < 6 ? 0 : 255);
        rgba[x * 4u + 3u] = 255;
    }
    rgba[1 * 4 + 3] = 0;                 /* transparent black: white */
    rgba[2 * 4 + 3] = 127;               /* mostly transparent black: white */
    rgba[3 * 4 + 3] = 128;               /* a little less transparent: black */
    rgba[4 * 4 + 1] = 255;               /* green alone is bright enough */
    rgba[5 * 4] = 255;                   /* red alone is not */
    rgba[6 * 4 + 1] = rgba[6 * 4 + 2] = 0; /* nor is red in a white-side run */
    rgba[7 * 4] = rgba[7 * 4 + 1] = rgba[7 * 4 + 2] = 127; /* just below half */
    rgba[8 * 4] = rgba[8 * 4 + 1] = rgba[8 * 4 + 2] = 128; /* half */
    assert(msp_encode_row(rgba, 12, row, 1) == 0);
    assert(msp_encode_row(NULL, 12, row, 2) == 0);
    assert(msp_encode_row(rgba, 0, row, 2) == 0);
    assert(msp_encode_row(rgba, 12, row, 2) == 2);
    assert(row[0] == 0x68 && row[1] == 0xf0);

    /* Encoding then decoding gives back the pixels. */
    header("DanM", 12, 2);
    assert(msp_make_header(12, 2, data));
    memcpy(data + 32, row, 2);
    memcpy(data + 34, row, 2);
    assert(msp_decode(data, 36, &image) == CODEC_OK);
    check_row(&image, 0, "#..#.###....");
    check_row(&image, 1, "#..#.###....");
    msp_free(&image);

    puts("msp codec tests passed");
    return 0;
}
