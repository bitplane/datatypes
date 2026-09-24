#include "../formats/sunraster/decode.h"
#include "../formats/sunraster/encode.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void be32(uint8_t *p, unsigned long value)
{
    p[0] = (uint8_t)(value >> 24); p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8); p[3] = (uint8_t)value;
}

static void header(uint8_t *data, unsigned long width, unsigned long height,
                   unsigned long depth, unsigned long type,
                   unsigned long maptype, unsigned long maplength)
{
    memset(data, 0, 32);
    be32(data, 0x59a66a95ul);
    be32(data + 4, width); be32(data + 8, height); be32(data + 12, depth);
    be32(data + 20, type); be32(data + 24, maptype); be32(data + 28, maplength);
}

static void expect(const uint8_t *data, size_t length,
                   const uint8_t *pixels, unsigned width, unsigned height)
{
    struct sunraster_image image;
    assert(sunraster_decode(data, length, &image) == CODEC_OK);
    assert(image.width == width && image.height == height);
    assert(memcmp(image.rgba, pixels, (size_t)width * height * 4u) == 0);
    sunraster_free(&image);
}

static enum codec_result decode(const uint8_t *data, size_t length)
{
    struct sunraster_image image;
    enum codec_result result = sunraster_decode(data, length, &image);
    assert(result != CODEC_OK || image.rgba != NULL);
    if (result == CODEC_OK)
        sunraster_free(&image);
    else
        assert(image.rgba == NULL && image.width == 0 && image.height == 0);
    return result;
}

