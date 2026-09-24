#include "../formats/xvthumb/decode.h"
#include "../formats/xvthumb/encode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t data[1024];

/* Header text followed by count pixel bytes 0, 1, 2, ... */
static size_t file(const char *header, size_t count)
{
    size_t n = strlen(header), i;
    memcpy(data, header, n);
    for (i = 0; i < count; i++)
        data[n + i] = (uint8_t)i;
    return n + count;
}

static enum codec_result decode(const char *header, size_t count)
{
    struct xvthumb_image image;
    enum codec_result result = xvthumb_decode(data, file(header, count), &image);
    if (result == CODEC_OK)
        xvthumb_free(&image);
    else
        assert(image.rgba == NULL && image.width == 0 && image.height == 0);
    return result;
}

static void expect_2x2(const char *header)
{
    struct xvthumb_image image;
    unsigned i;

    assert(xvthumb_decode(data, file(header, 4), &image) == CODEC_OK);
    assert(image.width == 2 && image.height == 2);
    for (i = 0; i < 4; i++) {
        assert(image.rgba[i * 4u] == 0 && image.rgba[i * 4u + 1u] == 0);
        assert(image.rgba[i * 4u + 2u] == i * 85u);
        assert(image.rgba[i * 4u + 3u] == 255);
    }
    xvthumb_free(&image);
}

