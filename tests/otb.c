#include "../formats/otb/decode.h"
#include "../formats/otb/encode.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static enum codec_result decode(const uint8_t *data, size_t length, struct otb_image *image)
{
    return otb_decode(data, length, 0, NULL, image);
}

static int is_black(const struct otb_image *image, unsigned x, unsigned y)
{
    const uint8_t *p = image->rgba + ((size_t)y * image->width + x) * 4u;
    assert(p[0] == p[1] && p[1] == p[2] && p[3] == 255);
    assert(p[0] == 0 || p[0] == 255);
    return p[0] == 0;
}

/* Rows of the 3x2 test image: row 0 is white, black, white; row 1 black, white, black. */
static void expect_pattern(const struct otb_image *image)
{
    assert(image->width == 3 && image->height == 2);
    assert(!is_black(image, 0, 0) && is_black(image, 1, 0) && !is_black(image, 2, 0));
    assert(is_black(image, 0, 1) && !is_black(image, 1, 1) && is_black(image, 2, 1));
}

static void expect_decodes(const uint8_t *data, size_t length)
{
    struct otb_image image;
    assert(decode(data, length, &image) == CODEC_OK);
    expect_pattern(&image);
    otb_free(&image);
}

static void round_trip(unsigned width, unsigned height)
{
    uint8_t source[300 * 3 * 4], row[38], file[OTB_HEADER_MAX + 3 * 38];
    struct otb_image image;
    size_t pos, count, cut;
    unsigned x, y;

    assert(width <= 300 && height <= 3);
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            uint8_t value = ((x * 7u + y * 3u) % 5u) < 2u ? 255 : 0;
            uint8_t *p = source + (y * width + x) * 4u;
            p[0] = p[1] = p[2] = value; p[3] = 255;
        }
    }
    pos = otb_make_header(width, height, file);
    assert(pos == (width > 255u ? 6u : 4u));
    for (y = 0; y < height; y++) {
        count = otb_encode_row(source + y * width * 4u, width, row, sizeof row);
        assert(count == (width + 7u) / 8u);
        memcpy(file + pos, row, count);
        pos += count;
    }
    assert(decode(file, pos, &image) == CODEC_OK);
    assert(image.width == width && image.height == height);
    assert(memcmp(image.rgba, source, (size_t)width * height * 4u) == 0);
    otb_free(&image);
    /* Every shorter file is truncated, unless it is long enough to be
       read as the packed layout, which only happens when width % 8 != 0. */
    for (cut = 0; cut < pos; cut++) {
        enum codec_result result = decode(file, cut, &image);
        assert(result == CODEC_TRUNCATED ||
               (result == CODEC_OK && width % 8u != 0 && height > 1));
        otb_free(&image);
    }
}

