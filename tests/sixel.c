#include "../formats/sixel/decode.h"
#include "../formats/sixel/encode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RED 0xff0000ffu
#define GREEN 0x00ff00ffu
#define BLUE 0x0000ffffu
#define BLACK 0x000000ffu
#define WHITE 0xffffffffu
#define CLEAR 0x00000000u

static enum codec_result decode_index(const char *text, unsigned index,
                                      struct sixel_image *image)
{
    return sixel_decode((const uint8_t *)text, strlen(text), index, image);
}

static enum codec_result decode(const char *text, struct sixel_image *image)
{
    return decode_index(text, 0, image);
}

static uint32_t pixel(const struct sixel_image *image, unsigned x, unsigned y)
{
    const uint8_t *p = image->rgba + ((size_t)y * image->width + x) * 4u;
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* Decode text and compare each pixel with RRGGBBAA values in row order. */
static void expect(const char *text, unsigned width, unsigned height,
                   const uint32_t *pixels)
{
    struct sixel_image image;
    unsigned x, y;
    assert(decode(text, &image) == CODEC_OK);
    if (image.width != width || image.height != height)
        fprintf(stderr, "%s: %ux%u\n", text, image.width, image.height);
    assert(image.width == width && image.height == height);
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++) {
            if (pixel(&image, x, y) != pixels[y * width + x])
                fprintf(stderr, "pixel %u,%u is %08lx\n", x, y,
                        (unsigned long)pixel(&image, x, y));
            assert(pixel(&image, x, y) == pixels[y * width + x]);
        }
    sixel_free(&image);
}

/* The colour of the first pixel after the given sixel data. */
static uint32_t first(const char *body)
{
    char text[512];
    struct sixel_image image;
    uint32_t value;
    snprintf(text, sizeof text, "\033Pq%s\033\\", body);
    assert(decode(text, &image) == CODEC_OK);
    value = pixel(&image, 0, 0);
    sixel_free(&image);
    return value;
}

static void expect_size(const char *text, unsigned width, unsigned height)
{
    struct sixel_image image;
    assert(decode(text, &image) == CODEC_OK);
    if (image.width != width || image.height != height)
        fprintf(stderr, "%s: %ux%u\n", text, image.width, image.height);
    assert(image.width == width && image.height == height);
    sixel_free(&image);
}

static void expect_result(const char *text, enum codec_result result)
{
    struct sixel_image image;
    assert(decode(text, &image) == result);
    assert(image.rgba == NULL);
}

static void test_drawing(void)
{
    static const uint32_t one[] = { RED, BLACK, BLACK, BLACK, BLACK, BLACK };
    static const uint32_t column[] = { GREEN, GREEN, GREEN, GREEN, GREEN, GREEN, GREEN };
    static const uint32_t cr[] = { RED, RED, BLUE, BLACK };
    static const uint32_t bits[] = { RED, BLACK, BLACK, RED, RED, BLACK,
                                     BLACK, RED, RED, BLACK, BLACK, RED };

    expect("\033Pq\"1;1;3;2#1;2;100;0;0@\033\\", 3, 2, one);
    /* - moves down six rows, to the left edge. */
    expect("\033Pq#1;2;0;100;0~-@\033\\", 1, 7, column);
    /* $ returns to the left edge of the same band. */
    expect("\033Pq#1;2;100;0;0@@$#2;2;0;0;100A\033\\", 2, 2, cr);
    /* Bit 0 is the top row: T is rows 0, 2 and 4; i is rows 1, 3 and 5. */
    expect("\033Pq\"1;1;2;6#1;2;100;0;0Ti\033\\", 2, 6, bits);
    /* ? is blank but still moves right; trailing blanks add no width. */
    expect_size("\033Pq#1;2;0;0;100?@\033\\", 2, 1);
    expect_size("\033Pq#1;2;0;0;100@???\033\\", 1, 1);
    /* An empty final band adds no height. */
    expect_size("\033Pq#1;2;0;100;0!4~$-!2?-\033\\", 4, 6);
    /* Nothing drawn and no raster attributes: one pixel of background. */
    expect_size("\033Pq\033\\", 1, 1);
    /* DEL is not a sixel. */
    expect_size("\033Pq@\177@\033\\", 2, 1);
}

