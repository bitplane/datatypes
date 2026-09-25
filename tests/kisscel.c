#include "../formats/kisscel/decode.h"
#include "../formats/kisscel/encode.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t data[32 + 4 * 64 * 4];

static size_t old_header(unsigned width, unsigned height)
{
    data[0] = (uint8_t)width; data[1] = (uint8_t)(width >> 8);
    data[2] = (uint8_t)height; data[3] = (uint8_t)(height >> 8);
    return 4;
}

static size_t new_header(unsigned mark, unsigned bpp, unsigned width,
                         unsigned height, unsigned x, unsigned y)
{
    memset(data, 0, 32);
    memcpy(data, "KiSS", 4);
    data[4] = (uint8_t)mark; data[5] = (uint8_t)bpp;
    data[8] = (uint8_t)width; data[9] = (uint8_t)(width >> 8);
    data[10] = (uint8_t)height; data[11] = (uint8_t)(height >> 8);
    data[12] = (uint8_t)x; data[13] = (uint8_t)(x >> 8);
    data[14] = (uint8_t)y; data[15] = (uint8_t)(y >> 8);
    return 32;
}

static void pixel(const struct kisscel_image *image, unsigned x, unsigned y,
                  unsigned r, unsigned g, unsigned b, unsigned a)
{
    const uint8_t *p = image->rgba + ((size_t)y * image->width + x) * 4u;
    assert(p[0] == r && p[1] == g && p[2] == b && p[3] == a);
}