int main(void)
{
    static const char xv[] =
        "P7 332\n#XVVERSION:Version 3.10a  Rev: 12/29/94\n"
        "#IMGINFO:512x384 Color JPEG\n#END_OF_COMMENTS\n16 16 255\n";
    struct xvthumb_image image;
    uint8_t rgba[256 * 4], row[256];
    char header[XVTHUMB_HEADER_MAX];
    size_t length, n;
    unsigned i;

    /* Every 3:3:2 value expands as v * 255 / max rounded down, like Pillow and netpbm. */
    length = file(xv, 256);
    assert(xvthumb_decode(data, length, &image) == CODEC_OK);
    assert(image.width == 16 && image.height == 16);
    for (i = 0; i < 256; i++) {
        assert(image.rgba[i * 4u] == (i >> 5) * 255u / 7u);
        assert(image.rgba[i * 4u + 1u] == ((i >> 2) & 7u) * 255u / 7u);
        assert(image.rgba[i * 4u + 2u] == (i & 3u) * 255u / 3u);
        assert(image.rgba[i * 4u + 3u] == 255);
    }
    assert(image.rgba[4 * 0x24 + 1] == 36 && image.rgba[4 * 0xe0] == 255);
    xvthumb_free(&image);

    /* Truncation anywhere, in the header or the pixels. */
    for (n = 0; n < length; n++)
        assert(xvthumb_decode(data, n, &image) == CODEC_TRUNCATED && image.rgba == NULL);
    /* Trailing bytes are ignored. */
    assert(xvthumb_decode(data, length + 5, &image) == CODEC_OK);
    xvthumb_free(&image);

    /* Header layouts that netpbm or Pillow accept. */
    expect_2x2("P7 332\n2 2 255\n");                             /* no comments */
    expect_2x2("P7 332\n#END_OF_COMMENTS\n#late\n2 2 255\n");    /* comment after the end marker */
    expect_2x2("P7 332\r\n#END_OF_COMMENTS\r\n2 2 255\r\n");     /* CRLF */
    expect_2x2("P7 332 junk\n2 2 255\n");                        /* rest of the magic line */
    expect_2x2("P7 332#x\n2 2 255\n");
    expect_2x2("P7 332\n  2 \t 2\t255  \n");                     /* spacing */
    expect_2x2("P7 332\n2 2\n");                                 /* no maxval, as Pillow reads */
    expect_2x2("P7 332\n2 2 255 9\n");                           /* text after maxval */
    expect_2x2("P7 332\n2 2 255x\n");
    expect_2x2("P7 332\n#\n##\n2 2 255\n");
    expect_2x2("P7 332\n2 2 00255\n");
    expect_2x2("P7 332\n+2 +2 +255\n");                        /* a sign, as both allow */

    /* A '#' after the size line is pixel data. */
    assert(xvthumb_decode(data, file("P7 332\n1 2 255\n#x", 0), &image) == CODEC_OK);
    assert(image.rgba[0] == 36 && image.rgba[2] == 255);
    xvthumb_free(&image);

    /* Malformed headers. */
    assert(decode("P7 331\n2 2 255\n", 4) == CODEC_INVALID);
    assert(decode("P6 332\n2 2 255\n", 4) == CODEC_INVALID);
    assert(decode("p7 332\n2 2 255\n", 4) == CODEC_INVALID);
    assert(decode("P7\n332\n2 2 255\n", 4) == CODEC_INVALID);
    assert(decode("P7 3", 0) == CODEC_TRUNCATED);
    assert(decode("P7 4", 0) == CODEC_INVALID);
    assert(decode("P7 332\n2 2 15\n", 4) == CODEC_INVALID);     /* maxval must be 255 */
    assert(decode("P7 332\n2 2 0\n", 4) == CODEC_INVALID);
    assert(decode("P7 332\n2 2 65535\n", 4) == CODEC_INVALID);
    assert(decode("P7 332\n2 2 99999999999999999999\n", 4) == CODEC_INVALID);
    assert(decode("P7 332\n2 2 x\n", 4) == CODEC_INVALID);
    assert(decode("P7 332\n2\n2\n255\n", 4) == CODEC_INVALID);   /* size split over lines */
    assert(decode("P7 332\n2x 2 255\n", 4) == CODEC_INVALID);
    assert(decode("P7 332\n2 2x 255\n", 4) == CODEC_INVALID);
    assert(decode("P7 332\n-2 2 255\n", 4) == CODEC_INVALID);
    assert(decode("P7 332\n+ 2 255\n", 4) == CODEC_INVALID);
    assert(decode("P7 332\n2 2 ++255\n", 4) == CODEC_INVALID);
    assert(decode("P7 332\n\n2 2 255\n", 4) == CODEC_INVALID);  /* blank line */
    assert(decode("P7 332\n #x\n2 2 255\n", 4) == CODEC_INVALID);
    assert(decode("P7 332\n2 \n", 4) == CODEC_INVALID);
    assert(decode("P7 332\n\n", 4) == CODEC_INVALID);
    assert(decode("P7 332\n0 2 255\n", 4) == CODEC_INVALID);
    assert(decode("P7 332\n2 0 255\n", 4) == CODEC_INVALID);
    assert(decode("P7 332\n2 2 255", 4) == CODEC_TRUNCATED);    /* no newline before pixels */
    assert(decode("P7 332\n#END_OF_COMMENTS", 0) == CODEC_TRUNCATED);
    assert(decode("P7 332\n2 2 255\n", 3) == CODEC_TRUNCATED);

    /* Limits: each side at most 65535, and at most 16M pixels. */
    assert(decode("P7 332\n65536 1 255\n", 4) == CODEC_TOO_LARGE);
    assert(decode("P7 332\n1 65536 255\n", 4) == CODEC_TOO_LARGE);
    assert(decode("P7 332\n99999999999999999999999 1 255\n", 4) == CODEC_TOO_LARGE);
    assert(decode("P7 332\n4097 4096 255\n", 4) == CODEC_TOO_LARGE);
    assert(decode("P7 332\n4096 4096 255\n", 4) == CODEC_TRUNCATED);
    assert(decode("P7 332\n65535 1 255\n", 4) == CODEC_TRUNCATED);

    assert(xvthumb_decode(NULL, 10, &image) == CODEC_TRUNCATED);
    assert(xvthumb_decode(data, 10, NULL) == CODEC_INVALID);

    /* The header netpbm and XV expect. */
    length = xvthumb_make_header(80, 60, header);
    assert(length == strlen("P7 332\n#END_OF_COMMENTS\n80 60 255\n"));
    assert(memcmp(header, "P7 332\n#END_OF_COMMENTS\n80 60 255\n", length) == 0);
    length = xvthumb_make_header(65535, 65535, header);
    assert(memcmp(header + length - 16, "65535 65535 255\n", 16) == 0);
    assert(xvthumb_make_header(0, 1, header) == 0);
    assert(xvthumb_make_header(1, 0, header) == 0);
    assert(xvthumb_make_header(65536, 1, header) == 0);
    assert(xvthumb_make_header(1, 65536, header) == 0);
    assert(xvthumb_make_header(1, 1, NULL) == 0);

    /* Each channel goes to its nearest level: pamtoxvmini's cut points. */
    for (i = 0; i < 256; i++) {
        rgba[i * 4u] = rgba[i * 4u + 1u] = rgba[i * 4u + 2u] = (uint8_t)i;
        rgba[i * 4u + 3u] = 255;
    }
    xvthumb_encode_row(rgba, 256, row);
    for (i = 0; i < 256; i++) {
        unsigned r3 = (i >= 19) + (i >= 55) + (i >= 91) + (i >= 128) +
                      (i >= 164) + (i >= 201) + (i >= 237);
        unsigned b2 = (i >= 43) + (i >= 128) + (i >= 213);
        assert(row[i] == (r3 << 5 | r3 << 2 | b2));
    }

    /* Every 3:3:2 value survives a decode and encode. */
    length = file(xv, 256);
    assert(xvthumb_decode(data, length, &image) == CODEC_OK);
    xvthumb_encode_row(image.rgba, 256, row);
    for (i = 0; i < 256; i++)
        assert(row[i] == i);
    xvthumb_free(&image);

    /* Alpha is composited over white. */
    memset(rgba, 0, 16);
    rgba[3] = 255;                                          /* opaque black */
    rgba[4 + 3] = 128;                                      /* half black: 127 */
    rgba[8] = 255;                                          /* transparent red: white */
    xvthumb_encode_row(rgba, 4, row);
    assert(row[0] == 0x00 && row[1] == (3u << 5 | 3u << 2 | 1u));
    assert(row[2] == 0xff && row[3] == 0xff);

    puts("xvthumb ok");
    return 0;
}