static void test_repeat(void)
{
    expect_size("\033Pq!5~\033\\", 5, 6);
    expect_size("\033Pq!0@\033\\", 1, 1);
    expect_size("\033Pq!@\033\\", 1, 1);
    /* The count applies to the next sixel only. */
    expect_size("\033Pq!3@@\033\\", 4, 1);
    /* $ and - clear a pending count. */
    expect_size("\033Pq!9$@\033\\", 1, 1);
    /* Writers wrap lines anywhere, even inside a number. */
    expect_size("\033Pq!\n3@\033\\", 3, 1);
    expect_size("\033Pq!1\r\n2@\033\\", 12, 1);
}

static void test_colors(void)
{
    /* RGB percentages round to the nearest 8-bit value; over 100 clamps. */
    assert(first("#1;2;50;1;100@") == 0x8003ffffu);
    assert(first("#1;2;200;0;0@") == RED);
    /* HLS, with DEC's hues: 0 blue, 120 red, 240 green. */
    assert(first("#1;1;120;50;100@") == RED);
    assert(first("#1;1;0;50;100@") == BLUE);
    assert(first("#1;1;240;50;100@") == GREEN);
    assert(first("#1;1;60;50;100@") == 0xff00ffffu);
    assert(first("#1;1;180;50;100@") == 0xffff00ffu);
    assert(first("#1;1;300;50;100@") == 0x00ffffffu);
    assert(first("#1;1;0;25;50@") == 0x202060ffu);
    assert(first("#1;1;90;75;40@") == 0xd9a6bfffu);
    assert(first("#1;1;0;5;0@") == 0x0d0d0dffu);
    assert(first("#1;1;0;100;100@") == WHITE);
    assert(first("#1;1;400;50;100@") == BLUE);
    /* Other colour systems leave the register alone. */
    assert(first("#1;3;0;0;0@") == 0x3333ccffu);
    /* A definition needs all five parameters; empty ones are 0. */
    assert(first("#1;2;100;0;@") == 0x3333ccffu);
    assert(first("#1;2;;100;0@") == GREEN);
    assert(first("#1; 2; 0; 0; 100@") == BLUE);
    /* A register's last definition colours pixels already drawn. */
    assert(first("#1;2;100;0;0@#1;2;0;0;100@") == BLUE);
}

static void test_default_palette(void)
{
    /* The VT340's colours, then xterm's cube and grey ramp, then white. */
    assert(first("#0@") == BLACK);
    assert(first("#1@") == 0x3333ccffu);
    assert(first("#2@") == 0xcc2121ffu);
    assert(first("#7@") == 0x878787ffu);
    assert(first("#8@") == 0x424242ffu);
    assert(first("#15@") == 0xccccccffu);
    assert(first("#16@") == BLACK);
    assert(first("#17@") == 0x000033ffu);
    assert(first("#100@") == 0x666600ffu);
    assert(first("#231@") == WHITE);
    assert(first("#232@") == BLACK);
    assert(first("#255@") == 0xfdfdfdffu);
    assert(first("#256@") == WHITE);
    assert(first("#1023@") == WHITE);
    /* Registers past the last share it. */
    assert(first("#1023;2;0;0;100#5000@") == BLUE);
    assert(first("#300;2;100;0;0#44;2;0;0;100#300@") == RED);
    /* Drawing without selecting uses register 0. */
    assert(first("#0;2;0;100;0@") == GREEN);
}