int main(void)
{
    struct otb_image image;
    uint8_t header[OTB_HEADER_MAX], row[2], pixel[4];
    unsigned width, count;
    size_t cut;

    /* ImageMagick's layout: each row padded to a byte. Set padding bits are ignored. */
    {
        const uint8_t data[] = {0x00, 3, 2, 1, 0x40, 0xa0};
        const uint8_t noisy[] = {0x00, 3, 2, 1, 0x5f, 0xbf};
        expect_decodes(data, sizeof data);
        expect_decodes(noisy, sizeof noisy);
        /* One byte short is exactly one packed 3x2 bitmap. */
        for (cut = 0; cut < sizeof data - 1; cut++)
            assert(decode(data, cut, &image) == CODEC_TRUNCATED);
        assert(image.rgba == NULL);
    }
    /* The specification's layout: rows run on, only the end is padded. */
    {
        const uint8_t data[] = {0x00, 3, 2, 1, 0x54};
        const uint8_t noisy[] = {0x00, 3, 2, 1, 0x57};
        expect_decodes(data, sizeof data);
        expect_decodes(noisy, sizeof noisy);
        assert(decode(data, sizeof data - 1, &image) == CODEC_TRUNCATED);
    }
    /* 16-bit sizes, big-endian. */
    {
        const uint8_t data[] = {0x10, 0, 3, 0, 2, 1, 0x40, 0xa0};
        expect_decodes(data, sizeof data);
        for (cut = 0; cut < sizeof data - 1; cut++)
            assert(decode(data, cut, &image) == CODEC_TRUNCATED);
    }
    {
        uint8_t data[6 + 2 * 33];
        memset(data, 0xff, sizeof data);
        data[0] = 0x10; data[1] = 0x01; data[2] = 0x02; data[3] = 0; data[4] = 2; data[5] = 1;
        assert(decode(data, sizeof data, &image) == CODEC_OK);
        assert(image.width == 258 && image.height == 2 && is_black(&image, 257, 1));
        otb_free(&image);
        assert(decode(data, sizeof data - 1, &image) == CODEC_OK); /* packed fits */
        otb_free(&image);
        assert(decode(data, 6 + 65 - 1, &image) == CODEC_TRUNCATED);
    }
    /* Extension fields are skipped, whatever their reserved bits hold. */
    {
        const uint8_t one[] = {0x80, 0x10, 3, 2, 1, 0x40, 0xa0};
        const uint8_t three[] = {0x90, 0xff, 0x80, 0x7f, 0, 3, 0, 2, 1, 0x40, 0xa0};
        uint8_t many[16 + 5 + 2];
        expect_decodes(one, sizeof one);
        expect_decodes(three, sizeof three);
        for (cut = 0; cut < sizeof three - 1; cut++)
            assert(decode(three, cut, &image) == CODEC_TRUNCATED);
        memset(many, 0x80, 16);
        many[0] = 0x80; many[15] = 0x00;
        many[16] = 3; many[17] = 2; many[18] = 1; many[19] = 0x40; many[20] = 0xa0;
        expect_decodes(many, 21);
        many[15] = 0x80; many[16] = 0x00;  /* seventeen extension fields */
        assert(decode(many, sizeof many, &image) == CODEC_INVALID);
    }
    /* An external palette follows the image data and is ignored; trailing bytes too. */
    {
        const uint8_t data[] = {0x20, 3, 2, 1, 0x40, 0xa0, 0x12, 0x34, 0x56};
        expect_decodes(data, sizeof data);
    }

    /* Animation: frames follow the main image in the same layout. */
    {
        const uint8_t padded[] = {0x02, 3, 2, 1, 0x40, 0xa0, 0xe0, 0x00, 0x00, 0xe0};
        const uint8_t packed[] = {0x02, 3, 2, 1, 0x54, 0xe0, 0x1c};
        const uint8_t *files[] = {padded, packed};
        size_t lengths[] = {sizeof padded, sizeof packed};
        unsigned f;
        for (f = 0; f < 2; f++) {
            count = 0;
            assert(otb_decode(files[f], lengths[f], 0, &count, &image) == CODEC_OK);
            assert(count == 3);
            expect_pattern(&image);
            otb_free(&image);
            assert(otb_decode(files[f], lengths[f], 1, &count, &image) == CODEC_OK);
            assert(is_black(&image, 0, 0) && is_black(&image, 2, 0));
            assert(!is_black(&image, 0, 1) && !is_black(&image, 2, 1));
            otb_free(&image);
            assert(otb_decode(files[f], lengths[f], 2, &count, &image) == CODEC_OK);
            assert(!is_black(&image, 0, 0) && is_black(&image, 1, 1));
            otb_free(&image);
            count = 0;
            assert(otb_decode(files[f], lengths[f], 3, &count, &image) == CODEC_INVALID);
            assert(count == 3 && image.rgba == NULL);
            assert(otb_decode(files[f], lengths[f], 99, NULL, &image) == CODEC_INVALID);
            /* Every declared frame must be present. */
            assert(otb_decode(files[f], 4 + 2, 0, &count, &image) == CODEC_TRUNCATED);
        }
        /* Fifteen extra frames of 1x1. */
        {
            uint8_t data[4 + 16] = {0x0f, 1, 1, 1};
            data[4 + 15] = 0x80;
            assert(otb_decode(data, sizeof data, 15, &count, &image) == CODEC_OK);
            assert(count == 16 && is_black(&image, 0, 0));
            otb_free(&image);
            assert(otb_decode(data, sizeof data, 14, &count, &image) == CODEC_OK);
            assert(!is_black(&image, 0, 0));
            otb_free(&image);
            assert(otb_decode(data, sizeof data - 1, 0, &count, &image) == CODEC_TRUNCATED);
        }
    }

    /* Header errors. */
    {
        const uint8_t compressed[] = {0x40, 3, 2, 1, 0x40, 0xa0};
        const uint8_t depth0[] = {0x00, 3, 2, 0, 0x40, 0xa0};
        const uint8_t depth2[] = {0x00, 3, 2, 2, 0x40, 0xa0, 0, 0};
        const uint8_t zero_width[] = {0x00, 0, 2, 1, 0};
        const uint8_t zero_height[] = {0x00, 3, 0, 1, 0};
        const uint8_t zero_wide[] = {0x10, 0, 0, 0, 2, 1, 0};
        const uint8_t huge[] = {0x10, 0xff, 0xff, 0xff, 0xff, 1, 0};
        const uint8_t over[] = {0x10, 0x10, 0x00, 0x10, 0x01, 1, 0};
        uint8_t limit[6 + 4096 * 512];
        count = 0;
        assert(otb_decode(compressed, sizeof compressed, 0, &count, &image) == CODEC_INVALID);
        assert(count == 1);
        assert(decode(depth0, sizeof depth0, &image) == CODEC_INVALID);
        assert(decode(depth2, sizeof depth2, &image) == CODEC_INVALID);
        assert(decode(zero_width, sizeof zero_width, &image) == CODEC_INVALID);
        assert(decode(zero_height, sizeof zero_height, &image) == CODEC_INVALID);
        assert(decode(zero_wide, sizeof zero_wide, &image) == CODEC_INVALID);
        assert(decode(huge, sizeof huge, &image) == CODEC_TOO_LARGE);
        assert(decode(over, sizeof over, &image) == CODEC_TOO_LARGE);
        /* 4096x4096 is exactly the limit. */
        memset(limit, 0, sizeof limit);
        limit[0] = 0x10; limit[1] = 0x10; limit[3] = 0x10; limit[5] = 1;
        assert(decode(limit, sizeof limit, &image) == CODEC_OK);
        assert(image.width == 4096 && image.height == 4096 && !is_black(&image, 4095, 4095));
        otb_free(&image);
        assert(decode(limit, sizeof limit - 1, &image) == CODEC_TRUNCATED);
        count = 7;
        assert(otb_decode(NULL, 0, 0, &count, &image) == CODEC_TRUNCATED);
        assert(count == 7 && image.rgba == NULL);
        assert(otb_decode(compressed, sizeof compressed, 0, NULL, NULL) == CODEC_INVALID);
    }

    /* Header encoding: 8-bit sizes when both fit. */
    assert(otb_make_header(255, 255, header) == 4);
    assert(header[0] == 0 && header[1] == 255 && header[2] == 255 && header[3] == 1);
    assert(otb_make_header(256, 1, header) == 6);
    assert(header[0] == 0x10 && header[1] == 1 && header[2] == 0);
    assert(header[3] == 0 && header[4] == 1 && header[5] == 1);
    assert(otb_make_header(1, 65535, header) == 6 && header[3] == 0xff && header[4] == 0xff);
    assert(otb_make_header(0, 1, header) == 0);
    assert(otb_make_header(1, 0, header) == 0);
    assert(otb_make_header(65536, 1, header) == 0);

    /* Threshold and compositing over white; set bits are black. */
    {
        struct { uint8_t r, g, b, a; int black; } cases[] = {
            {255, 255, 255, 255, 0}, {0, 0, 0, 255, 1},
            {128, 128, 128, 255, 0}, {127, 127, 127, 255, 1},
            {0, 0, 0, 0, 0}, {0, 0, 0, 128, 1}, {0, 0, 0, 127, 0},
            {255, 0, 0, 255, 1}, {0, 255, 0, 255, 0}, {0, 0, 255, 255, 1},
        };
        size_t i;
        for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
            pixel[0] = cases[i].r; pixel[1] = cases[i].g;
            pixel[2] = cases[i].b; pixel[3] = cases[i].a;
            row[0] = 0x55;
            assert(otb_encode_row(pixel, 1, row, 1) == 1);
            assert(row[0] == (cases[i].black ? 0x80 : 0));
        }
    }
    assert(otb_encode_row(pixel, 9, row, 1) == 0);
    assert(otb_encode_row(pixel, 0, row, 1) == 0);
    assert(otb_encode_row(NULL, 1, row, 1) == 0);

    for (width = 1; width <= 20; width++)
        round_trip(width, width % 3u + 1u);
    round_trip(255, 3);
    round_trip(256, 2);
    round_trip(300, 3);
    puts("otb codec tests passed");
    return 0;
}
