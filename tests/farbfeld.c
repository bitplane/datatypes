#include "../formats/farbfeld/decode.h"
#include "../formats/farbfeld/encode.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t data[16 + 8 * 256 + 4];

static void header(uint32_t width, uint32_t height)
{
    memcpy(data, "farbfeld", 8);
    data[8] = (uint8_t)(width >> 24); data[9] = (uint8_t)(width >> 16);
    data[10] = (uint8_t)(width >> 8); data[11] = (uint8_t)width;
    data[12] = (uint8_t)(height >> 24); data[13] = (uint8_t)(height >> 16);
    data[14] = (uint8_t)(height >> 8); data[15] = (uint8_t)height;
}

static void put16(size_t pixel, unsigned channel, unsigned value)
{
    data[16 + pixel * 8u + channel * 2u] = (uint8_t)(value >> 8);
    data[16 + pixel * 8u + channel * 2u + 1u] = (uint8_t)value;
}

int main(void)
{
    struct farbfeld_image image;
    uint8_t source[256 * 4];
    unsigned v, i;
    size_t n;

    /* Every 16-bit value rounds to the nearest 8-bit one, as round(v / 257). */
    header(1, 1);
    for (v = 0; v < 65536; v++) {
        put16(0, 0, v); put16(0, 1, 65535 - v); put16(0, 2, v); put16(0, 3, 65535);
        assert(farbfeld_decode(data, 24, &image) == CODEC_OK);
        assert(image.width == 1 && image.height == 1);
        assert(image.rgba[0] == (v * 2u + 257u) / 514u);
        assert(image.rgba[1] == ((65535u - v) * 2u + 257u) / 514u);
        assert(image.rgba[3] == 255);
        farbfeld_free(&image);
    }

    /* Straight alpha is kept, and rows come out top down. */
    header(2, 2);
    for (i = 0; i < 4; i++) {
        put16(i, 0, i * 0x1111u); put16(i, 1, 0x8080u);
        put16(i, 2, 0xffffu - i); put16(i, 3, i * 0x4040u);
    }
    assert(farbfeld_decode(data, 16 + 32, &image) == CODEC_OK);
    assert(image.width == 2 && image.height == 2);
    for (i = 0; i < 4; i++) {
        assert(image.rgba[i * 4u] == i * 0x11u);
        assert(image.rgba[i * 4u + 1u] == 0x80);
        assert(image.rgba[i * 4u + 2u] == 0xff);
        assert(image.rgba[i * 4u + 3u] == i * 0x40u);
    }
    farbfeld_free(&image);

    /* A tiny nonzero alpha still counts as alpha. */
    for (i = 0; i < 4; i++) put16(i, 3, 0);
    put16(3, 3, 1);
    assert(farbfeld_decode(data, 16 + 32, &image) == CODEC_OK);
    assert(image.rgba[3] == 0 && image.rgba[15] == 0);
    farbfeld_free(&image);

    /* An all-zero alpha channel is transparent; colour is untouched. */
    put16(3, 3, 0);
    assert(farbfeld_decode(data, 16 + 32, &image) == CODEC_OK);
    for (i = 0; i < 4; i++) {
        assert(image.rgba[i * 4u] == i * 0x11u);
        assert(image.rgba[i * 4u + 3u] == 0);
    }
    farbfeld_free(&image);

    /* Trailing bytes are ignored. */
    assert(farbfeld_decode(data, 16 + 32 + 3, &image) == CODEC_OK);
    farbfeld_free(&image);

    /* Truncation anywhere, in the header or the pixels. */
    for (n = 0; n < 16 + 32; n++) {
        assert(farbfeld_decode(data, n, &image) == CODEC_TRUNCATED);
        assert(image.rgba == NULL && image.width == 0);
    }
    assert(farbfeld_decode(NULL, 48, &image) == CODEC_TRUNCATED);
    assert(farbfeld_decode(data, 48, NULL) == CODEC_INVALID);

    /* Bad magic, including a case change. */
    data[0] = 'F';
    assert(farbfeld_decode(data, 48, &image) == CODEC_INVALID);
    data[0] = 'f'; data[7] = 'D';
    assert(farbfeld_decode(data, 48, &image) == CODEC_INVALID);
    data[7] = 'd';

    /* Empty and oversized dimensions. */
    header(0, 2);
    assert(farbfeld_decode(data, sizeof data, &image) == CODEC_INVALID);
    header(2, 0);
    assert(farbfeld_decode(data, sizeof data, &image) == CODEC_INVALID);
    header(65536, 1);
    assert(farbfeld_decode(data, sizeof data, &image) == CODEC_TOO_LARGE);
    header(1, 65536);
    assert(farbfeld_decode(data, sizeof data, &image) == CODEC_TOO_LARGE);
    header(0xffffffffu, 0xffffffffu);
    assert(farbfeld_decode(data, sizeof data, &image) == CODEC_TOO_LARGE);
    header(4097, 4096);
    assert(farbfeld_decode(data, sizeof data, &image) == CODEC_TOO_LARGE);
    assert(image.rgba == NULL);
    /* At the limits the size check runs first, so these are only truncated. */
    header(4096, 4096);
    assert(farbfeld_decode(data, sizeof data, &image) == CODEC_TRUNCATED);
    header(65535, 256);
    assert(farbfeld_decode(data, sizeof data, &image) == CODEC_TRUNCATED);
    header(256, 1);
    assert(farbfeld_decode(data, 16 + 8 * 256 - 1, &image) == CODEC_TRUNCATED);

    /* Encoding then decoding gives back every 8-bit value. */
    assert(!farbfeld_make_header(0, 1, data));
    assert(!farbfeld_make_header(1, 0, data));
    assert(!farbfeld_make_header(65536, 1, data));
    assert(!farbfeld_make_header(1, 65536, data));
    assert(!farbfeld_make_header(1, 1, NULL));
    for (i = 0; i < 256; i++) {
        source[i * 4u] = (uint8_t)i;
        source[i * 4u + 1u] = (uint8_t)(255u - i);
        source[i * 4u + 2u] = (uint8_t)(i * 7u);
        source[i * 4u + 3u] = (uint8_t)(i * 3u);
    }
    assert(farbfeld_make_header(64, 4, data));
    assert(memcmp(data, "farbfeld\0\0\0\x40\0\0\0\x04", 16) == 0);
    for (i = 0; i < 4; i++)
        farbfeld_encode_row(source + i * 64u * 4u, 64, data + 16 + i * 64u * 8u);
    assert(data[16] == 0 && data[17] == 0 && data[18] == 0xff && data[19] == 0xff);
    assert(farbfeld_decode(data, 16 + 8 * 256, &image) == CODEC_OK);
    assert(image.width == 64 && image.height == 4);
    assert(memcmp(image.rgba, source, sizeof source) == 0);
    farbfeld_free(&image);

    puts("farbfeld codec tests passed");
    return 0;
}