static void test_background(void)
{
    static const uint32_t green[] = { RED, GREEN, GREEN, GREEN };
    static const uint32_t clear[] = { RED, CLEAR, CLEAR, CLEAR };
    static const uint32_t zero[] = { BLACK, CLEAR };

    /* P2 0 or 2: undrawn pixels take register 0, as it is at the end. */
    expect("\033Pq\"1;1;2;2#1;2;100;0;0@#0;2;0;100;0\033\\", 2, 2, green);
    expect("\033P0;2q\"1;1;2;2#1;2;100;0;0@#0;2;0;100;0\033\\", 2, 2, green);
    /* P2 1: they stay transparent. */
    expect("\033P0;1;0q\"1;1;2;2#1;2;100;0;0@#0;2;0;100;0\033\\", 2, 2, clear);
    expect("\033P;1q\"1;1;2;2#1;2;100;0;0@\033\\", 2, 2, clear);
    expect("\033P0;1q\"1;1;2;1#0@\033\\", 2, 1, zero);
    expect("\033P0;1q\033\\", 1, 1, zero + 1);
}

static void test_raster(void)
{
    /* The raster attributes give the least size; drawing can exceed it. */
    expect_size("\033Pq\"1;1;5;3#1@\033\\", 5, 3);
    expect_size("\033Pq\"1;1;2;1!4@\033\\", 4, 1);
    expect_size("\033Pq\"1;1;2;1~\033\\", 2, 6);
    expect_size("\033Pq\"0;0;0;0@\033\\", 1, 1);
    expect_size("\033Pq\"1;1;7\033\\", 7, 1);
    /* They may come after data. */
    expect_size("\033Pq@\"1;1;3;9\033\\", 3, 9);
    /* The aspect ratio (P1, Pan and Pad) is ignored. */
    expect_size("\033P0q~\033\\", 1, 6);
    expect_size("\033P2;0;0q\"2;1;1;1~\033\\", 1, 6);
}

static void test_framing(void)
{
    static const uint32_t blue2[] = { BLUE, BLUE };

    expect("\2200;0;8q\"1;1;2;1#1;2;0;0;100@@\234", 2, 1, blue2);
    expect("\033P0;0;8q\"1;1;2;1#1;2;0;0;100@@\033\\", 2, 1, blue2);
    /* Text and escape sequences before the image are not drawn. */
    expect("\033[?80h\033Pq#1;2;0;0;100@@\033\\\033[1H\n", 2, 1, blue2);
    expect("\033[2J\033[1H\033Pq#1;2;0;0;100@@\033\\", 2, 1, blue2);
    expect("Some text!\n\033Pq#1;2;0;0;100@@\033\\", 2, 1, blue2);
    /* Other device control strings are skipped. */
    expect("\033P$qm\033\\\033P1$r0m\033\\\033Pq#1;2;0;0;100@@\033\\", 2, 1, blue2);
    /* Any escape ends the image. */
    expect("\033Pq#1;2;0;0;100@@\033[0m@@", 2, 1, blue2);
    /* Line breaks and spaces in the data are ignored. */
    expect("\033Pq\n#1;2;0;0;100\r\n@ @\n\033\\", 2, 1, blue2);
    expect("\033Pq#1;2;0;0;100@@\033\\trailing text", 2, 1, blue2);
}

static void test_multiple(void)
{
    static const char two[] =
        "\033Pq#1;2;100;0;0@\033\\\033Pq\"1;1;2;2#1;2;0;0;100@@\033\\";
    static const char mixed[] =
        "\220q#1;2;100;0;0@\234\220q#1;2;0;100;0@\234\033[H\033Pq@\033\\";
    struct sixel_image image;

    assert(sixel_count((const uint8_t *)two, strlen(two)) == 2);
    assert(decode_index(two, 0, &image) == CODEC_OK);
    assert(image.width == 1 && image.height == 1 && pixel(&image, 0, 0) == RED);
    sixel_free(&image);
    assert(decode_index(two, 1, &image) == CODEC_OK);
    assert(image.width == 2 && image.height == 2);
    assert(pixel(&image, 1, 0) == BLUE && pixel(&image, 1, 1) == BLACK);
    sixel_free(&image);
    assert(decode_index(two, 2, &image) == CODEC_INVALID);
    assert(decode_index(two, 0xffffffffu, &image) == CODEC_INVALID);

    assert(sixel_count((const uint8_t *)mixed, strlen(mixed)) == 3);
    assert(decode_index(mixed, 1, &image) == CODEC_OK);
    assert(pixel(&image, 0, 0) == GREEN);
    sixel_free(&image);
    /* Registers start afresh in each image. */
    assert(decode_index(mixed, 2, &image) == CODEC_OK);
    assert(pixel(&image, 0, 0) == BLACK);
    sixel_free(&image);

    /* An unterminated last image is not counted, and is truncated. */
    assert(sixel_count((const uint8_t *)two, strlen(two) - 2) == 1);
    assert(sixel_decode((const uint8_t *)two, strlen(two) - 2, 1, &image) == CODEC_TRUNCATED);
    assert(sixel_count((const uint8_t *)"", 0) == 0);
    assert(sixel_count((const uint8_t *)"\033P", 2) == 0);
}

