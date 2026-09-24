#include "../formats/targa/decode.h"
#include "../formats/targa/encode.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void header(uint8_t *data, unsigned type, unsigned width,
                   unsigned height, unsigned depth, unsigned descriptor)
{
    memset(data, 0, 18);
    data[2] = (uint8_t)type;
    data[12] = (uint8_t)width;
    data[13] = (uint8_t)(width >> 8);
    data[14] = (uint8_t)height;
    data[15] = (uint8_t)(height >> 8);
    data[16] = (uint8_t)depth;
    data[17] = (uint8_t)descriptor;
}

static void expect(const uint8_t *data, size_t length,
                   const uint8_t *pixels, unsigned width, unsigned height)
{
    struct tga_image image;
    assert(tga_decode(data, length, &image) == CODEC_OK);
    assert(image.width == width && image.height == height);
    assert(memcmp(image.rgba, pixels, (size_t)width * height * 4u) == 0);
    tga_free(&image);
}

static void expect_encoded(unsigned width, unsigned bytes_per_pixel,
                           unsigned pattern)
{
    uint8_t source[300 * 4], file[18 + 300 * 5];
    struct tga_image image;
    size_t length;
    unsigned x;

    assert(width <= 300);
    header(file, 10, width, 1, bytes_per_pixel * 8u,
           bytes_per_pixel == 4 ? 0x28 : 0x20);
    for (x = 0; x < width; x++) {
        unsigned value = pattern == 0 ? 7 : pattern == 1 ? x : x / 3;
        source[x * 4u] = (uint8_t)value;
        source[x * 4u + 1u] = (uint8_t)(value * 3u);
        source[x * 4u + 2u] = (uint8_t)(value * 5u);
        source[x * 4u + 3u] = bytes_per_pixel == 4 ? (uint8_t)(x / 3u) : 255;
    }
    length = tga_encode_row(source, width, bytes_per_pixel, file + 18,
                            sizeof file - 18);
    assert(length > 0);
    if (pattern == 0 && bytes_per_pixel == 3)
        assert(length < (size_t)width * 3u);
    assert(tga_decode(file, length + 18, &image) == CODEC_OK);
    assert(image.width == width && image.height == 1);
    assert(memcmp(image.rgba, source, (size_t)width * 4u) == 0);
    tga_free(&image);
}

static void expect_extension_alpha(unsigned type, const uint8_t expected[4])
{
    uint8_t data[18 + 4 + 495 + 26] = {0};
    struct tga_image image;
    const size_t extension = 22;
    const size_t footer = extension + 495;

    header(data, 2, 1, 1, 32, 0x28);
    data[18] = 16; data[19] = 32; data[20] = 64; data[21] = 128;
    data[extension] = 495 & 255;
    data[extension + 1] = 495 >> 8;
    data[extension + 494] = (uint8_t)type;
    data[footer] = (uint8_t)extension;
    memcpy(data + footer + 8, "TRUEVISION-XFILE.\0", 18);
    expect(data, sizeof data, expected, 1, 1);
    data[21] = 0;
    {
        const uint8_t transparent[4] = {64, 32, 16, 0};
        const uint8_t opaque[4] = {64, 32, 16, 255};
        const uint8_t premultiplied[4] = {0, 0, 0, 0};
        expect(data, sizeof data,
               type == 4 ? premultiplied : type >= 2 ? transparent : opaque,
               1, 1);
    }
    data[footer] = 255;
    data[footer + 1] = 255;
    assert(tga_decode(data, sizeof data, &image) == CODEC_INVALID);
}

