#include "../formats/targa/decode.h"

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
    assert(tga_decode(data, length, &image) == TGA_OK);
    assert(image.width == width && image.height == height);
    assert(memcmp(image.rgba, pixels, (size_t)width * height * 4u) == 0);
    tga_free(&image);
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
    assert(tga_decode(data, 26, &image) == TGA_INVALID);

    header(data, 11, 1, 1, 16, 0x28); /* RLE gray + alpha */
    data[18] = 0; data[19] = 77; data[20] = 42;
    expect(data, 21, gray_alpha, 1, 1);

    header(data, 2, 1, 1, 32, 0x28);
    data[18] = 30; data[19] = 20; data[20] = 10; data[21] = 40;
    expect(data, 22, rgba, 1, 1);
    assert(tga_decode(data, 21, &image) == TGA_TRUNCATED);

    header(data, 2, 1, 1, 16, 0x21);
    data[18] = 0; data[19] = 0xfc; /* opaque red in 5:5:5:1 */
    expect(data, 20, red, 1, 1);

    header(data, 10, 1, 1, 24, 0x20);
    data[18] = 0x81; /* two-pixel packet into one-pixel image */
    assert(tga_decode(data, 22, &image) == TGA_INVALID);
    data[17] = 0xe0; /* interleaved scan lines unsupported */
    assert(tga_decode(data, 22, &image) == TGA_INVALID);
    header(data, 2, 65535, 65535, 24, 0);
    assert(tga_decode(data, 18, &image) == TGA_TOO_LARGE);
    puts("targa decoder tests passed");
    return 0;
}