static void test_cels(void)
{
    struct kisscel_image image;
    struct kisscel_palette palette;
    struct kisscel_info info;
    size_t n;
    unsigned i;

    palette.count = 16;
    for (i = 0; i < 16; i++) {
        palette.rgb[i * 3u] = (uint8_t)(i * 10u);
        palette.rgb[i * 3u + 1u] = (uint8_t)(i * 5u);
        palette.rgb[i * 3u + 2u] = (uint8_t)(255u - i);
    }

    /* Old 4-bit cels: high nibble first, odd rows padded to a byte. */
    n = old_header(3, 2);
    data[n++] = 0x12; data[n++] = 0x30;
    data[n++] = 0x0f; data[n++] = 0xe5;
    assert(kisscel_info(data, n, &info) == CODEC_OK);
    assert(info.bpp == 4 && info.width == 3 && info.height == 2);
    assert(kisscel_decode(data, n, &palette, &image) == CODEC_OK);
    assert(image.width == 3 && image.height == 2);
    pixel(&image, 0, 0, 10, 5, 254, 255);
    pixel(&image, 1, 0, 20, 10, 253, 255);
    pixel(&image, 2, 0, 30, 15, 252, 255);
    pixel(&image, 0, 1, 0, 0, 0, 0);
    pixel(&image, 1, 1, 150, 75, 240, 255);
    pixel(&image, 2, 1, 140, 70, 241, 255);
    kisscel_free(&image);
    /* Without a palette, a grey ramp of i * 16, as GIMP shows. */
    assert(kisscel_decode(data, n, NULL, &image) == CODEC_OK);
    pixel(&image, 0, 0, 16, 16, 16, 255);
    pixel(&image, 1, 1, 240, 240, 240, 255);
    pixel(&image, 0, 1, 0, 0, 0, 0);
    kisscel_free(&image);
    /* Trailing data is ignored; a missing byte is truncation. */
    assert(kisscel_decode(data, n + 5, NULL, &image) == CODEC_OK);
    kisscel_free(&image);
    for (i = 0; i < n; i++)
        assert(kisscel_decode(data, i, NULL, &image) == CODEC_TRUNCATED &&
               image.rgba == NULL);

    /* New 4-bit cels with offsets: the canvas grows to hold them. */
    n = new_header(0x20, 4, 1, 2, 2, 1);
    data[n++] = 0x30; data[n++] = 0x00;
    assert(kisscel_decode(data, n, &palette, &image) == CODEC_OK);
    assert(image.width == 3 && image.height == 3);
    pixel(&image, 2, 1, 30, 15, 252, 255);
    pixel(&image, 2, 2, 0, 0, 0, 0);
    pixel(&image, 0, 0, 0, 0, 0, 0);
    pixel(&image, 1, 1, 0, 0, 0, 0);
    kisscel_free(&image);
    for (i = 0; i < n; i++)
        assert(kisscel_decode(data, i, NULL, &image) == CODEC_TRUNCATED);

    /* 8-bit: an index the palette lacks is opaque black. */
    n = new_header(0x20, 8, 4, 1, 0, 0);
    data[n++] = 0; data[n++] = 15; data[n++] = 16; data[n++] = 255;
    assert(kisscel_decode(data, n, &palette, &image) == CODEC_OK);
    pixel(&image, 0, 0, 0, 0, 0, 0);
    pixel(&image, 1, 0, 150, 75, 240, 255);
    pixel(&image, 2, 0, 0, 0, 0, 255);
    pixel(&image, 3, 0, 0, 0, 0, 255);
    kisscel_free(&image);
    /* The 8-bit grey ramp is the index. */
    assert(kisscel_decode(data, n, NULL, &image) == CODEC_OK);
    pixel(&image, 2, 0, 16, 16, 16, 255);
    pixel(&image, 3, 0, 255, 255, 255, 255);
    kisscel_free(&image);
    assert(kisscel_decode(data, n - 1, NULL, &image) == CODEC_TRUNCATED);

    /* 32-bit: BGRA, straight alpha kept even at zero, marks 0x20 and 0x21. */
    for (i = 0x20; i <= 0x21; i++) {
        n = new_header(i, 32, 2, 1, 1, 0);
        data[n++] = 1; data[n++] = 2; data[n++] = 3; data[n++] = 0;
        data[n++] = 10; data[n++] = 20; data[n++] = 30; data[n++] = 128;
        assert(kisscel_decode(data, n, &palette, &image) == CODEC_OK);
        assert(image.width == 3 && image.height == 1);
        pixel(&image, 0, 0, 0, 0, 0, 0);
        pixel(&image, 1, 0, 3, 2, 1, 0);
        pixel(&image, 2, 0, 30, 20, 10, 128);
        kisscel_free(&image);
        assert(kisscel_decode(data, n - 1, NULL, &image) == CODEC_TRUNCATED);
    }

    /* Bad marks and depths, and empty sizes. */
    n = new_header(0x10, 8, 1, 1, 0, 0);
    data[n++] = 1;
    assert(kisscel_decode(data, n, NULL, &image) == CODEC_INVALID);
    new_header(0x22, 8, 1, 1, 0, 0);
    assert(kisscel_decode(data, n, NULL, &image) == CODEC_INVALID);
    new_header(0x20, 24, 1, 1, 0, 0);
    assert(kisscel_decode(data, n + 3, NULL, &image) == CODEC_INVALID);
    new_header(0x20, 1, 1, 1, 0, 0);
    assert(kisscel_decode(data, n, NULL, &image) == CODEC_INVALID);
    new_header(0x20, 8, 0, 1, 0, 0);
    assert(kisscel_decode(data, n, NULL, &image) == CODEC_INVALID);
    new_header(0x20, 8, 1, 0, 0, 0);
    assert(kisscel_decode(data, n, NULL, &image) == CODEC_INVALID);
    old_header(0, 5);
    assert(kisscel_decode(data, 40, NULL, &image) == CODEC_INVALID);
    /* Reserved bytes are ignored. */
    new_header(0x20, 8, 1, 1, 0, 0);
    data[6] = 1; data[20] = 0xff; data[32] = 1;
    assert(kisscel_decode(data, 33, NULL, &image) == CODEC_OK);
    kisscel_free(&image);

    /* Limits apply to the canvas, offsets included, before reading pixels. */
    new_header(0x20, 8, 65535, 1, 0, 0);
    assert(kisscel_info(data, 33, &info) == CODEC_TRUNCATED);
    new_header(0x20, 8, 65535, 1, 1, 0);
    assert(kisscel_info(data, 33, &info) == CODEC_TOO_LARGE);
    new_header(0x20, 8, 1, 65535, 0, 1);
    assert(kisscel_info(data, 33, &info) == CODEC_TOO_LARGE);
    new_header(0x20, 8, 4096, 4096, 0, 0);
    assert(kisscel_info(data, 33, &info) == CODEC_TRUNCATED);
    new_header(0x20, 8, 4096, 4096, 1, 0);
    assert(kisscel_info(data, 33, &info) == CODEC_TOO_LARGE);
    new_header(0x20, 32, 65535, 65535, 65535, 65535);
    assert(kisscel_info(data, 33, &info) == CODEC_TOO_LARGE);
    old_header(65535, 65535);
    assert(kisscel_info(data, 33, &info) == CODEC_TOO_LARGE);
    old_header(4096, 4096);
    assert(kisscel_info(data, 33, &info) == CODEC_TRUNCATED);
}

