#include "../formats/xpm/colors.h"
#include "../formats/xpm/decode.h"
#include "../formats/xpm/encode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static enum codec_result decode(const char *text, struct xpm_image *image)
{
    return xpm_decode((const uint8_t *)text, strlen(text), image);
}

static uint32_t pixel(const struct xpm_image *image, unsigned x, unsigned y)
{
    const uint8_t *p = image->rgba + ((size_t)y * image->width + x) * 4u;
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* Decode text and compare each pixel with RRGGBBAA values in row order. */
static void expect(const char *text, unsigned width, unsigned height,
                   const uint32_t *pixels)
{
    struct xpm_image image;
    unsigned x, y;
    assert(decode(text, &image) == CODEC_OK);
    assert(image.width == width && image.height == height);
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++) {
            if (pixel(&image, x, y) != pixels[y * width + x])
                fprintf(stderr, "pixel %u,%u is %08lx\n", x, y,
                        (unsigned long)pixel(&image, x, y));
            assert(pixel(&image, x, y) == pixels[y * width + x]);
        }
    xpm_free(&image);
}

/* A one-pixel XPM3 file with the given colour line after the code "a". */
static enum codec_result one(const char *color_line, uint32_t *value)
{
    char text[256];
    struct xpm_image image;
    enum codec_result result;
    snprintf(text, sizeof text,
             "/* XPM */\nstatic char *x[] = {\n\"1 1 2 1\",\n\"a %s\",\n\"b c red\",\n\"a\"};\n",
             color_line);
    result = decode(text, &image);
    if (result == CODEC_OK) {
        *value = pixel(&image, 0, 0);
        xpm_free(&image);
    } else {
        assert(image.rgba == NULL && image.width == 0 && image.hot_x == -1);
    }
    return result;
}

static uint32_t color(const char *color_line)
{
    uint32_t value = 0;
    assert(one(color_line, &value) == CODEC_OK);
    return value;
}

static void fails(const char *text, enum codec_result result)
{
    struct xpm_image image;
    assert(decode(text, &image) == result);
    assert(image.rgba == NULL && image.width == 0 && image.hot_x == -1);
}

/* Every prefix that cuts into the pixels or earlier must fail cleanly;
   last_row is the final pixel string, quotes included. */
static void every_prefix_fails(const char *text, const char *last_row)
{
    size_t length = strlen(text), n;
    for (n = 0; n < length; n++) {
        struct xpm_image image;
        char *copy = malloc(n + 1u);
        enum codec_result result;
        assert(copy != NULL);
        memcpy(copy, text, n);
        result = xpm_decode((const uint8_t *)copy, n, &image);
        /* Anything after the last row is not needed. */
        if (result == CODEC_OK) {
            assert(n >= (size_t)(strstr(text, last_row) - text) + strlen(last_row));
            xpm_free(&image);
        } else {
            assert(result == CODEC_TRUNCATED || result == CODEC_INVALID);
            assert(image.rgba == NULL);
        }
        free(copy);
    }
}

static void set(uint8_t *p, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    p[0] = r; p[1] = g; p[2] = b; p[3] = a;
}