static void test_malformed(void)
{
    static const char full[] = "\033P0;1;0q\"1;1;2;7#1;2;100;0;0!2~-#2;1;0;50;100@\033\\";
    struct sixel_image image;
    size_t length;

    expect_result("", CODEC_INVALID);
    expect_result("hello", CODEC_INVALID);
    expect_result("q#1;2;0;0;100@@\033\\", CODEC_INVALID);
    expect_result("\033Pp\033\\", CODEC_INVALID);
    expect_result("\033P1$q\033\\", CODEC_INVALID);
    /* Cut at every point, including inside the terminator. */
    for (length = 1; length < sizeof full - 1; length++) {
        assert(sixel_decode((const uint8_t *)full, length, 0, &image) == CODEC_TRUNCATED);
        assert(image.rgba == NULL);
    }
    assert(sixel_decode((const uint8_t *)full, sizeof full - 1, 0, &image) == CODEC_OK);
    sixel_free(&image);
    expect_result("\220q@", CODEC_TRUNCATED);
    expect_result("text\033", CODEC_TRUNCATED);
}

static void test_limits(void)
{
    char *text;
    size_t i;
    struct sixel_image image;

    expect_size("\033Pq\"1;1;65535;1\033\\", 65535, 1);
    expect_result("\033Pq\"1;1;65536;1\033\\", CODEC_TOO_LARGE);
    expect_result("\033Pq\"1;1;1;65536\033\\", CODEC_TOO_LARGE);
    expect_result("\033Pq\"1;1;4097;4096\033\\", CODEC_TOO_LARGE);
    expect_result("\033Pq\"1;1;99999999999999999999;1\033\\", CODEC_TOO_LARGE);
    expect_size("\033Pq!65535@\033\\", 65535, 1);
    expect_result("\033Pq!65535@@\033\\", CODEC_TOO_LARGE);
    expect_result("\033Pq!99999999999999999999@\033\\", CODEC_TOO_LARGE);
    /* Blank sixels may run past the edge; only drawing counts. */
    expect_size("\033Pq@!99999999?!99999999?$@\033\\", 1, 1);
    /* 10923 bands reach row 65538. */
    text = malloc(10923 + 16);
    assert(text != NULL);
    strcpy(text, "\033Pq");
    for (i = 0; i < 10923; i++)
        text[3 + i] = '-';
    strcpy(text + 3 + i, "@\033\\");
    expect_result(text, CODEC_TOO_LARGE);
    /* Without the drawing, the bands are free. */
    strcpy(text + 3 + i, "\033\\");
    expect_size(text, 1, 1);
    /* 10922 bands and a sixel reach row 65532..65537; bit 0 is row 65532. */
    strcpy(text + 3 + 10922, "@\033\\");
    assert(decode(text, &image) == CODEC_OK);
    assert(image.width == 1 && image.height == 65533);
    sixel_free(&image);
    free(text);
    /* Huge colour numbers clamp. */
    assert(first("#99999999999999;2;99999999999;0;0@") == RED);
}

