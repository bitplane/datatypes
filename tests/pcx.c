#include "../formats/pcx/decode.h"
#include "../formats/pcx/encode.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void header(uint8_t *data, unsigned width, unsigned height,
                   unsigned bits, unsigned planes, unsigned bytes_per_line)
{
    memset(data, 0, 128);
    data[0] = 0x0a; data[1] = 5; data[2] = 0; data[3] = (uint8_t)bits;
    data[8] = (uint8_t)(width - 1u); data[9] = (uint8_t)((width - 1u) >> 8);
    data[10] = (uint8_t)(height - 1u); data[11] = (uint8_t)((height - 1u) >> 8);
    data[65] = (uint8_t)planes;
    data[66] = (uint8_t)bytes_per_line;
    data[67] = (uint8_t)(bytes_per_line >> 8);
    data[68] = 1;
}

static void expect(const uint8_t *data, size_t length,
                   const uint8_t *pixels, unsigned width, unsigned height)
{
    struct pcx_image image;
    assert(pcx_decode(data, length, &image) == PCX_OK);
    assert(image.width == width && image.height == height);
    assert(memcmp(image.rgba, pixels, (size_t)width * height * 4u) == 0);
    pcx_free(&image);
}

int main(void)
{
    uint8_t data[128 + 1000] = {0};
    struct pcx_image image;
    const uint8_t rgb[12] = {255,0,0,255, 0,255,0,255, 0,0,255,255};

    header(data, 3, 1, 4, 1, 2);
    data[16 + 3] = 255; data[16 + 7] = 255; data[16 + 11] = 255;
    data[128] = 0x12; data[129] = 0x30;
    expect(data, 130, rgb, 3, 1);
    data[66] = 1;
    assert(pcx_decode(data, 130, &image) == PCX_INVALID);

    header(data, 3, 1, 1, 3, 2);
    data[16 + 3] = 255; data[16 + 7] = 255; data[16 + 11] = 255;
    data[128] = 0xa0; data[130] = 0x60; data[132] = 0;
    expect(data, 134, rgb, 3, 1);

    header(data, 3, 1, 8, 1, 4);
    data[128] = 1; data[129] = 2; data[130] = 3; data[131] = 0;
    data[132] = 0x0c;
    data[133 + 3] = 255; data[133 + 7] = 255; data[133 + 11] = 255;
    expect(data, 133 + 768, rgb, 3, 1);
    assert(pcx_decode(data, 132, &image) == PCX_INVALID);
    data[132] = 0;
    assert(pcx_decode(data, 133 + 768, &image) == PCX_INVALID);

    header(data, 2, 1, 8, 3, 2);
    data[2] = 1;
    data[128] = 0xc1; data[129] = 255; data[130] = 0;
    data[131] = 0; data[132] = 0xc1; data[133] = 255;
    data[134] = 0; data[135] = 0;
    {
        const uint8_t expected[8] = {255,0,0,255, 0,255,0,255};
        expect(data, 136, expected, 2, 1);
    }
    assert(pcx_decode(data, 129, &image) == PCX_TRUNCATED);
    data[128] = 0xc0;
    assert(pcx_decode(data, 136, &image) == PCX_TRUNCATED);

    {
        uint8_t out[128 + 100];
        const uint8_t source[12] = {255,0,0,255, 0,255,0,255, 0,0,255,128};
        const uint8_t expected[12] = {255,0,0,255, 0,255,0,255, 127,127,255,255};
        size_t count;
        assert(pcx_make_header(3, 1, out));
        count = pcx_encode_row(source, 3, out + 128, sizeof out - 128);
        assert(count != 0);
        expect(out, 128 + count, expected, 3, 1);
        assert(!pcx_make_header(65535, 1, out));
        assert(pcx_encode_row(source, 3, out, 1) == 0);
    }
    puts("pcx codec tests passed");
    return 0;
}
