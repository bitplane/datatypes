#include "../formats/qoi/decode.h"
#include "../formats/qoi/encode.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint8_t end[8] = {0,0,0,0,0,0,0,1};

static void expect(const uint8_t *data, size_t length,
                   const uint8_t *pixels, unsigned width, unsigned height)
{
    struct qoi_image image;
    assert(qoi_decode(data, length, &image) == CODEC_OK);
    assert(image.width == width && image.height == height);
    assert(memcmp(image.rgba, pixels, (size_t)width * height * 4u) == 0);
    qoi_free(&image);
}

int main(void)
{
    uint8_t data[14 + 5 * 200 + 9] = {0};
    struct qoi_image image;
    struct qoi_encoder encoder;
    size_t pos, count;
    unsigned x;
    uint8_t source[128 * 4];

    assert(qoi_make_header(3, 1, data));
    data[12] = 3;
    data[14] = 0xfe; data[15] = 10; data[16] = 20; data[17] = 30;
    data[18] = 0xc1; /* two repeats */
    memcpy(data + 19, end, 8);
    {
        const uint8_t expected[12] = {10,20,30,255, 10,20,30,255, 10,20,30,255};
        expect(data, 27, expected, 3, 1);
    }
    data[18] = 0xfd; /* run crosses image bounds */
    assert(qoi_decode(data, 27, &image) == CODEC_INVALID);
    data[18] = 0xc1;
    assert(qoi_decode(data, 26, &image) == CODEC_INVALID);

    for (x = 0; x < 128; x++) {
        source[x * 4u] = (uint8_t)(x / 4u);
        source[x * 4u + 1u] = (uint8_t)(x / 3u);
        source[x * 4u + 2u] = (uint8_t)(x * 7u);
        source[x * 4u + 3u] = x < 64 ? 255 : (uint8_t)(x * 2u);
    }
    assert(qoi_make_header(64, 2, data));
    qoi_encoder_init(&encoder);
    pos = 14;
    count = qoi_encode_row(&encoder, source, 64, data + pos, sizeof data - pos);
    assert(count != SIZE_MAX); pos += count;
    count = qoi_encode_row(&encoder, source + 64 * 4, 64, data + pos, sizeof data - pos);
    assert(count != SIZE_MAX); pos += count;
    count = qoi_encode_end(&encoder, data + pos, sizeof data - pos);
    assert(count != SIZE_MAX); pos += count;
    expect(data, pos, source, 64, 2);

    qoi_encoder_init(&encoder);
    memset(source, 0, sizeof source);
    for (x = 0; x < 128; x++) source[x * 4u + 3u] = 255;
    assert(qoi_make_header(64, 2, data));
    pos = 14;
    count = qoi_encode_row(&encoder, source, 64, data + pos, sizeof data - pos);
    assert(count != SIZE_MAX); pos += count;
    count = qoi_encode_row(&encoder, source + 64 * 4, 64, data + pos, sizeof data - pos);
    assert(count != SIZE_MAX); pos += count;
    count = qoi_encode_end(&encoder, data + pos, sizeof data - pos);
    assert(count != SIZE_MAX); pos += count;
    expect(data, pos, source, 64, 2);
    assert(qoi_encode_end(&encoder, data, 7) == SIZE_MAX);
    puts("qoi codec tests passed");
    return 0;
}
