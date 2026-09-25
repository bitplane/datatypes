#include "../formats/mtv/decode.h"
#include "../formats/mtv/encode.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t data[32768];
static size_t size;

static void add(const void *bytes, size_t n)
{
    assert(size + n <= sizeof data);
    memcpy(data + size, bytes, n);
    size += n;
}

static void add_text(const char *text)
{
    add(text, strlen(text));
}

/* Pixel (x, y) of the test pattern with a seed, channel c. */
static uint8_t value(unsigned x, unsigned y, unsigned c, unsigned seed)
{
    return (uint8_t)(x * 37u + y * 11u + c * 89u + seed);
}

static void add_pixels(unsigned width, unsigned height, unsigned seed)
{
    unsigned x, y, c;
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++)
            for (c = 0; c < 3; c++) {
                uint8_t v = value(x, y, c, seed);
                add(&v, 1);
            }
}

static void add_qrt(unsigned width, unsigned height, unsigned seed)
{
    uint8_t header[4] = { (uint8_t)width, (uint8_t)(width >> 8),
                          (uint8_t)height, (uint8_t)(height >> 8) };
    unsigned x, y, c;

    add(header, 4);
    for (y = 0; y < height; y++) {
        /* Row numbers are ignored; write nonsense to prove it. */
        uint8_t row[2] = { (uint8_t)(0xa5 ^ y), 0x5a };
        add(row, 2);
        for (c = 0; c < 3; c++)
            for (x = 0; x < width; x++) {
                uint8_t v = value(x, y, c, seed);
                add(&v, 1);
            }
    }
}

static void check(const struct mtv_image *image, unsigned width, unsigned height,
                  unsigned seed)
{
    unsigned x, y, c;
    assert(image->width == width && image->height == height);
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++) {
            const uint8_t *p = image->rgba + ((size_t)y * width + x) * 4u;
            for (c = 0; c < 3; c++)
                assert(p[c] == value(x, y, c, seed));
            assert(p[3] == 255);
        }
}

static void expect(const char *header, enum codec_result want)
{
    struct mtv_image image;
    size = 0;
    add_text(header);
    add_pixels(3, 2, 0);
    assert(mtv_decode(data, size, 0, &image) == want);
    if (want == CODEC_OK) {
        check(&image, 3, 2, 0);
        mtv_free(&image);
    } else {
        assert(image.rgba == NULL && image.width == 0);
    }
}