/* Encode rgba, decode the text again, and compare. Returns the cpp used. */
static unsigned round_trip(const uint8_t *rgba, unsigned width, unsigned height,
                           long hot_x, long hot_y)
{
    struct xpm_encoder encoder;
    struct xpm_image image;
    size_t capacity, pos, size, i, row;
    unsigned x, y, cpp;
    int transparent = 0;
    char *text;

    xpm_encoder_init(&encoder);
    for (y = 0; y < height; y++)
        assert(xpm_encoder_add_row(&encoder, rgba + (size_t)y * width * 4u, width) == CODEC_OK);
    xpm_encoder_finish(&encoder, height);
    row = xpm_row_capacity(&encoder, width);
    capacity = XPM_HEADER_MAX + encoder.count * XPM_COLOR_LINE_MAX + height * row;
    text = malloc(capacity);
    assert(text != NULL);
    pos = xpm_make_header(&encoder, "pic", width, height, hot_x, hot_y, text, XPM_HEADER_MAX);
    assert(pos != 0);
    for (i = 0; i < encoder.count; i++) {
        size = xpm_color_line(&encoder, i, text + pos, XPM_COLOR_LINE_MAX);
        assert(size != 0);
        pos += size;
    }
    for (y = 0; y < height; y++) {
        size = xpm_encode_row(&encoder, rgba + (size_t)y * width * 4u, width, text + pos, row);
        assert(size != SIZE_MAX && size <= row);
        pos += size;
    }
    assert(xpm_encode_row(&encoder, rgba, width, text + pos, row) == SIZE_MAX);
    assert(memcmp(text + pos - 4, "\n};\n", 4) == 0);
    cpp = encoder.cpp;
    xpm_encoder_free(&encoder);

    assert(xpm_decode((const uint8_t *)text, pos, &image) == CODEC_OK);
    assert(image.width == width && image.height == height);
    assert(image.hot_x == hot_x && image.hot_y == hot_y);
    for (i = 0; i < (size_t)width * height; i++) {
        const uint8_t *p = rgba + i * 4u, *q = image.rgba + i * 4u;
        if (p[3] < 128) {
            transparent = 1;
            continue;
        }
        assert(q[3] == 255);
        if (p[3] == 255)
            assert(memcmp(p, q, 3) == 0);
        else
            for (x = 0; x < 3; x++)
                assert(q[x] == (p[x] * p[3] + 255u * (255u - p[3]) + 127u) / 255u);
    }
    for (i = 0; i < (size_t)width * height; i++)
        if (rgba[i * 4u + 3u] < 128)
            assert(image.rgba[i * 4u + 3u] == 0);
    assert(image.has_alpha == transparent);
    xpm_free(&image);
    free(text);
    return cpp;
}