static void test_palettes(void)
{
    struct kisscel_palette palette;
    uint8_t kcf[32 + 3 * 256 * 2];
    unsigned i;

    /* Old files: headerless groups of 16 12-bit colours, rrrrbbbb 0000gggg. */
    memset(kcf, 0, sizeof kcf);
    kcf[2] = 0xf1; kcf[3] = 0x0a;
    kcf[32 + 2] = 0x23; kcf[32 + 3] = 0x04;
    assert(kisscel_palette(kcf, 64, 0, &palette) == CODEC_OK);
    assert(palette.count == 16);
    assert(palette.rgb[3] == 0xf0 && palette.rgb[4] == 0xa0 &&
           palette.rgb[5] == 0x10);
    assert(kisscel_palette(kcf, 64, 1, &palette) == CODEC_OK);
    assert(palette.rgb[3] == 0x20 && palette.rgb[4] == 0x40 &&
           palette.rgb[5] == 0x30);
    /* A group the file lacks, whole or in part, is the first. */
    assert(kisscel_palette(kcf, 63, 1, &palette) == CODEC_OK);
    assert(palette.rgb[3] == 0xf0);
    assert(kisscel_palette(kcf, 64, 9, &palette) == CODEC_OK);
    assert(palette.rgb[3] == 0xf0);
    assert(kisscel_palette(kcf, 31, 0, &palette) == CODEC_TRUNCATED);
    assert(kisscel_palette(kcf, 0, 0, &palette) == CODEC_TRUNCATED);

    /* New 24-bit files with two groups of 16. */
    memset(kcf, 0, sizeof kcf);
    memcpy(kcf, "KiSS", 4);
    kcf[4] = 0x10; kcf[5] = 24; kcf[8] = 16; kcf[10] = 2;
    for (i = 0; i < 96; i++)
        kcf[32 + i] = (uint8_t)i;
    assert(kisscel_palette(kcf, 32 + 96, 1, &palette) == CODEC_OK);
    assert(palette.count == 16 && palette.rgb[0] == 48 && palette.rgb[47] == 95);
    assert(kisscel_palette(kcf, 32 + 95, 1, &palette) == CODEC_OK);
    assert(palette.rgb[0] == 0 && palette.rgb[47] == 47);
    assert(kisscel_palette(kcf, 32 + 47, 0, &palette) == CODEC_TRUNCATED);
    for (i = 0; i < 32; i++)
        assert(kisscel_palette(kcf, i, 0, &palette) == CODEC_TRUNCATED);
    /* 256 colours of 12 bits. */
    kcf[5] = 12; kcf[8] = 0; kcf[9] = 1; kcf[10] = 1;
    kcf[32 + 510] = 0x5a; kcf[32 + 511] = 0xf7;
    assert(kisscel_palette(kcf, 32 + 512, 0, &palette) == CODEC_OK);
    assert(palette.count == 256);
    assert(palette.rgb[765] == 0x50 && palette.rgb[766] == 0x70 &&
           palette.rgb[767] == 0xa0);
    assert(kisscel_palette(kcf, 32 + 511, 0, &palette) == CODEC_TRUNCATED);
    /* Reserved values. */
    kcf[5] = 16;
    assert(kisscel_palette(kcf, sizeof kcf, 0, &palette) == CODEC_INVALID);
    kcf[5] = 12; kcf[8] = 17; kcf[9] = 0;
    assert(kisscel_palette(kcf, sizeof kcf, 0, &palette) == CODEC_INVALID);
    kcf[8] = 16; kcf[10] = 0;
    assert(kisscel_palette(kcf, sizeof kcf, 0, &palette) == CODEC_INVALID);
    kcf[10] = 1; kcf[4] = 0x20;
    assert(kisscel_palette(kcf, sizeof kcf, 0, &palette) == CODEC_INVALID);
}

static int lookup(const char *cnf, const char *cel, const char *want,
                  unsigned want_group)
{
    const char *kcf;
    size_t length;
    unsigned group = 99;

    if (!kisscel_cnf_palette(cnf, strlen(cnf), cel, &kcf, &length, &group))
        return want == NULL;
    return want != NULL && length == strlen(want) &&
           memcmp(kcf, want, length) == 0 && group == want_group;
}