int main(void)
{
    struct mtv_image image;
    uint8_t source[64 * 4], row[64 * 3], header[MTV_HEADER_CAPACITY];
    size_t n, first, header_size;
    unsigned i;

    /* A plain MTV image loads top down and opaque. */
    size = 0;
    add_text("3 2\n");
    add_pixels(3, 2, 0);
    assert(mtv_count(data, size) == 1);
    assert(mtv_decode(data, size, 0, &image) == CODEC_OK);
    check(&image, 3, 2, 0);
    mtv_free(&image);
    assert(mtv_decode(data, size, 1, &image) == CODEC_INVALID);

    /* Header lines as sscanf reads them in ImageMagick and netpbm. */
    expect("  3\t 2  anything\n", CODEC_OK);
    expect("3 2\r\n", CODEC_OK);
    expect("+3 +2\n", CODEC_OK);
    expect("003 0002\n", CODEC_OK);
    expect("3\t2\n", CODEC_OK);
    expect("3\n2\n", CODEC_INVALID);
    expect("3,2\n", CODEC_INVALID);
    expect("-3 2\n", CODEC_INVALID);
    expect("3 -2\n", CODEC_INVALID);
    expect("0 2\n", CODEC_INVALID);
    expect("3 0\n", CODEC_INVALID);
    expect(" x3 2\n", CODEC_INVALID);
    /* Not text, so it is reported as QRT: 0x3378 wide. */
    expect("x3 2\n", CODEC_TOO_LARGE);
    /* A long header line is fine up to 4096 bytes. */
    {
        char line[4200];
        memset(line, ' ', sizeof line);
        memcpy(line, "3 2", 3);
        line[4095] = '\n'; line[4096] = 0;
        expect(line, CODEC_OK);
        line[4095] = ' '; line[4096] = '\n'; line[4097] = 0;
        expect(line, CODEC_INVALID);
    }

    /* Trailing bytes that don't form a header are ignored. */
    size = 0;
    add_text("3 2\n");
    add_pixels(3, 2, 0);
    add_text("5x\n");
    assert(mtv_count(data, size) == 1);
    assert(mtv_decode(data, size, 0, &image) == CODEC_OK);
    mtv_free(&image);
    assert(mtv_decode(data, size, 1, &image) == CODEC_INVALID);

    /* Several images, one after another. */
    size = 0;
    add_text("3 2\n");
    add_pixels(3, 2, 0);
    add_text("2 4\n");
    add_pixels(2, 4, 9);
    add_text("1 1\n");
    add_pixels(1, 1, 5);
    assert(mtv_count(data, size) == 3);
    assert(mtv_decode(data, size, 0, &image) == CODEC_OK);
    check(&image, 3, 2, 0);
    mtv_free(&image);
    assert(mtv_decode(data, size, 1, &image) == CODEC_OK);
    check(&image, 2, 4, 9);
    mtv_free(&image);
    assert(mtv_decode(data, size, 2, &image) == CODEC_OK);
    check(&image, 1, 1, 5);
    mtv_free(&image);
    assert(mtv_decode(data, size, 3, &image) == CODEC_INVALID);
    assert(mtv_decode(data, size, 0xffffffffu, &image) == CODEC_INVALID);
    assert(image.rgba == NULL);

    /* A truncated later image still counts and is an error to load; earlier
       ones load. Cut at every byte of the last image. */
    first = 4 + 18 + 4 + 24;
    for (n = first; n < size; n++) {
        assert(mtv_decode(data, n, 0, &image) == CODEC_OK);
        mtv_free(&image);
        assert(mtv_decode(data, n, 1, &image) == CODEC_OK);
        mtv_free(&image);
        if (n < first + 4) {
            /* The third header line isn't complete. */
            assert(mtv_count(data, n) == 2);
            assert(mtv_decode(data, n, 2, &image) == CODEC_INVALID);
        } else {
            assert(mtv_count(data, n) == 3);
            assert(mtv_decode(data, n, 2, &image) == CODEC_TRUNCATED);
        }
    }

    /* Truncation of a single MTV image, anywhere. */
    size = 0;
    add_text("3 2\n");
    add_pixels(3, 2, 0);
    for (n = 0; n < size; n++) {
        assert(mtv_decode(data, n, 0, &image) == CODEC_TRUNCATED);
        assert(image.rgba == NULL && image.width == 0);
    }
    assert(mtv_count(data, 0) == 0);
    assert(mtv_count(data, 3) == 0);
    assert(mtv_count(data, 4) == 1);
    assert(mtv_decode(NULL, 10, 0, &image) == CODEC_TRUNCATED);
    assert(mtv_count(NULL, 10) == 0);
    assert(mtv_decode(data, size, 0, NULL) == CODEC_INVALID);

    /* Size limits: each side 65535, 16M pixels. */
    expect("65536 1\n", CODEC_TOO_LARGE);
    expect("1 65536\n", CODEC_TOO_LARGE);
    expect("99999999999999999999999 1\n", CODEC_TOO_LARGE);
    expect("4097 4096\n", CODEC_TOO_LARGE);
    expect("4096 4096\n", CODEC_TRUNCATED);
    expect("65535 256\n", CODEC_TRUNCATED);
    /* An oversized later image is counted, but not loaded. */
    size = 0;
    add_text("1 1\n");
    add_pixels(1, 1, 0);
    add_text("65536 1\n");
    assert(mtv_count(data, size) == 2);
    assert(mtv_decode(data, size, 1, &image) == CODEC_TOO_LARGE);

    /* QRT, with the planes of each row reassembled. */
    size = 0;
    add_qrt(3, 2, 0);
    assert(mtv_count(data, size) == 1);
    assert(mtv_decode(data, size, 0, &image) == CODEC_OK);
    check(&image, 3, 2, 0);
    mtv_free(&image);
    assert(mtv_decode(data, size, 1, &image) == CODEC_INVALID);
    size = 0;
    add_qrt(17, 9, 3);
    assert(mtv_decode(data, size, 0, &image) == CODEC_OK);
    check(&image, 17, 9, 3);
    mtv_free(&image);
    /* Trailing bytes are ignored. */
    add_text("xx");
    assert(mtv_decode(data, size, 0, &image) == CODEC_OK);
    mtv_free(&image);
    /* Truncation anywhere. */
    size = 0;
    add_qrt(3, 2, 0);
    for (n = 0; n < size; n++) {
        assert(mtv_decode(data, n, 0, &image) == CODEC_TRUNCATED);
        assert(image.rgba == NULL);
    }
    /* Empty and oversized. */
    size = 0;
    add_qrt(0, 2, 0);
    add_pixels(8, 8, 0);
    assert(mtv_decode(data, size, 0, &image) == CODEC_INVALID);
    size = 0;
    add_qrt(2, 0, 0);
    add_pixels(8, 8, 0);
    assert(mtv_decode(data, size, 0, &image) == CODEC_INVALID);
    memcpy(data, "\xff\xff\xff\xff", 4);
    assert(mtv_decode(data, size, 0, &image) == CODEC_TOO_LARGE);
    memcpy(data, "\x01\x10\x00\x10", 4);
    assert(mtv_decode(data, size, 0, &image) == CODEC_TOO_LARGE);
    memcpy(data, "\x00\x10\x00\x10", 4);
    assert(mtv_decode(data, size, 0, &image) == CODEC_TRUNCATED);

    /* A QRT file whose width bytes are text ("1 ", 8241 wide) is still QRT. */
    size = 0;
    add_qrt(0x2031, 1, 0);
    assert(data[0] == '1' && data[1] == ' ');
    assert(mtv_count(data, size) == 1);
    assert(mtv_decode(data, size, 0, &image) == CODEC_OK);
    check(&image, 0x2031, 1, 0);
    mtv_free(&image);
    /* Cut short it is neither format; it starts like text, so MTV's error. */
    assert(mtv_decode(data, size - 1, 0, &image) == CODEC_INVALID);
    /* A complete MTV image wins over reading the same bytes as QRT. */
    size = 0;
    add_text("1 1\n");
    add_pixels(1, 1, 0);
    assert(mtv_decode(data, size, 0, &image) == CODEC_OK);
    check(&image, 1, 1, 0);
    mtv_free(&image);

    /* Headers the encoder writes. */
    assert(mtv_make_header(0, 1, header) == 0);
    assert(mtv_make_header(1, 0, header) == 0);
    assert(mtv_make_header(65536, 1, header) == 0);
    assert(mtv_make_header(1, 65536, header) == 0);
    assert(mtv_make_header(1, 1, NULL) == 0);
    header_size = mtv_make_header(65535, 65535, header);
    assert(header_size == 12 && memcmp(header, "65535 65535\n", 12) == 0);
    header_size = mtv_make_header(1, 10, header);
    assert(header_size == 5 && memcmp(header, "1 10\n", 5) == 0);

    /* Compositing over white; opaque pixels are unchanged. */
    for (i = 0; i < 64; i++) {
        source[i * 4u] = (uint8_t)(i * 4u);
        source[i * 4u + 1u] = (uint8_t)(255u - i);
        source[i * 4u + 2u] = (uint8_t)(i * 7u);
        source[i * 4u + 3u] = 255;
    }
    source[3] = 0;
    source[7] = 128;
    source[4] = 255; source[5] = 0; source[6] = 0;
    mtv_encode_row(source, 64, row);
    assert(row[0] == 255 && row[1] == 255 && row[2] == 255);
    assert(row[3] == 255 && row[4] == 127 && row[5] == 127);
    for (i = 2; i < 64; i++) {
        assert(row[i * 3u] == source[i * 4u]);
        assert(row[i * 3u + 1u] == source[i * 4u + 1u]);
        assert(row[i * 3u + 2u] == source[i * 4u + 2u]);
    }

    /* Encoding then decoding gives the opaque pixels back. */
    size = 0;
    header_size = mtv_make_header(8, 8, header);
    add(header, header_size);
    for (i = 0; i < 8; i++) {
        mtv_encode_row(source + i * 8u * 4u, 8, row);
        add(row, 8 * 3);
    }
    assert(mtv_decode(data, size, 0, &image) == CODEC_OK);
    assert(image.width == 8 && image.height == 8);
    for (i = 2; i < 64; i++)
        assert(memcmp(image.rgba + i * 4u, source + i * 4u, 4) == 0);
    mtv_free(&image);

    puts("mtv codec tests passed");
    return 0;
}