/* Write an image with the encoder into a malloc'd buffer. */
static char *encode(const uint8_t *rgba, unsigned width, unsigned height,
                    size_t *length, struct sixel_encoder *e)
{
    size_t capacity = SIXEL_HEADER_MAX + (size_t)height * 300u * 64u + 64u, n, used;
    char *out = malloc(capacity), line[70000];
    unsigned y;

    assert(out != NULL);
    assert(sixel_encoder_init(e, width, height) == CODEC_OK);
    for (y = 0; y < height; y++)
        assert(sixel_encoder_scan(e, rgba + (size_t)y * width * 4u) == CODEC_OK);
    assert(sixel_encoder_plan(e) == CODEC_OK);
    assert(sixel_encoder_header(e, out, SIXEL_HEADER_MAX - 1) == 0);
    used = sixel_encoder_header(e, out, capacity);
    assert(used > 0 && used <= SIXEL_HEADER_MAX);
    for (y = 0; y < height; y++) {
        if (!sixel_encoder_add_row(e, rgba + (size_t)y * width * 4u))
            continue;
        while ((n = sixel_encoder_next(e, line, sixel_line_capacity(width))) > 0) {
            assert(n <= sixel_line_capacity(width));
            assert(used + n < capacity);
            memcpy(out + used, line, n);
            used += n;
        }
    }
    used += sixel_encoder_end(out + used, capacity - used);
    *length = used;
    return out;
}

/* Every repeat count is at most its offset in the file. */
static void expect_short_repeats(const char *file, size_t length)
{
    size_t i;
    unsigned long n;

    for (i = 0; i < length; i++) {
        if (file[i] != '!')
            continue;
        for (n = 0; i + 1 < length && file[i + 1] >= '0' && file[i + 1] <= '9'; i++)
            n = n * 10u + (unsigned long)(file[i + 1] - '0');
        assert(n >= 4 && n <= i);
    }
}

static uint8_t level(uint8_t c)
{
    unsigned p = (c * 100u + 127u) / 255u;
    return (uint8_t)((p * 255u + 50u) / 100u);
}

/* Encode and decode; every pixel must come back as the writer promises. */
static void round_trip(const uint8_t *rgba, unsigned width, unsigned height,
                       int transparent, unsigned colors)
{
    struct sixel_encoder e;
    struct sixel_image image;
    size_t length, i;
    char *file = encode(rgba, width, height, &length, &e);

    assert(e.quantised == 0 && e.transparent == transparent);
    /* colors 0: not checked. */
    assert(colors == 0 || e.colors == colors);
    sixel_encoder_free(&e);
    assert(memcmp(file, transparent ? "\033P0;1;0q\"1;1;" : "\033P0;0;0q\"1;1;", 13) == 0);
    assert(sixel_count((const uint8_t *)file, length) == 1);
    expect_short_repeats(file, length);
    assert(sixel_decode((const uint8_t *)file, length, 0, &image) == CODEC_OK);
    assert(image.width == width && image.height == height);
    for (i = 0; i < (size_t)width * height; i++) {
        const uint8_t *s = rgba + i * 4u, *d = image.rgba + i * 4u;
        unsigned k, a = s[3];
        if (a < 128) {
            assert(d[0] == 0 && d[1] == 0 && d[2] == 0 && d[3] == 0);
            continue;
        }
        for (k = 0; k < 3; k++)
            assert(d[k] == level((uint8_t)((s[k] * a + 255u * (255u - a) + 127u) / 255u)));
        assert(d[3] == 255);
    }
    sixel_free(&image);
    free(file);
}