static void test_cnf(void)
{
    const char *cnf =
        "; a doll\r\n"
        "=640,480 ; screen\r\n"
        "%base.kcf ; body\r\n"
        "%Hair.KCF\r\n"
        "%third.kcf;x\r\n"
        "[0\r\n"
        "#1 body.cel ; no palette\r\n"
        "#2.99 HAIR.CEL *1 : 2 3 ;; hair\r\n"
        "#3 dir\\shoe.cel*2:4\r\n"
        "#4 hat.cel *7\r\n"
        "#5   bow.cel\t*  1\t:\t5 1\r\n"
        "#6 body.cel *2\r\n"
        "#7 cap.cel :\r\n"
        "$0 10,10 20,20\r\n"
        " 30,30\r\n"
        "$4 *\r\n"
        "$2\r\n"
        "$\r\n"
        "$6 1,1\r\n"
        "$5\n";

    assert(lookup(cnf, "body.cel", "base.kcf", 0));
    assert(lookup(cnf, "hair.cel", "Hair.KCF", 2));
    assert(lookup(cnf, "shoe.cel", "third.kcf", 6));
    assert(lookup(cnf, "hat.cel", NULL, 0));
    assert(lookup(cnf, "bow.cel", "Hair.KCF", 4));
    assert(lookup(cnf, "cap.cel", "base.kcf", 0));
    assert(lookup(cnf, "missing.cel", NULL, 0));
    assert(lookup(cnf, "body", NULL, 0));
    assert(lookup(cnf, "ody.cel", NULL, 0));
    /* A set with no $ line is group 0. */
    assert(lookup("%a.kcf\n#1 x.cel : 9\n$3\n", "x.cel", "a.kcf", 0));
    assert(lookup("%a.kcf\n#1 x.cel\n", "x.cel", "a.kcf", 0));
    assert(lookup("#1 x.cel\n", "x.cel", NULL, 0));
    assert(lookup("", "x.cel", NULL, 0));
    assert(lookup("%\n#\n$\n#1\n#1 \n*\n:", "x.cel", NULL, 0));
    /* Ends without a newline, even mid-field. */
    assert(lookup("%a.kcf\n%b.kcf\n#1 x.cel *1", "x.cel", "b.kcf", 0));
    assert(lookup("%a.kcf\n%b.kcf\n#1 x.cel *", "x.cel", "a.kcf", 0));
    assert(lookup("%a.kcf\n#1 x.cel :", "x.cel", "a.kcf", 0));
    /* Huge numbers don't wrap. */
    assert(lookup("%a.kcf\n#1 x.cel *99999999999999999999", "x.cel", NULL, 0));
}

static void test_encode(void)
{
    struct kisscel_image image;
    uint8_t rgba[3 * 4] = { 1, 2, 3, 0, 250, 128, 7, 255, 9, 8, 7, 100 };
    unsigned y;
    size_t n;

    assert(!kisscel_make_header(0, 1, data));
    assert(!kisscel_make_header(1, 0, data));
    assert(!kisscel_make_header(65536, 1, data));
    assert(!kisscel_make_header(1, 65536, data));
    assert(kisscel_make_header(3, 2, data));
    assert(memcmp(data, "KiSS\x21\x20\0\0\3\0\2\0", 12) == 0);
    for (n = 12; n < 32; n++)
        assert(data[n] == 0);
    for (y = 0; y < 2; y++)
        kisscel_encode_row(rgba, 3, data + 32 + y * 12u);
    assert(data[32] == 3 && data[33] == 2 && data[34] == 1 && data[35] == 0);
    assert(kisscel_decode(data, 32 + 24, NULL, &image) == CODEC_OK);
    assert(image.width == 3 && image.height == 2);
    assert(memcmp(image.rgba, rgba, 12) == 0);
    assert(memcmp(image.rgba + 12, rgba, 12) == 0);
    kisscel_free(&image);
    assert(kisscel_make_header(65535, 65535, data));
    assert(data[8] == 0xff && data[9] == 0xff && data[10] == 0xff);
}

int main(void)
{
    test_cels();
    test_palettes();
    test_cnf();
    test_encode();
    puts("kisscel: ok");
    return 0;
}