int main(void)
{
    uint8_t data[64] = {0};
    struct tga_image image;
    const uint8_t red[4] = {255, 0, 0, 255};
    const uint8_t two_red[8] = {255, 0, 0, 255, 255, 0, 0, 255};
    const uint8_t blue_red[8] = {0, 0, 255, 255, 255, 0, 0, 255};
    const uint8_t blue_white_red_green[16] = {
        0, 0, 255, 255, 255, 255, 255, 255,
        255, 0, 0, 255, 0, 255, 0, 255
    };
    const uint8_t gray_alpha[4] = {77, 77, 77, 42};
    const uint8_t rgba[4] = {10, 20, 30, 40};

    header(data, 2, 2, 1, 24, 0x30); /* top/right origin */
    data[18] = 0; data[19] = 0; data[20] = 255; /* red */
    data[21] = 255; data[22] = 0; data[23] = 0; /* blue */
    expect(data, 24, blue_red, 2, 1);

    header(data, 2, 2, 2, 24, 0); /* bottom/left origin */
    data[18] = 0; data[19] = 0; data[20] = 255; /* red */
    data[21] = 0; data[22] = 255; data[23] = 0; /* green */
    data[24] = 255; data[25] = 0; data[26] = 0; /* blue */
    data[27] = 255; data[28] = 255; data[29] = 255; /* white */
    expect(data, 30, blue_white_red_green, 2, 2);

    header(data, 10, 2, 1, 24, 0x20); /* RLE true colour */
    data[18] = 0x81; data[19] = 0; data[20] = 0; data[21] = 255;
    expect(data, 22, two_red, 2, 1);

    header(data, 1, 2, 1, 8, 0x20); /* colour map starts at index 5 */
    data[1] = 1; data[3] = 5; data[5] = 2; data[7] = 24;
    data[18] = 0; data[19] = 0; data[20] = 255; /* red */
    data[21] = 255; data[22] = 0; data[23] = 0; /* blue */
    data[24] = 6; data[25] = 5;
    expect(data, 26, blue_red, 2, 1);
    data[25] = 4;
    assert(tga_decode(data, 26, &image) == CODEC_INVALID);

    header(data, 1, 1, 1, 8, 0x28); /* 32-bit palette with transparency */
    data[1] = 1; data[5] = 1; data[7] = 32;
    data[18] = 30; data[19] = 20; data[20] = 10; data[21] = 40;
    data[22] = 0;
    expect(data, 23, rgba, 1, 1);

    header(data, 11, 1, 1, 16, 0x28); /* RLE gray + alpha */
    data[18] = 0; data[19] = 77; data[20] = 42;
    expect(data, 21, gray_alpha, 1, 1);

    header(data, 2, 1, 1, 32, 0x28);
    data[18] = 30; data[19] = 20; data[20] = 10; data[21] = 40;
    expect(data, 22, rgba, 1, 1);
    assert(tga_decode(data, 21, &image) == CODEC_TRUNCATED);

    header(data, 2, 1, 1, 16, 0x21);
    data[18] = 0; data[19] = 0xfc; /* opaque red in 5:5:5:1 */
    expect(data, 20, red, 1, 1);
    data[19] = 0x7c; /* declared alpha bit is clear */
    {
        const uint8_t transparent_red[4] = {255, 0, 0, 0};
        expect(data, 20, transparent_red, 1, 1);
    }
    header(data, 2, 2, 1, 16, 0x21);
    data[18] = 0; data[19] = 0xfc;
    data[20] = 0; data[21] = 0x7c;
    {
        const uint8_t red_transparent_red[8] = {255, 0, 0, 255, 255, 0, 0, 0};
        expect(data, 22, red_transparent_red, 2, 1);
    }

    header(data, 2, 1, 1, 32, 0x28); /* declared alpha, all transparent */
    data[18] = 0; data[19] = 0; data[20] = 255; data[21] = 0;
    {
        const uint8_t transparent_red[4] = {255, 0, 0, 0};
        expect(data, 22, transparent_red, 1, 1);
    }

    header(data, 1, 1, 1, 8, 0x20); /* 32-bit palette, no attribute bits */
    data[1] = 1; data[5] = 1; data[7] = 32;
    data[18] = 0; data[19] = 0; data[20] = 255; data[21] = 40;
    data[22] = 0;
    expect(data, 23, red, 1, 1);

    header(data, 1, 1, 1, 8, 0x20); /* 16-bit palette, no attribute bits */
    data[1] = 1; data[5] = 1; data[7] = 16;
    data[18] = 0; data[19] = 0x7c;
    data[20] = 0;
    expect(data, 21, red, 1, 1);

    header(data, 3, 1, 1, 16, 0x20); /* gray + byte, no attribute bits */
    data[18] = 77; data[19] = 42;
    {
        const uint8_t gray[4] = {77, 77, 77, 255};
        expect(data, 20, gray, 1, 1);
    }

    header(data, 2, 1, 1, 24, 0x20); /* unused colour map before pixels */
    data[1] = 1; data[5] = 2; data[7] = 24;
    memset(data + 18, 0x55, 6);
    data[24] = 0; data[25] = 0; data[26] = 255;
    expect(data, 27, red, 1, 1);
    assert(tga_decode(data, 23, &image) == CODEC_TRUNCATED);
    data[2] = 10; /* RLE true colour with an unused colour map */
    data[24] = 0x80; data[25] = 0; data[26] = 0; data[27] = 255;
    expect(data, 28, red, 1, 1);
    data[1] = 2; /* reserved colour map type */
    assert(tga_decode(data, 28, &image) == CODEC_INVALID);

    {
        const uint8_t raw_alpha[4] = {64, 32, 16, 128};
        const uint8_t no_alpha[4] = {64, 32, 16, 255};
        const uint8_t straight_alpha[4] = {128, 64, 32, 128};
        expect_extension_alpha(0, no_alpha);
        expect_extension_alpha(1, no_alpha);
        expect_extension_alpha(2, raw_alpha);
        expect_extension_alpha(3, raw_alpha);
        expect_extension_alpha(4, straight_alpha);
    }

    header(data, 10, 1, 1, 24, 0x20);
    data[18] = 0x81; /* two-pixel packet into one-pixel image */
    assert(tga_decode(data, 22, &image) == CODEC_INVALID);
    data[17] = 0xe0; /* interleaved scan lines unsupported */
    assert(tga_decode(data, 22, &image) == CODEC_INVALID);
    header(data, 2, 65535, 65535, 24, 0);
    assert(tga_decode(data, 18, &image) == CODEC_TOO_LARGE);
    expect_encoded(3, 3, 0);
    expect_encoded(3, 3, 1);
    expect_encoded(3, 3, 2);
    expect_encoded(127, 3, 0);
    expect_encoded(128, 3, 1);
    expect_encoded(129, 3, 0);
    expect_encoded(300, 3, 2);
    expect_encoded(300, 3, 1);
    expect_encoded(300, 3, 0);
    expect_encoded(300, 4, 2);
    assert(tga_encode_row(rgba, 1, 4, data, 1) == 0);
    puts("targa codec tests passed");
    return 0;
}
