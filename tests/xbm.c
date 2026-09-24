#include "../formats/xbm/decode.h"
#include "../formats/xbm/encode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static enum codec_result decode(const char *text, struct xbm_image *image)
{
    return xbm_decode((const uint8_t *)text, strlen(text), image);
}

static void expect(const char *text, const char *rows, unsigned width, unsigned height)
{
    struct xbm_image image;
    size_t i;
    assert(decode(text, &image) == CODEC_OK);
    assert(image.width == width && image.height == height);
    for (i = 0; i < (size_t)width * height; i++)
        assert(image.pixels[i] == (rows[i] == '#'));
    xbm_free(&image);
}

static void fails(const char *text, enum codec_result result)
{
    struct xbm_image image;
    assert(decode(text, &image) == result);
    assert(image.pixels == NULL && image.width == 0 && image.hot_x == -1);
}

static void set(uint8_t *p, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    p[0] = r; p[1] = g; p[2] = b; p[3] = a;
}

/* Encode rgba, decode the text again, and check it holds the dark pixels. */
static void round_trip(const uint8_t *rgba, unsigned width, unsigned height,
                       long hot_x, long hot_y, char *text, size_t capacity)
{
    struct xbm_encoder encoder;
    struct xbm_image image;
    size_t pos, size, row = xbm_row_capacity(width);
    unsigned x, y;

    pos = xbm_make_header("pic", width, height, hot_x, hot_y, text, capacity);
    assert(pos != 0);
    xbm_encoder_init(&encoder, width, height);
    for (y = 0; y < height; y++) {
        assert(capacity - pos >= row);
        size = xbm_encode_row(&encoder, rgba + (size_t)y * width * 4u, width,
                              text + pos, row);
        assert(size != SIZE_MAX && size <= row);
        pos += size;
    }
    assert(encoder.remaining == 0);
    assert(memcmp(text + pos - 3, "};\n", 3) == 0);
    assert(xbm_decode((const uint8_t *)text, pos, &image) == CODEC_OK);
    assert(image.width == width && image.height == height);
    assert(image.hot_x == hot_x && image.hot_y == hot_y);
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++) {
            const uint8_t *p = rgba + ((size_t)y * width + x) * 4u;
            assert(image.pixels[(size_t)y * width + x] == (p[0] + p[1] + p[2] == 0 && p[3] == 255));
        }
    xbm_free(&image);
}

