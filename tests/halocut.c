#include "../formats/halocut/decode.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t data[4096];
static uint8_t pal[2048];
static size_t n;
static size_t row_start;

static void header(unsigned width, unsigned height)
{
    data[0] = (uint8_t)width; data[1] = (uint8_t)(width >> 8);
    data[2] = (uint8_t)height; data[3] = (uint8_t)(height >> 8);
    data[4] = data[5] = 0;
    n = 6;
}

static void put(unsigned byte) { data[n++] = (uint8_t)byte; }

/* Start a row; end_row fills in its size. */
static void row(void) { row_start = n; n += 2; }

static void end_row(void)
{
    size_t size;
    put(0);
    size = n - row_start - 2;
    data[row_start] = (uint8_t)size;
    data[row_start + 1] = (uint8_t)(size >> 8);
}

static void pixel(const struct halocut_image *image, unsigned x, unsigned y,
                  unsigned r, unsigned g, unsigned b)
{
    const uint8_t *p = image->rgba + ((size_t)y * image->width + x) * 4u;
    assert(p[0] == r && p[1] == g && p[2] == b && p[3] == 255);
}

static void grey(const struct halocut_image *image, unsigned x, unsigned y,
                 unsigned v)
{
    pixel(image, x, y, v, v, v);
}

static void test_8bit(void)
{
    struct halocut_image image;
    struct halocut_palette palette;
    size_t i, whole;

    /* A literal, a run, a run clamped at the row's end. */
    header(5, 2);
    row(); put(2); put(10); put(20); put(0x83); put(30); end_row();
    row(); put(0x81); put(40); put(0x8f); put(50); end_row();
    assert(halocut_decode(data, n, NULL, &image) == CODEC_OK);
    assert(image.width == 5 && image.height == 2);
    grey(&image, 0, 0, 10); grey(&image, 1, 0, 20);
    grey(&image, 2, 0, 30); grey(&image, 4, 0, 30);
    grey(&image, 0, 1, 40); grey(&image, 1, 1, 50); grey(&image, 4, 1, 50);
    halocut_free(&image);

    /* With a palette; indices past it take entry 0. */
    palette.count = 21;
    memset(palette.rgb, 0, sizeof palette.rgb);
    palette.rgb[0] = 1; palette.rgb[1] = 2; palette.rgb[2] = 3;
    palette.rgb[30] = 100; palette.rgb[31] = 110; palette.rgb[32] = 120;
    palette.rgb[60] = 200; palette.rgb[61] = 210; palette.rgb[62] = 220;
    assert(halocut_decode(data, n, &palette, &image) == CODEC_OK);
    pixel(&image, 0, 0, 100, 110, 120);
    pixel(&image, 1, 0, 200, 210, 220);
    pixel(&image, 2, 0, 1, 2, 3);
    halocut_free(&image);

    /* Trailing data is ignored; every shorter length is truncated. */
    whole = n;
    assert(halocut_decode(data, whole + 7, NULL, &image) == CODEC_OK);
    halocut_free(&image);
    for (i = 0; i < whole; i++)
        assert(halocut_decode(data, i, NULL, &image) == CODEC_TRUNCATED &&
               image.rgba == NULL);

    /* A literal past the row's end is skipped whole; 0x80 also ends a row;
       a short row keeps the previous row's tail. */
    header(4, 3);
    row(); put(4); put(1); put(2); put(3); put(4); end_row();
    row(); put(6); put(5); put(6); put(7); put(8); put(9); put(10);
    put(0x81); put(11); put(0x80);
    row(); put(0x82); put(12); end_row();
    assert(halocut_decode(data, n, NULL, &image) == CODEC_OK);
    grey(&image, 0, 1, 5); grey(&image, 3, 1, 8);
    grey(&image, 0, 2, 12); grey(&image, 1, 2, 12);
    grey(&image, 2, 2, 7); grey(&image, 3, 2, 8);
    halocut_free(&image);

    /* A row decoding shorter than any depth is 8-bit; the rest stays 0. */
    header(5, 1);
    row(); put(3); put(9); put(8); put(7); end_row();
    assert(halocut_decode(data, n, NULL, &image) == CODEC_OK);
    grey(&image, 2, 0, 7); grey(&image, 3, 0, 0); grey(&image, 4, 0, 0);
    halocut_free(&image);
}

static void test_mono(void)
{
    struct halocut_image image;

    /* Without a palette, pictures using only 0 and 1 are black and white. */
    header(3, 1);
    row(); put(3); put(0); put(1); put(1); end_row();
    assert(halocut_decode(data, n, NULL, &image) == CODEC_OK);
    grey(&image, 0, 0, 0); grey(&image, 1, 0, 255);
    halocut_free(&image);
    /* but not when another index appears anywhere. */
    header(3, 2);
    row(); put(3); put(0); put(1); put(1); end_row();
    row(); put(0x83); put(2); end_row();
    assert(halocut_decode(data, n, NULL, &image) == CODEC_OK);
    grey(&image, 1, 0, 1); grey(&image, 0, 1, 2);
    halocut_free(&image);
}

