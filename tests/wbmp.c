#include "../formats/wbmp/decode.h"
#include "../formats/wbmp/encode.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static enum codec_result decode(const uint8_t *data, size_t length, struct wbmp_image *image)
{
    return wbmp_decode(data, length, image);
}

static void expect_size(const uint8_t *data, size_t length, unsigned width, unsigned height)
{
    struct wbmp_image image;
    assert(decode(data, length, &image) == CODEC_OK);
    assert(image.width == width && image.height == height);
    wbmp_free(&image);
}

static int is_white(const struct wbmp_image *image, unsigned x, unsigned y)
{
    const uint8_t *p = image->rgba + ((size_t)y * image->width + x) * 4u;
    assert(p[0] == p[1] && p[1] == p[2] && p[3] == 255);
    assert(p[0] == 0 || p[0] == 255);
    return p[0] == 255;
}

static void round_trip(unsigned width, unsigned height)
{
    uint8_t source[20 * 3 * 4], row[3], file[WBMP_HEADER_MAX + 3 * 3];
    struct wbmp_image image;
    size_t pos, count;
    unsigned x, y;

    assert(width <= 20 && height <= 3);
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            uint8_t value = ((x * 7u + y * 3u) % 5u) < 2u ? 255 : 0;
            uint8_t *p = source + (y * width + x) * 4u;
            p[0] = p[1] = p[2] = value; p[3] = 255;
        }
    }
    pos = wbmp_make_header(width, height, file);
    assert(pos == 4);
    for (y = 0; y < height; y++) {
        count = wbmp_encode_row(source + y * width * 4u, width, row, sizeof row);
        assert(count == (width + 7u) / 8u);
        memcpy(file + pos, row, count);
        pos += count;
    }
    assert(decode(file, pos, &image) == CODEC_OK);
    assert(image.width == width && image.height == height);
    assert(memcmp(image.rgba, source, (size_t)width * height * 4u) == 0);
    wbmp_free(&image);
    assert(decode(file, pos - 1, &image) == CODEC_TRUNCATED);
    assert(image.rgba == NULL);
}