int main(void)
{
    static const char x11[] =
        "/* Created by bitmap */\n"
        "#define arrow_width 10\n"
        "#define arrow_height 2\n"
        "#define arrow_x_hot 9\n"
        "#define arrow_y_hot 1\n"
        "static unsigned char arrow_bits[] = {\n"
        "   0x01, 0x02, // row 0\n"
        "   0xff, 0x03, };\n";
    static const char rows[] = "#........#" "##########";
    struct xbm_image image;
    struct xbm_encoder encoder;
    char name[XBM_MAX_NAME + 1], header[XBM_HEADER_MAX], *text, *copy;
    uint8_t rgba[100 * 3 * 4];
    size_t i, length, last, row;
    unsigned x, y;

    /* X11 bitmap with a hotspot, comments and a trailing comma. */
    assert(decode(x11, &image) == CODEC_OK);
    assert(image.width == 10 && image.height == 2);
    assert(image.hot_x == 9 && image.hot_y == 1);
    for (i = 0; i < 20; i++)
        assert(image.pixels[i] == (rows[i] == '#'));
    xbm_free(&image);
    assert(image.pixels == NULL && image.hot_x == -1);

    /* X10 bitmaps hold 16-bit words, low bit first. */
    expect("#define old_width 18\n#define old_height 2\n"
           "static short old_bits[] = {0x8001, 0x0002, 0xffff, 0x0001};",
           "#..............#.#" "#################.", 18, 2);

    /* Decimal and negative values, CRLF, no static, and other C around it. */
    expect("#ifndef ICON_H\r\n#define ICON_H\r\n#define VERSION \"1\"\r\n"
           "#define icon_width 4\r\n#define icon_height 3\r\n"
           "static const int unrelated[] = { 1, { 2 } };\r\n"
           "const signed char icon_bits[3] = { -1, 9, -0 };\r\n#endif\r\n",
           "####" "#..#" "....", 4, 3);
    expect("#define a_width 8\n#define a_height 1\n"
           "static short a_bits[] = { -32768 };",
           "........", 8, 1);

    /* Xlib matches suffixes only, so prefixes may differ. Extra values are ignored. */
    expect("#define width 3\n#define foo_height 1\nchar bar_bits[] = {5, 7, 9};",
           "#.#", 3, 1);
    /* Names from X11's own bitmaps directory start with a digit. */
    expect("#define 1x1_width 2\n#define 1x1_height 1\nstatic char 1x1_bits[] = {0x01};",
           "#.", 2, 1);
    /* Later defines win, as with Xlib, and a line continuation joins lines. */
    expect("#define a_width 9\n#define a_width \\\n 2\n#define a_height 1\n"
           "char a_bits[] = { 0x2 };", ".#", 2, 1);

    /* A hotspot outside the image, or with only one coordinate, is dropped. */
    assert(decode("#define a_width 2\n#define a_height 2\n#define a_x_hot 2\n"
                  "#define a_y_hot 0\nchar a_bits[] = {1, 2};", &image) == CODEC_OK);
    assert(image.hot_x == -1 && image.hot_y == -1);
    xbm_free(&image);
    assert(decode("#define a_width 2\n#define a_height 2\n#define a_x_hot 1\n"
                  "char a_bits[] = {1, 2};", &image) == CODEC_OK);
    assert(image.hot_x == -1 && image.hot_y == -1);
    xbm_free(&image);

    /* Malformed input. */
    assert(xbm_decode(NULL, 0, &image) == CODEC_TRUNCATED);
    assert(xbm_decode((const uint8_t *)"", 0, NULL) == CODEC_INVALID);
    fails("", CODEC_INVALID);
    fails("hello world", CODEC_INVALID);
    fails("#define a_width 8\n#define a_height 1\n", CODEC_TRUNCATED);
    fails("#define a_width 8\n#define a_height 2\nchar a_bits[] = {1};", CODEC_TRUNCATED);
    fails("#define a_width 8\n#define a_height 2\nchar a_bits[] = {1,", CODEC_TRUNCATED);
    fails("#define a_width 8\n#define a_height 2\nchar a_bits[] = {1, }", CODEC_TRUNCATED);
    fails("#define a_width 8\n#define a_height 1\nchar a_bits[] = {1", CODEC_TRUNCATED);
    fails("#define a_width 8\n#define a_height 1\nchar a_bits[] = {1,", CODEC_TRUNCATED);
    fails("#define a_width 8\n#define a_height 1\nchar a_bits[] = {1;", CODEC_INVALID);
    fails("#define a_width 8\n#define a_height 1\n/* unterminated", CODEC_TRUNCATED);
    fails("#define a_width 8\n#define a_height 1\nint x = { 1", CODEC_TRUNCATED);
    fails("#define a_width 8\nchar a_bits[] = {1};", CODEC_INVALID);
    fails("char a_bits[] = {1};\n#define a_width 8\n#define a_height 1\n", CODEC_INVALID);
    fails("#define a_width 0\n#define a_height 1\nchar a_bits[] = {1};", CODEC_INVALID);
    fails("#define a_width 8\n#define a_height 1\nchar a_bits[] = {256};", CODEC_INVALID);
    fails("#define a_width 8\n#define a_height 1\nchar a_bits[] = {-129};", CODEC_INVALID);
    fails("#define a_width 8\n#define a_height 1\nshort a_bits[] = {0x10000};", CODEC_INVALID);
    fails("#define a_width 8\n#define a_height 1\nchar a_bits[] = {0x};", CODEC_INVALID);
    fails("#define a_width 8\n#define a_height 1\nchar a_bits[] = {0x1g};", CODEC_INVALID);
    fails("#define a_width 8\n#define a_height 1\nchar a_bits[] = {a};", CODEC_INVALID);
    fails("#define a_width 8\n#define a_height 2\nchar a_bits[] = {1; 2};", CODEC_INVALID);
    fails("#define a_width 8\n#define a_height 1\n\xc3\xa9", CODEC_INVALID);
    assert(xbm_decode((const uint8_t *)"#define a_width 8\n\0", 19, &image) == CODEC_INVALID);
    fails("#define a_width 65536\n#define a_height 1\nchar a_bits[] = {1};", CODEC_TOO_LARGE);
    fails("#define a_width 99999999999999999999\n#define a_height 1\nchar a_bits[] = {1};",
          CODEC_TOO_LARGE);
    fails("#define a_width 65535\n#define a_height 65535\nchar a_bits[] = {1};", CODEC_TOO_LARGE);
    /* A width define on the next line is not a value. */
    fails("#define a_width\n8\n#define a_height 1\nchar a_bits[] = {1};", CODEC_INVALID);

    /* No prefix of a file decodes until its last value is present. */
    length = strlen(x11);
    last = (size_t)(strstr(x11, "0x03") - x11);
    for (i = 0; i < length; i++) {
        copy = malloc(i + 1);
        assert(copy != NULL);
        memcpy(copy, x11, i);
        if (xbm_decode((const uint8_t *)copy, i, &image) == CODEC_OK) {
            assert(i > last);
            xbm_free(&image);
        }
        free(copy);
    }

    /* Identifiers from file names. */
    xbm_make_name("Work:Pics/my-icon.xbm", name);
    assert(strcmp(name, "my_icon") == 0);
    xbm_make_name("dir/2up.cursor.xbm", name);
    assert(strcmp(name, "_2up_cursor") == 0);
    xbm_make_name(".xbm", name);
    assert(strcmp(name, "_xbm") == 0);
    xbm_make_name("RAM:", name);
    assert(strcmp(name, "image") == 0);
    xbm_make_name(NULL, name);
    assert(strcmp(name, "image") == 0);
    xbm_make_name("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", name);
    assert(strlen(name) == XBM_MAX_NAME);

    /* Headers. */
    assert(xbm_make_header("pic", 16, 2, -1, -1, header, sizeof header) ==
           strlen("#define pic_width 16\n#define pic_height 2\n"
                  "static unsigned char pic_bits[] = {\n"));
    assert(strstr(header, "pic_x_hot") == NULL);
    assert(xbm_make_header("pic", 16, 2, 3, 1, header, sizeof header) != 0);
    assert(strstr(header, "#define pic_x_hot 3\n#define pic_y_hot 1\n") != NULL);
    assert(xbm_make_header("pic", 0, 2, -1, -1, header, sizeof header) == 0);
    assert(xbm_make_header("pic", 65536, 2, -1, -1, header, sizeof header) == 0);
    assert(xbm_make_header("pic", 16, 2, -1, -1, header, 20) == 0);
    memset(name, 'n', XBM_MAX_NAME);
    name[XBM_MAX_NAME] = '\0';
    assert(xbm_make_header(name, 65535, 65535, 65534, 65534, header, sizeof header) != 0);

    /* Round trips: widths that are and are not multiples of eight, line wrapping,
       and colours thresholded after compositing over white. */
    text = malloc(1u << 16);
    assert(text != NULL);
    for (y = 0; y < 3; y++)
        for (x = 0; x < 100; x++) {
            uint8_t *p = rgba + ((size_t)y * 100 + x) * 4u;
            if ((x * 7 + y * 3) % 5 < 2)
                set(p, 0, 0, 0, 255);
            else
                set(p, 255, 255, 255, 255);
        }
    set(rgba, 0, 0, 0, 0);            /* transparent black is background */
    set(rgba + 4, 200, 200, 200, 255);
    set(rgba + 8, 0, 0, 0, 255);
    round_trip(rgba, 100, 3, 5, 2, text, 1u << 16);
    round_trip(rgba, 13, 3, -1, -1, text, 1u << 16);
    round_trip(rgba, 8, 1, 0, 0, text, 1u << 16);
    round_trip(rgba, 1, 1, -1, -1, text, 1u << 16);

    /* Threshold: mid grey and a half-transparent black are both light. */
    set(rgba, 127, 127, 127, 255);
    set(rgba + 4, 128, 128, 128, 255);
    set(rgba + 8, 0, 0, 0, 128);
    set(rgba + 12, 0, 0, 0, 127);
    set(rgba + 16, 0, 0, 255, 255);   /* saturated blue is dark */
    xbm_encoder_init(&encoder, 5, 1);
    row = xbm_encode_row(&encoder, rgba, 5, text, xbm_row_capacity(5));
    assert(row == strlen("   0x15};\n"));
    assert(memcmp(text, "   0x15};\n", row) == 0);

    /* Twelve bytes per line. */
    xbm_encoder_init(&encoder, 104, 2);
    memset(rgba, 255, 104 * 4);
    row = xbm_encode_row(&encoder, rgba, 104, text, xbm_row_capacity(104));
    assert(row != SIZE_MAX);
    text[row] = '\0';
    assert(strncmp(text, "   0x00, ", 9) == 0);
    assert(strstr(text, "0x00,\n   0x00, ") != NULL);
    assert(text[row - 1] == ' ' && encoder.remaining == 13);

    /* Too little space, or more rows than the header promised. */
    xbm_encoder_init(&encoder, 8, 1);
    assert(xbm_encode_row(&encoder, rgba, 8, text, xbm_row_capacity(8) - 1) == SIZE_MAX);
    assert(xbm_encode_row(&encoder, rgba, 8, text, xbm_row_capacity(8)) != SIZE_MAX);
    assert(xbm_encode_row(&encoder, rgba, 8, text, xbm_row_capacity(8)) == SIZE_MAX);
    free(text);

    puts("xbm tests passed");
    return 0;
}