static void test_packed(void)
{
    struct halocut_image image;
    struct halocut_palette palette;

    /* 4-bit when the first row holds half as many bytes as pixels. */
    header(4, 2);
    row(); put(2); put(0x1f); put(0x80); end_row();
    row(); put(0x82); put(0x3c); end_row();
    assert(halocut_decode(data, n, NULL, &image) == CODEC_OK);
    grey(&image, 0, 0, 1); grey(&image, 1, 0, 15);
    grey(&image, 2, 0, 8); grey(&image, 3, 0, 0);
    grey(&image, 0, 1, 3); grey(&image, 1, 1, 12); grey(&image, 3, 1, 12);
    halocut_free(&image);

    /* 1-bit when it holds an eighth, most significant bit first. */
    header(16, 1);
    row(); put(2); put(0x81); put(0x40); end_row();
    assert(halocut_decode(data, n, NULL, &image) == CODEC_OK);
    grey(&image, 0, 0, 255); grey(&image, 1, 0, 0); grey(&image, 7, 0, 255);
    grey(&image, 8, 0, 0); grey(&image, 9, 0, 255); grey(&image, 15, 0, 0);
    halocut_free(&image);
    palette.count = 2;
    palette.rgb[0] = 10; palette.rgb[1] = 20; palette.rgb[2] = 30;
    palette.rgb[3] = 40; palette.rgb[4] = 50; palette.rgb[5] = 60;
    assert(halocut_decode(data, n, &palette, &image) == CODEC_OK);
    pixel(&image, 0, 0, 40, 50, 60); pixel(&image, 1, 0, 10, 20, 30);
    halocut_free(&image);
}

static void test_header(void)
{
    struct halocut_image image;

    header(0, 1);
    row(); end_row();
    assert(halocut_decode(data, n, NULL, &image) == CODEC_INVALID);
    header(1, 0);
    assert(halocut_decode(data, n, NULL, &image) == CODEC_INVALID);
    header(1, 1);
    data[4] = 1;
    row(); put(1); put(1); end_row();
    assert(halocut_decode(data, n, NULL, &image) == CODEC_INVALID);
    data[4] = 0; data[5] = 1;
    assert(halocut_decode(data, n, NULL, &image) == CODEC_INVALID);
    /* 65535 x 257 is over 16M pixels; 65535 x 256 is not. */
    header(65535, 257);
    assert(halocut_decode(data, n, NULL, &image) == CODEC_TOO_LARGE);
    header(65535, 256);
    row(); end_row();
    assert(halocut_decode(data, n, NULL, &image) == CODEC_TRUNCATED &&
           image.rgba == NULL);
}

static void le16(size_t at, unsigned v)
{
    pal[at] = (uint8_t)v;
    pal[at + 1] = (uint8_t)(v >> 8);
}

static size_t pal_header(unsigned max_index, unsigned r, unsigned g,
                         unsigned b)
{
    memset(pal, 0, sizeof pal);
    pal[0] = 'A'; pal[1] = 'H';
    le16(2, 0xe3);
    pal[6] = 0x0a;
    le16(12, max_index);
    le16(14, r); le16(16, g); le16(18, b);
    return HALOCUT_PAL_HEADER;
}

static void test_palette(void)
{
    struct halocut_palette palette;
    size_t at, i;

    /* Entries skip to the next 512-byte block rather than straddle one. */
    at = pal_header(255, 255, 255, 255);
    for (i = 0; i < 256; i++) {
        if (at % 512 > 506)
            at += 512 - at % 512;
        le16(at, i); le16(at + 2, 255 - i); le16(at + 4, i / 2);
        at += 6;
    }
    assert(at <= sizeof pal);
    assert(halocut_palette(pal, at, &palette) == CODEC_OK);
    assert(palette.count == 256);
    for (i = 0; i < 256; i++)
        assert(palette.rgb[i * 3] == i && palette.rgb[i * 3 + 1] == 255 - i &&
               palette.rgb[i * 3 + 2] == i / 2);
    /* The tail of a short file is black. */
    assert(halocut_palette(pal, 600, &palette) == CODEC_OK);
    assert(palette.rgb[3 * 70] == 70 && palette.rgb[3 * 200] == 0 &&
           palette.rgb[3 * 200 + 1] == 0);

    /* Components scale by their maximum, rounding through 16 bits: 0 is
       16-bit, 63 is VGA, values past the maximum clamp. */
    at = pal_header(3, 63, 0, 100);
    le16(at, 63); le16(at + 2, 65535); le16(at + 4, 200);
    le16(at + 6, 31); le16(at + 8, 21261); le16(at + 10, 50);
    le16(at + 12, 1); le16(at + 14, 128); le16(at + 16, 1);
    assert(halocut_palette(pal, at + 24, &palette) == CODEC_OK);
    assert(palette.count == 4);
    assert(palette.rgb[0] == 255 && palette.rgb[1] == 255 &&
           palette.rgb[2] == 255);
    assert(palette.rgb[3] == 125 && palette.rgb[4] == 83 &&
           palette.rgb[5] == 128);
    assert(palette.rgb[6] == 4 && palette.rgb[7] == 0 && palette.rgb[8] == 3);
    assert(palette.rgb[9] == 0);

    /* More than 256 entries: only the first 256 matter. */
    pal_header(1000, 255, 255, 255);
    assert(halocut_palette(pal, sizeof pal, &palette) == CODEC_OK &&
           palette.count == 256);

    /* Not a palette. */
    pal_header(0, 255, 255, 255);
    assert(halocut_palette(pal, sizeof pal, &palette) == CODEC_INVALID);
    pal_header(15, 255, 255, 255);
    assert(halocut_palette(pal, HALOCUT_PAL_HEADER - 1, &palette) ==
           CODEC_INVALID);
    assert(halocut_palette(pal, HALOCUT_PAL_HEADER, &palette) == CODEC_OK);
    pal[1] = 'h';
    assert(halocut_palette(pal, sizeof pal, &palette) == CODEC_INVALID);
}

int main(void)
{
    test_8bit();
    test_mono();
    test_packed();
    test_header();
    test_palette();
    puts("halocut ok");
    return 0;
}