int main(void)
{
    uint8_t data[1024], out[64];
    const uint8_t rgb[12] = {255,0,0,255, 0,255,0,255, 0,0,255,255};
    const uint8_t gray[24] = {
        1,1,1,255, 0x80,0x80,0x80,255, 7,7,7,255,
        7,7,7,255, 7,7,7,255, 9,9,9,255};
    const uint8_t rgba[8] = {255,0,0,255, 0,0,255,0};

    /* 24-bit BGR, odd width: 9 pixel bytes padded to 10. */
    header(data, 3, 1, 24, 1, 0, 0);
    memcpy(data + 32, "\0\0\xff\0\xff\0\xff\0\0\0", 10);
    expect(data, 42, rgb, 3, 1);
    assert(decode(data, 41) == CODEC_TRUNCATED);
    header(data, 3, 1, 24, 0, 0, 0);
    expect(data, 42, rgb, 3, 1);

    /* Type 3 stores RGB. */
    header(data, 3, 1, 24, 3, 0, 0);
    memcpy(data + 32, "\xff\0\0\0\xff\0\0\0\xff\0", 10);
    expect(data, 42, rgb, 3, 1);

    /* 32-bit XBGR and XRGB; the pad byte is not alpha. */
    header(data, 3, 1, 32, 1, 0, 0);
    memcpy(data + 32, "\x7f\0\0\xff\0\0\xff\0\x7f\xff\0\0", 12);
    expect(data, 44, rgb, 3, 1);
    header(data, 3, 1, 32, 3, 0, 0);
    memcpy(data + 32, "\0\xff\0\0\x7f\0\xff\0\0\0\0\xff", 12);
    expect(data, 44, rgb, 3, 1);

    /* A colormap on a true-colour image is skipped. */
    header(data, 3, 1, 24, 1, 1, 6);
    memset(data + 32, 0x55, 6);
    memcpy(data + 38, "\0\0\xff\0\xff\0\xff\0\0\0", 10);
    expect(data, 48, rgb, 3, 1);

    /* 1-bit without a colormap: 1 is black, 0 is white; rows pad to 16 bits. */
    header(data, 2, 2, 1, 1, 0, 0);
    data[32] = 0x80; data[33] = 0xff; data[34] = 0x40; data[35] = 0;
    {
        const uint8_t two[16] = {0,0,0,255, 255,255,255,255,
                                 255,255,255,255, 0,0,0,255};
        expect(data, 36, two, 2, 2);
    }
    assert(decode(data, 35) == CODEC_TRUNCATED);

    /* 1-bit with a planar colormap. */
    header(data, 2, 1, 1, 1, 1, 6);
    memcpy(data + 32, "\xff\0" "\0\0" "\0\xff", 6);
    data[38] = 0x40; data[39] = 0;
    memcpy(out, "\xff\0\0\xff\0\0\xff\xff", 8);
    expect(data, 40, out, 2, 1);
    header(data, 2, 1, 1, 1, 1, 9);
    assert(decode(data, 64) == CODEC_INVALID);

    /* 8-bit with a colormap; an index past its end is invalid. */
    header(data, 3, 1, 8, 1, 1, 9);
    memcpy(data + 32, "\xff\0\0" "\0\xff\0" "\0\0\xff", 9);
    data[41] = 0; data[42] = 1; data[43] = 2; data[44] = 0;
    expect(data, 45, rgb, 3, 1);
    assert(decode(data, 44) == CODEC_TRUNCATED);
    data[43] = 3;
    assert(decode(data, 45) == CODEC_INVALID);
    assert(decode(data, 38) == CODEC_TRUNCATED);

    /* 8-bit without a colormap is grayscale. */
    header(data, 3, 2, 8, 1, 0, 0);
    memcpy(data + 32, "\x01\x80\x07\0\x07\x07\x09\0", 8);
    expect(data, 40, gray, 3, 2);

    /* RLE: a literal, an escaped 0x80 and a two-byte run. */
    header(data, 3, 2, 8, 2, 0, 0);
    {
        const uint8_t packed[] = {
            0x01, 0x80, 0x00, 0x07, 0x00,   /* 01 80 07, pad */
            0x80, 0x01, 0x07, 0x09, 0x00};  /* 07 07 09, pad */
        memcpy(data + 32, packed, sizeof packed);
        expect(data, 32 + sizeof packed, gray, 3, 2);
        assert(decode(data, 32 + sizeof packed - 1) == CODEC_TRUNCATED);
        assert(decode(data, 32 + 7) == CODEC_TRUNCATED);
        assert(decode(data, 32 + 6) == CODEC_TRUNCATED);
        assert(decode(data, 32 + 2) == CODEC_TRUNCATED);
    }
    /* A run may cross a row boundary; bytes after the image are ignored. */
    {
        const uint8_t packed[] = {0x01, 0x80, 0x00, 0x80, 0x03, 0x07,
                                  0x09, 0x00, 0xaa};
        memcpy(data + 32, packed, sizeof packed);
        expect(data, 32 + sizeof packed, gray, 3, 2);
    }
    /* A run past the end of the image is invalid. */
    header(data, 3, 2, 8, 2, 0, 0);
    memcpy(data + 32, "\x80\x08\x07", 3);
    assert(decode(data, 35) == CODEC_INVALID);
    memcpy(data + 32, "\x80\x07\x07", 3);
    assert(decode(data, 35) == CODEC_OK);

    /* Malformed headers. */
    header(data, 1, 1, 8, 1, 0, 0);
    assert(decode(data, 34) == CODEC_OK);
    assert(decode(data, 31) == CODEC_TRUNCATED);
    assert(decode(NULL, 0) == CODEC_TRUNCATED);
    assert(sunraster_decode(data, 34, NULL) == CODEC_INVALID);
    data[0] = 0x95;
    assert(decode(data, 34) == CODEC_INVALID);
    header(data, 1, 1, 4, 1, 0, 0);
    assert(decode(data, 64) == CODEC_INVALID);
    header(data, 1, 1, 8, 4, 0, 0);
    assert(decode(data, 64) == CODEC_INVALID);
    header(data, 1, 1, 8, 1, 2, 3);
    assert(decode(data, 64) == CODEC_INVALID);
    header(data, 1, 1, 8, 1, 0, 3);
    assert(decode(data, 64) == CODEC_INVALID);
    /* An RGB map type with zero entries has no map and uses grayscale. */
    header(data, 1, 1, 8, 1, 1, 0);
    data[32] = 7; data[33] = 0;
    {
        const uint8_t pixel[4] = {7, 7, 7, 255};
        expect(data, 34, pixel, 1, 1);
    }
    header(data, 1, 1, 1, 1, 1, 0);
    data[32] = 0; data[33] = 0;
    {
        const uint8_t pixel[4] = {255, 255, 255, 255};
        expect(data, 34, pixel, 1, 1);
    }
    header(data, 1, 1, 8, 1, 1, 4);
    assert(decode(data, 64) == CODEC_INVALID);
    header(data, 1, 1, 8, 1, 1, 771);
    assert(decode(data, 1024) == CODEC_INVALID);
    header(data, 1, 1, 8, 1, 1, 0xfffffff3ul);
    assert(decode(data, 64) == CODEC_INVALID);
    header(data, 1, 1, 24, 1, 1, 0xfffffff3ul);
    assert(decode(data, 64) == CODEC_TRUNCATED);
    header(data, 0, 1, 8, 1, 0, 0);
    assert(decode(data, 64) == CODEC_INVALID);
    header(data, 1, 0, 8, 1, 0, 0);
    assert(decode(data, 64) == CODEC_INVALID);
    header(data, 65536, 1, 8, 1, 0, 0);
    assert(decode(data, 64) == CODEC_TOO_LARGE);
    header(data, 0xfffffffful, 0xfffffffful, 32, 2, 0, 0);
    assert(decode(data, 64) == CODEC_TOO_LARGE);
    header(data, 4097, 4096, 8, 1, 0, 0);
    assert(decode(data, 64) == CODEC_TOO_LARGE);
    header(data, 4096, 4096, 32, 1, 0, 0);
    assert(decode(data, 64) == CODEC_TRUNCATED);
    header(data, 4096, 4096, 32, 2, 0, 0);
    memcpy(data + 32, "\x80\xff\x00", 3);
    assert(decode(data, 35) == CODEC_TRUNCATED);

    /* Encoding: BGR over white, padded to even, then read back. */
    assert(sunraster_make_header(2, 1, data));
    assert(memcmp(data, "\x59\xa6\x6a\x95\0\0\0\x02\0\0\0\x01\0\0\0\x18"
                        "\0\0\0\x06\0\0\0\x01\0\0\0\0\0\0\0\0", 32) == 0);
    assert(sunraster_encode_row(rgba, 2, data + 32, 6) == 6);
    assert(memcmp(data + 32, "\0\0\xff\xff\xff\xff", 6) == 0);
    memcpy(out, "\xff\0\0\xff\xff\xff\xff\xff", 8);
    expect(data, 38, out, 2, 1);
    assert(sunraster_encode_row(rgba, 2, data + 32, 5) == 0);
    assert(sunraster_encode_row(rgb, 3, data + 32, 9) == 0);
    assert(sunraster_encode_row(rgb, 3, data + 32, 10) == 10);
    assert(data[41] == 0);
    assert(sunraster_make_header(3, 1, data));
    expect(data, 42, rgb, 3, 1);
    assert(!sunraster_make_header(0, 1, data));
    assert(!sunraster_make_header(1, 0, data));
    assert(!sunraster_make_header(65536, 1, data));
    assert(!sunraster_make_header(65535, 65535, data));
    assert(sunraster_make_header(65535, 21000, data));
    assert(sunraster_encode_row(NULL, 1, out, sizeof out) == 0);
    assert(sunraster_encode_row(rgb, 0, out, sizeof out) == 0);

    puts("sunraster tests passed");
    return 0;
}