int main(void)
{
    static const char xpm3[] =
        "/* XPM */\n"
        "static const char *const test_xpm[] = {\n"
        "/* width height ncolors cpp x_hot y_hot */\n"
        "\"4 2 4 1 3 1\",\n"
        "/* colors */\n"
        "\"  c None\",\n"
        "\". c #FF0000\",\n"
        "\"X c blue\",\n"
        "\"o c #0f0\",\n"
        "/* pixels */\n"
        "\" .Xo\",\n"
        "\"oX. \"\n"
        "};\n";
    static const uint32_t xpm3_pixels[] = {
        0x00000000, 0xff0000ff, 0x0000ffff, 0x00ff00ff,
        0x00ff00ff, 0x0000ffff, 0xff0000ff, 0x00000000,
    };
    static const char xpm2[] =
        "! XPM2\n"
        "4 2 4 1\r\n"
        "! a comment line\n"
        "  c None\n"
        ". c #FF0000\n"
        "X c blue\n"
        "o c #0f0\n"
        " .Xo\n"
        "oX. ";
    static const char xpm2c[] =
        "/* XPM2 C */\n"
        "static char *x[] = {\n"
        "\"4 2 4 1\",\n\"  c None\",\n\". c #FF0000\",\n\"X c blue\",\n\"o c #0f0\",\n"
        "\" .Xo\",\n\"oX. \"\n};\n";
    static const char xpm1[] =
        "#define test_format 1\n"
        "#define test_width 4\n"
        "#define test_height 2\n"
        "#define test_ncolors 4\n"
        "#define test_chars_per_pixel 1\n"
        "static char *test_colors[] = {\n"
        "\" \", \"None\",\n"
        "\".\", \"#FF0000\",\n"
        "\"X\", \"blue\",\n"
        "\"o\", \"#0f0\"\n"
        "};\n"
        "static char *test_mono[] = { \" \", \"white\", \".\", \"black\" };\n"
        "static char *test_pixels[] = {\n"
        "\" .Xo\",\n"
        "\"oX. \"\n"
        "};\n";
    static const char cpp2[] =
        "/* XPM */\nstatic char *x[] = {\n"
        "\"3 2 3 2 XPMEXT\",\n"
        "\"aa c #FF0000 m black\",\n"
        "\"a  m white c #0000FF\",\n"
        "\" a s foo g #808080\",\n"
        "\"aaa  a\",\n"
        "\" a a aaextra\",\n"
        "\"XPMEXT ext1 data\",\n\"more\",\n\"XPMENDEXT\"\n};\n";
    static const uint32_t cpp2_pixels[] = {
        0xff0000ff, 0x0000ffff, 0x808080ff,
        0x808080ff, 0x808080ff, 0x808080ff,
    };
    struct xpm_image image;
    uint8_t rgba[4], *big;
    uint32_t value;
    char text[512];
    unsigned i;

    /* Colour values. */
    assert(xpm_parse_color((const uint8_t *)"NoNe", 4, rgba) && rgba[3] == 0);
    assert(color("c #F80") == 0xff8800ff);
    assert(color("c #fF8000") == 0xff8000ff);
    assert(color("c #FFF888000") == 0xff8800ff);
    assert(color("c #FFFF80807F7F") == 0xff807fff);
    assert(color("c red") == 0xff0000ff);
    assert(color("c gray") == 0xbebebeff);
    assert(color("c green") == 0x00ff00ff);
    assert(color("c maroon") == 0xb03060ff);
    assert(color("c Light Goldenrod") == 0xeedd82ff);
    assert(color("c LightGoldenrod") == 0xeedd82ff);
    assert(color("c dark   slate  GREY") == 0x2f4f4fff);
    assert(color("c NavajoWhite4") == 0x8b795eff);
    assert(color("c gray0") == 0x000000ff);
    assert(color("c GREY50") == 0x7f7f7fff);
    assert(color("c gray100") == 0xffffffff);
    assert(color("c aliceblue") == 0xf0f8ffff);
    assert(color("c yellowgreen") == 0x9acd32ff);
    assert(one("c gray101", &value) == CODEC_INVALID);
    assert(one("c gray050", &value) == CODEC_INVALID);
    assert(one("c gray5x", &value) == CODEC_INVALID);
    assert(one("c notacolour", &value) == CODEC_INVALID);
    assert(one("c transparent", &value) == CODEC_INVALID);
    assert(one("c #12345", &value) == CODEC_INVALID);
    assert(one("c #1234567890abc", &value) == CODEC_INVALID);
    assert(one("c #GG0000", &value) == CODEC_INVALID);
    assert(one("c #", &value) == CODEC_INVALID);
    assert(one("c rgb:ff/80/00", &value) == CODEC_INVALID);
    assert(one("c aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", &value) == CODEC_INVALID);

    /* Keys: c, then g, g4 and m; s alone is not a colour. */
    assert(color("m white c #0000FF") == 0x0000ffff);
    assert(color("g #808080 m white") == 0x808080ff);
    assert(color("g4 #808080 m white") == 0x808080ff);
    assert(color("m black") == 0x000000ff);
    assert(color("s c c red") == 0xff0000ff);
    assert(color("c bogus m white") == 0xffffffff);
    assert(color("c\tred") == 0xff0000ff);
    assert(color("c red c blue") == 0x0000ffff);
    assert(one("s foo", &value) == CODEC_INVALID);
    assert(one("", &value) == CODEC_INVALID);
    assert(one("c", &value) == CODEC_INVALID);
    assert(one("C red", &value) == CODEC_INVALID);
    assert(one("red", &value) == CODEC_INVALID);

    /* The syntaxes. */
    expect(xpm3, 4, 2, xpm3_pixels);
    expect(xpm2, 4, 2, xpm3_pixels);
    expect(xpm2c, 4, 2, xpm3_pixels);
    expect(xpm1, 4, 2, xpm3_pixels);
    expect(cpp2, 3, 2, cpp2_pixels);
    assert(decode(xpm3, &image) == CODEC_OK);
    assert(image.hot_x == 3 && image.hot_y == 1 && image.has_alpha);
    xpm_free(&image);
    assert(decode(xpm1, &image) == CODEC_OK);
    assert(image.hot_x == -1 && image.has_alpha);
    xpm_free(&image);
    assert(decode(cpp2, &image) == CODEC_OK);
    assert(image.hot_x == -1 && !image.has_alpha);
    xpm_free(&image);
    /* netpbm's spelling of the XPM1 colour count, and a byte order mark. */
    {
        char copy[sizeof xpm1 + 4];
        char *p;
        strcpy(copy, xpm1);
        p = strstr(copy, "_ncolors");
        p[2] = 'C';
        expect(copy, 4, 2, xpm3_pixels);
        strcpy(copy, "\xef\xbb\xbf");
        strcat(copy, xpm3);
        expect(copy, 4, 2, xpm3_pixels);
    }
    /* Hotspots outside the image, or incomplete, are dropped. */
    assert(decode("/* XPM */ {\"1 1 1 1 1 0\" \"a c red\" \"a\"}", &image) == CODEC_OK);
    assert(image.hot_x == -1 && image.hot_y == -1);
    xpm_free(&image);
    assert(decode("/* XPM */ {\"1 1 1 1 0\" \"a c red\" \"a\"}", &image) == CODEC_OK);
    assert(image.hot_x == -1);
    xpm_free(&image);
    assert(decode("/* XPM */ {\"1 1 1 1 junk 0 0\" \"a c red\" \"a\"}", &image) == CODEC_OK);
    assert(image.hot_x == -1);
    xpm_free(&image);
    /* A code defined twice takes the later colour. */
    assert(decode("/* XPM */ {\"1 1 2 1\" \"a c red\" \"a c blue\" \"a\"}", &image) == CODEC_OK);
    assert(pixel(&image, 0, 0) == 0x0000ffff);
    xpm_free(&image);
    /* Three characters per pixel, and the extra length of a long row. */
    assert(decode("/* XPM */ {\"2 1 2 3\" \"abc c red\" \"ab. c blue\" \"ab.abcab\"}",
                  &image) == CODEC_OK);
    assert(pixel(&image, 0, 0) == 0x0000ffff && pixel(&image, 1, 0) == 0xff0000ff);
    xpm_free(&image);
    /* Every pixel transparent remains transparent. */
    assert(decode("/* XPM */ {\"2 1 1 1\" \"a c None\" \"aa\"}", &image) == CODEC_OK);
    assert(pixel(&image, 0, 0) == 0x00000000 && image.has_alpha);
    xpm_free(&image);
    /* A comment opener inside a string is part of the string. */
    assert(decode("/* XPM */ {\"1 1 1 2\" \"/* c red\" \"/*\"}", &image) == CODEC_OK);
    assert(pixel(&image, 0, 0) == 0xff0000ff);
    xpm_free(&image);

    /* Malformed input. */
    fails("", CODEC_INVALID);
    fails("   \n", CODEC_INVALID);
    fails("/* XPN */ {\"1 1 1 1\" \"a c red\" \"a\"}", CODEC_INVALID);
    fails("/* XPM2 Lisp */ {\"1 1 1 1\" \"a c red\" \"a\"}", CODEC_INVALID);
    fails("/**/ {\"1 1 1 1\" \"a c red\" \"a\"}", CODEC_INVALID);
    fails("/* XPM", CODEC_TRUNCATED);
    fails("! XPM3\n1 1 1 1\na c red\na\n", CODEC_INVALID);
    fails("! XPM2 C\n1 1 1 1\na c red\na\n", CODEC_INVALID);
    fails("#include <x.h>\n", CODEC_INVALID);
    fails("#define x_width 1\n#define x_height 1\n#define x_ncolors 1\n"
          "#define x_chars_per_pixel 1\n{\"a\",\"red\"}", CODEC_INVALID);
    fails("/* XPM */", CODEC_TRUNCATED);
    fails("/* XPM */ {\"1 1 1", CODEC_TRUNCATED);
    fails("/* XPM */ {\"1 1 1\"", CODEC_INVALID);
    fails("/* XPM */ {\"1 1 x 1\" \"a c red\" \"a\"}", CODEC_INVALID);
    fails("/* XPM */ {\"0 1 1 1\" \"a c red\" \"a\"}", CODEC_INVALID);
    fails("/* XPM */ {\"1 0 1 1\" \"a c red\" \"a\"}", CODEC_INVALID);
    fails("/* XPM */ {\"1 1 0 1\" \"a c red\" \"a\"}", CODEC_INVALID);
    fails("/* XPM */ {\"1 1 1 0\" \"a c red\" \"a\"}", CODEC_INVALID);
    fails("/* XPM */ {\"1 1 1 33\" \"a c red\" \"a\"}", CODEC_INVALID);
    fails("/* XPM */ {\"65536 1 1 1\" \"a c red\" \"a\"}", CODEC_TOO_LARGE);
    fails("/* XPM */ {\"1 65536 1 1\" \"a c red\" \"a\"}", CODEC_TOO_LARGE);
    fails("/* XPM */ {\"4097 4097 1 1\" \"a c red\" \"a\"}", CODEC_TOO_LARGE);
    fails("/* XPM */ {\"99999999999999999999 1 1 1\" \"a c red\" \"a\"}", CODEC_TOO_LARGE);
    fails("/* XPM */ {\"1 1 99999999999 1\" \"a c red\" \"a\"}", CODEC_TOO_LARGE);
    fails("/* XPM */ {\"1 1 1000000 1\" \"a c red\" \"a\"}", CODEC_TRUNCATED);
    fails("/* XPM */ {\"4096 4096 1 1\" \"a c red\" \"a\"}", CODEC_TRUNCATED);
    fails("/* XPM */ {\"1 1 2 1\" \"a c red\"}", CODEC_TRUNCATED);
    fails("/* XPM */ {\"1 2 1 1\" \"a c red\" \"a\"}", CODEC_TRUNCATED);
    fails("/* XPM */ {\"1 1 1 1\" \"a c red\" \"a", CODEC_TRUNCATED);
    fails("/* XPM */ {\"1 1 1 1\" \"a c red\" /* \"a\"}", CODEC_TRUNCATED);
    fails("/* XPM */ {\"1 1 1 2\" \"a\" \"a \"}", CODEC_INVALID);
    fails("/* XPM */ {\"2 1 1 1\" \"a c red\" \"a\"}", CODEC_INVALID);
    fails("/* XPM */ {\"2 1 1 1\" \"a c red\" \"ab\"}", CODEC_INVALID);
    fails("/* XPM */ {\"1 1 1 1\" \"a c nothing\" \"a\"}", CODEC_INVALID);
    fails("! XPM2\n1 2 1 1\na c red\na\n", CODEC_TRUNCATED);
    fails("#define x_format 1\n#define x_width 1\n#define x_height 1\n"
          "#define x_ncolors 1\n#define x_chars_per_pixel 1\n"
          "static char *x_colors[] = {\"a\"};\n", CODEC_TRUNCATED);
    fails("#define x_format 1\n#define x_width 1\n#define x_height 1\n"
          "#define x_ncolors 1\n#define x_chars_per_pixel 1\n"
          "static char *x_colors[] = {\"a\", \"red\"};\n"
          "static char *x_other[] = {\"a\"};\n", CODEC_TRUNCATED);
    fails("#define x_format 1\n/* never closed", CODEC_TRUNCATED);
    every_prefix_fails(xpm3, "\"oX. \"");
    every_prefix_fails(xpm2c, "\"oX. \"");
    every_prefix_fails(xpm1, "\"oX. \"");
    every_prefix_fails(cpp2, "\" a a aaextra\"");
    /* Natural XPM2 has no closing syntax: a prefix cut inside the last row
       is short, and one cut earlier is missing lines. */
    for (i = 0; i < sizeof xpm2 - 1u; i++) {
        enum codec_result result = xpm_decode((const uint8_t *)xpm2, i, &image);
        assert(result == CODEC_TRUNCATED || result == CODEC_INVALID);
    }
    assert(xpm_decode(NULL, 0, &image) == CODEC_TRUNCATED);
    assert(xpm_decode((const uint8_t *)xpm3, sizeof xpm3 - 1u, NULL) == CODEC_INVALID);

    /* Writing: names. */
    {
        char name[XPM_MAX_NAME + 1];
        xpm_make_name("Work:pics/3d-icon.xpm", name);
        assert(strcmp(name, "_3d_icon") == 0);
        xpm_make_name(NULL, name);
        assert(strcmp(name, "image") == 0);
        xpm_make_name(".xpm", name);
        assert(strcmp(name, "_xpm") == 0);
    }

    /* Writing: opaque, transparent, partly transparent, and all transparent. */
    {
        uint8_t small[4 * 6];
        set(small, 255, 0, 0, 255);
        set(small + 4, 0, 0, 255, 255);
        set(small + 8, 0, 0, 0, 0);
        set(small + 12, 10, 20, 30, 128);
        set(small + 16, 1, 2, 3, 127);
        set(small + 20, 255, 0, 0, 255);
        assert(round_trip(small, 3, 2, 2, 1) == 1);
        assert(round_trip(small, 2, 1, -1, -1) == 1);
        assert(round_trip(small + 8, 1, 1, -1, -1) == 1);
    }
    /* Many colours need two, then three characters per pixel. */
    big = malloc(300u * 300u * 4u);
    assert(big != NULL);
    for (i = 0; i < 300u * 300u; i++)
        set(big + i * 4u, (uint8_t)i, (uint8_t)(i >> 8), (uint8_t)(i >> 16), 255);
    assert(round_trip(big, 90, 1, -1, -1) == 1);
    assert(round_trip(big, 92, 1, -1, -1) == 2);
    assert(round_trip(big, 300, 300, 10, 20) == 3);
    free(big);

    /* Writer edge cases. */
    {
        struct xpm_encoder encoder;
        uint8_t red[4] = { 255, 0, 0, 255 }, blue[4] = { 0, 0, 255, 255 };
        xpm_encoder_init(&encoder);
        assert(xpm_make_header(&encoder, "x", 1, 1, -1, -1, text, sizeof text) == 0);
        assert(xpm_encode_row(&encoder, red, 1, text, sizeof text) == SIZE_MAX);
        assert(xpm_encoder_add_row(&encoder, red, 1) == CODEC_OK);
        xpm_encoder_finish(&encoder, 1);
        assert(xpm_make_header(&encoder, "x", 0, 1, -1, -1, text, sizeof text) == 0);
        assert(xpm_make_header(&encoder, "x", 1, 1, -1, -1, text, 10) == 0);
        assert(xpm_color_line(&encoder, 1, text, sizeof text) == 0);
        assert(xpm_color_line(&encoder, 0, text, 5) == 0);
        assert(xpm_encode_row(&encoder, blue, 1, text, sizeof text) == SIZE_MAX);
        assert(xpm_encode_row(&encoder, red, 1, text, 3) == SIZE_MAX);
        xpm_encoder_free(&encoder);
    }
    puts("xpm: ok");
    return 0;
}