int main(void)
{
    struct wbmp_image image;
    uint8_t header[WBMP_HEADER_MAX], row[2], pixel[4];
    unsigned width;

    /* 3x2: row 0 is white, black, white; row 1 is black, white, black.
       The padding bits are set and must be ignored. */
    {
        const uint8_t data[] = {0, 0, 3, 2, 0xbf, 0x5f};
        assert(decode(data, sizeof data, &image) == CODEC_OK);
        assert(image.width == 3 && image.height == 2);
        assert(is_white(&image, 0, 0) && !is_white(&image, 1, 0) && is_white(&image, 2, 0));
        assert(!is_white(&image, 0, 1) && is_white(&image, 1, 1) && !is_white(&image, 2, 1));
        wbmp_free(&image);
        assert(decode(data, sizeof data - 1, &image) == CODEC_TRUNCATED);
    }

    /* Multi-byte dimensions: 128 is 0x81 0x00. */
    {
        uint8_t data[5 + 16];
        memset(data, 0, sizeof data);
        data[2] = 0x81; data[3] = 0x00; data[4] = 1;
        expect_size(data, sizeof data, 128, 1);
        assert(decode(data, sizeof data - 1, &image) == CODEC_TRUNCATED);
    }
    /* Trailing bytes are ignored. */
    {
        const uint8_t data[] = {0, 0, 1, 1, 0x80, 0xaa, 0xbb};
        assert(decode(data, sizeof data, &image) == CODEC_OK);
        assert(is_white(&image, 0, 0));
        wbmp_free(&image);
    }

    /* Header errors. */
    {
        const uint8_t type1[] = {1, 0, 1, 1, 0};
        const uint8_t ext[] = {0, 0x80, 1, 1, 0};
        const uint8_t fixed[] = {0, 1, 1, 1, 0};
        const uint8_t zero_width[] = {0, 0, 0, 1, 0};
        const uint8_t zero_height[] = {0, 0, 1, 0, 0};
        const uint8_t long_var[] = {0, 0, 0x80, 0x80, 0x80, 0x80, 0x81, 1, 0};
        const uint8_t overflow[] = {0, 0, 0x90, 0x80, 0x80, 0x80, 0x00, 1, 0};
        const uint8_t wide[] = {0, 0, 0x84, 0x80, 0x00, 1};
        const uint8_t huge[] = {0, 0, 0x83, 0xff, 0x7f, 0x83, 0xff, 0x7f};
        const uint8_t cut_var[] = {0, 0, 0x81};
        assert(decode(type1, sizeof type1, &image) == CODEC_INVALID);
        assert(decode(ext, sizeof ext, &image) == CODEC_INVALID);
        assert(decode(fixed, sizeof fixed, &image) == CODEC_INVALID);
        assert(decode(zero_width, sizeof zero_width, &image) == CODEC_INVALID);
        assert(decode(zero_height, sizeof zero_height, &image) == CODEC_INVALID);
        assert(decode(long_var, sizeof long_var, &image) == CODEC_INVALID);
        assert(decode(overflow, sizeof overflow, &image) == CODEC_INVALID);
        assert(decode(wide, sizeof wide, &image) == CODEC_TOO_LARGE);
        assert(decode(huge, sizeof huge, &image) == CODEC_TOO_LARGE);
        assert(decode(cut_var, sizeof cut_var, &image) == CODEC_TRUNCATED);
        assert(decode(cut_var, 1, &image) == CODEC_TRUNCATED);
        assert(decode(cut_var, 0, &image) == CODEC_TRUNCATED);
        assert(decode(NULL, 0, &image) == CODEC_TRUNCATED);
        assert(image.rgba == NULL);
        assert(wbmp_decode(cut_var, sizeof cut_var, NULL) == CODEC_INVALID);
    }

    /* Header encoding at uintvar boundaries. */
    assert(wbmp_make_header(127, 1, header) == 4 && header[2] == 0x7f);
    assert(wbmp_make_header(128, 1, header) == 5 && header[2] == 0x81 && header[3] == 0);
    assert(wbmp_make_header(16383, 16384, header) == 7);
    assert(header[2] == 0xff && header[3] == 0x7f);
    assert(header[4] == 0x81 && header[5] == 0x80 && header[6] == 0x00);
    assert(wbmp_make_header(65535, 65535, header) == 8);
    assert(wbmp_make_header(0, 1, header) == 0);
    assert(wbmp_make_header(1, 65536, header) == 0);

    /* Threshold and compositing over white. */
    {
        struct { uint8_t r, g, b, a; int white; } cases[] = {
            {255, 255, 255, 255, 1}, {0, 0, 0, 255, 0},
            {128, 128, 128, 255, 1}, {127, 127, 127, 255, 0},
            {0, 0, 0, 0, 1}, {0, 0, 0, 128, 0}, {0, 0, 0, 127, 1},
            {255, 0, 0, 255, 0}, {0, 255, 0, 255, 1}, {0, 0, 255, 255, 0},
        };
        size_t i;
        for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
            pixel[0] = cases[i].r; pixel[1] = cases[i].g;
            pixel[2] = cases[i].b; pixel[3] = cases[i].a;
            row[0] = 0x55;
            assert(wbmp_encode_row(pixel, 1, row, 1) == 1);
            assert(row[0] == (cases[i].white ? 0x80 : 0));
        }
    }
    assert(wbmp_encode_row(pixel, 9, row, 1) == 0);
    assert(wbmp_encode_row(pixel, 0, row, 1) == 0);
    assert(wbmp_encode_row(NULL, 1, row, 1) == 0);

    for (width = 1; width <= 20; width++)
        round_trip(width, width % 3u + 1u);
    puts("wbmp codec tests passed");
    return 0;
}