static void test_encoder(void)
{
    uint8_t rgba[300 * 13 * 4];
    struct sixel_encoder e;
    struct sixel_image image;
    unsigned x, y, n;
    size_t length;
    char *file;

    assert(sixel_encoder_init(&e, 0, 1) == CODEC_INVALID);
    assert(sixel_encoder_init(&e, 1, 65536) == CODEC_INVALID);

    /* A few colours, over heights that do and don't fill the last band. */
    for (n = 1; n <= 13; n++) {
        for (y = 0; y < n; y++)
            for (x = 0; x < 7; x++) {
                uint8_t *p = rgba + (y * 7u + x) * 4u;
                p[0] = (uint8_t)(x * 40u);
                p[1] = (uint8_t)(y * 20u);
                p[2] = 7;
                p[3] = 255;
            }
        round_trip(rgba, 7, n, 0, 7u * (n < 13 ? n : 13u));
    }
    /* One wide colour is written as repeats, none longer than the file
       so far, since ImageMagick stops at those. */
    memset(rgba, 0x80, 300 * 4);
    file = encode(rgba, 300, 1, &length, &e);
    sixel_encoder_free(&e);
    assert(strstr(file, "#0!") != NULL);
    assert(length < 80);
    expect_short_repeats(file, length);
    free(file);
    round_trip(rgba, 300, 1, 0, 1);
    round_trip(rgba, 1, 1, 0, 1);

    /* Alpha: clear below 128, the rest over white, register 0 white. */
    for (x = 0; x < 256; x++) {
        uint8_t *p = rgba + x * 4u;
        p[0] = 200;
        p[1] = (uint8_t)x;
        p[2] = 0;
        p[3] = (uint8_t)x;
    }
    round_trip(rgba, 256, 1, 1, 0);
    file = encode(rgba, 256, 1, &length, &e);
    sixel_encoder_free(&e);
    assert(strstr(file, "#0;2;100;100;100#1;") != NULL);
    free(file);
    /* Nothing drawn at all. */
    memset(rgba, 0, 300 * 13 * 4);
    round_trip(rgba, 300, 13, 1, 1);

    /* Exactly 256 colours fit; with a clear pixel they no longer do. */
    for (x = 0; x < 256; x++) {
        uint8_t *p = rgba + x * 4u;
        p[0] = (uint8_t)(x / 16u * 17u);
        p[1] = (uint8_t)(x % 16u * 17u);
        p[2] = 0;
        p[3] = 255;
    }
    round_trip(rgba, 256, 1, 0, 256);
    memset(rgba + 256 * 4, 0, 4);
    file = encode(rgba, 257, 1, &length, &e);
    assert(e.quantised && e.transparent && e.colors == 256);
    sixel_encoder_free(&e);
    assert(sixel_decode((const uint8_t *)file, length, 0, &image) == CODEC_OK);
    assert(pixel(&image, 256, 0) == CLEAR && image.rgba[3] == 255);
    sixel_free(&image);
    free(file);
}

/* More colours than registers: median cut keeps the error small. */
static void test_quantiser(void)
{
    enum { W = 96, H = 64 };
    static uint8_t rgba[W * H * 4];
    struct sixel_encoder e;
    struct sixel_image image;
    unsigned x, y, k;
    unsigned long total = 0;
    int err, worst = 0;
    size_t length;
    char *file;

    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            uint8_t *p = rgba + (y * W + x) * 4u;
            p[0] = (uint8_t)(x * 255u / (W - 1));
            p[1] = (uint8_t)(y * 255u / (H - 1));
            p[2] = (uint8_t)((x + y) * 255u / (W + H - 2));
            p[3] = 255;
        }
    file = encode(rgba, W, H, &length, &e);
    assert(e.quantised && !e.transparent && e.colors == 256);
    sixel_encoder_free(&e);
    assert(sixel_decode((const uint8_t *)file, length, 0, &image) == CODEC_OK);
    assert(image.width == W && image.height == H);
    for (x = 0; x < W * H; x++)
        for (k = 0; k < 4; k++) {
            err = abs((int)image.rgba[x * 4 + k] - (int)rgba[x * 4 + k]);
            total += (unsigned long)err;
            if (err > worst)
                worst = err;
        }
    assert(worst <= 32);
    assert(total / (W * H * 3) <= 6);
    sixel_free(&image);
    free(file);
}

int main(void)
{
    test_drawing();
    test_repeat();
    test_colors();
    test_default_palette();
    test_background();
    test_raster();
    test_framing();
    test_multiple();
    test_malformed();
    test_limits();
    test_encoder();
    test_quantiser();
    puts("sixel tests passed");
    return 0;
}
