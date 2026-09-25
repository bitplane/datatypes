#include "../formats/avs/decode.h"
#include "../formats/avs/encode.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t data[3 * (8 + 4 * 64) + 16];

static void put32(uint8_t *p, uint32_t v, enum avs_variant variant)
{
    unsigned i;
    for (i = 0; i < 4; i++)
        p[variant == AVS_VARIANT_AVS ? 3u - i : i] = (uint8_t)(v >> (i * 8u));
}

/* Writes a header at offset and returns the offset of its pixels. */
static size_t header(size_t offset, uint32_t width, uint32_t height,
                     enum avs_variant variant)
{
    put32(data + offset, width, variant);
    put32(data + offset + 4, height, variant);
    return offset + 8;
}

/* Pixel i of a test pattern, as RGBA. */
static void pattern(unsigned i, unsigned seed, uint8_t rgba[4])
{
    rgba[0] = (uint8_t)(i * 17u + seed);
    rgba[1] = (uint8_t)(200u - i * 3u);
    rgba[2] = (uint8_t)(i * 5u + 1u);
    rgba[3] = (uint8_t)(i * 29u + seed);
}

/* Fills an image of width * height pixels at offset in file order. */
static void pixels(size_t offset, unsigned count, unsigned seed,
                   enum avs_variant variant)
{
    uint8_t rgba[4];
    unsigned i;
    for (i = 0; i < count; i++) {
        pattern(i, seed, rgba);
        avs_encode_row(variant, rgba, 1, data + offset + i * 4u);
    }
}

static void expect(const struct avs_image *image, unsigned width,
                   unsigned height, unsigned seed)
{
    uint8_t rgba[4];
    unsigned i;
    assert(image->width == width && image->height == height);
    for (i = 0; i < width * height; i++) {
        pattern(i, seed, rgba);
        if (rgba[3] == 254)
            rgba[3] = 255;
        assert(memcmp(image->rgba + i * 4u, rgba, 4) == 0);
    }
}

static void variant_tests(enum avs_variant variant)
{
    enum avs_variant found;
    struct avs_image image;
    size_t first, second, third, end, n;

    /* Three images back to back; the index picks one, in file order. */
    first = header(0, 4, 3, variant);
    pixels(first, 12, 1, variant);
    second = header(first + 48, 2, 5, variant);
    pixels(second, 10, 2, variant);
    third = header(second + 40, 7, 1, variant);
    pixels(third, 7, 3, variant);
    end = third + 28;
    assert(avs_detect(data, end, &found) && found == variant);
    assert(avs_count(data, end) == 3);
    assert(avs_decode(data, end, 0, &image) == CODEC_OK);
    expect(&image, 4, 3, 1);
    avs_free(&image);
    assert(avs_decode(data, end, 1, &image) == CODEC_OK);
    expect(&image, 2, 5, 2);
    avs_free(&image);
    assert(avs_decode(data, end, 2, &image) == CODEC_OK);
    expect(&image, 7, 1, 3);
    avs_free(&image);
    assert(avs_decode(data, end, 3, &image) == CODEC_INVALID);
    assert(image.rgba == NULL && image.width == 0);
    assert(avs_decode(data, end, 0xffffffffu, &image) == CODEC_INVALID);

    /* Up to seven trailing bytes are padding, not a fourth image. */
    memset(data + end, 0xa5, 7);
    assert(avs_count(data, end + 7) == 3);
    assert(avs_decode(data, end + 7, 3, &image) == CODEC_INVALID);
    /* A zero-sized header ends the list, whatever follows it. */
    header(end, 0, 9, variant);
    assert(avs_count(data, end + 16) == 3);
    header(end, 9, 0, variant);
    assert(avs_count(data, end + 16) == 3);
    assert(avs_decode(data, end + 16, 3, &image) == CODEC_INVALID);

    /* A damaged later image doesn't stop earlier ones from loading. */
    header(end, 3, 3, variant);
    assert(avs_count(data, end + 16) == 3);
    assert(avs_decode(data, end + 16, 3, &image) == CODEC_TRUNCATED);
    assert(avs_decode(data, end + 16, 2, &image) == CODEC_OK);
    avs_free(&image);
    header(end, 65536, 1, variant);
    assert(avs_decode(data, end + 16, 3, &image) == CODEC_TOO_LARGE);
    header(end, 4097, 4096, variant);
    assert(avs_decode(data, end + 16, 3, &image) == CODEC_TOO_LARGE);
    header(end, 0xffffffffu, 0xffffffffu, variant);
    assert(avs_decode(data, end + 16, 3, &image) == CODEC_TOO_LARGE);
    assert(avs_count(data, end + 16) == 3);

    /* Truncation at every byte of the first image, and inside later ones. */
    for (n = 0; n < first + 48; n++) {
        assert(avs_decode(data, n, 0, &image) == CODEC_TRUNCATED);
        assert(image.rgba == NULL && image.width == 0);
        assert(avs_count(data, n) == 0);
    }
    for (n = first + 48 + 8; n < second + 40; n++) {
        assert(avs_decode(data, n, 1, &image) == CODEC_TRUNCATED);
        assert(avs_count(data, n) == 1);
    }
    /* A partial second header is padding. */
    for (n = first + 48; n < first + 48 + 8; n++) {
        assert(avs_count(data, n) == 1);
        assert(avs_decode(data, n, 1, &image) == CODEC_INVALID);
    }

    /* At the limits the size check runs first, so these are only truncated. */
    header(0, 4096, 4096, variant);
    assert(avs_decode(data, sizeof data, 0, &image) == CODEC_TRUNCATED);
    header(0, 65535, 256, variant);
    assert(avs_decode(data, sizeof data, 0, &image) == CODEC_TRUNCATED);
    header(0, 256, 65535, variant);
    assert(avs_decode(data, sizeof data, 0, &image) == CODEC_TRUNCATED);
    header(0, 4097, 4096, variant);
    assert(avs_decode(data, sizeof data, 0, &image) == CODEC_TOO_LARGE);
    assert(avs_count(data, sizeof data) == 0);

    /* Empty images and sides past 65535 can't be told from other data. */
    header(0, 0, 2, variant);
    assert(!avs_detect(data, sizeof data, &found));
    assert(avs_decode(data, sizeof data, 0, &image) == CODEC_INVALID);
    header(0, 2, 0, variant);
    assert(avs_decode(data, sizeof data, 0, &image) == CODEC_INVALID);
    header(0, 65536, 1, variant);
    assert(avs_decode(data, sizeof data, 0, &image) == CODEC_INVALID);
    header(0, 1, 65536, variant);
    assert(avs_decode(data, sizeof data, 0, &image) == CODEC_INVALID);
    assert(avs_count(data, sizeof data) == 0);
}

int main(void)
{
    struct avs_image image;
    enum avs_variant found;
    uint8_t source[64 * 4];
    unsigned i;

    variant_tests(AVS_VARIANT_AVS);
    variant_tests(AVS_VARIANT_AAI);

    /* Byte order and channel order, spelled out. */
    memcpy(data, "\0\0\0\2\0\0\0\1" "\x80\x10\x20\x30" "\x00\x40\x50\x60", 16);
    assert(avs_decode(data, 16, 0, &image) == CODEC_OK);
    assert(image.width == 2 && image.height == 1);
    assert(memcmp(image.rgba, "\x10\x20\x30\x80" "\x40\x50\x60\x00", 8) == 0);
    avs_free(&image);
    memcpy(data, "\2\0\0\0\1\0\0\0" "\x30\x20\x10\x80" "\x60\x50\x40\x00", 16);
    assert(avs_decode(data, 16, 0, &image) == CODEC_OK);
    assert(image.width == 2 && image.height == 1);
    assert(memcmp(image.rgba, "\x10\x20\x30\x80" "\x40\x50\x60\x00", 8) == 0);
    avs_free(&image);

    /* AVS: all-zero alpha is opaque, but any nonzero alpha is kept. */
    memcpy(data, "\0\0\0\2\0\0\0\1" "\x00\x10\x20\x30" "\x00\x40\x50\x60", 16);
    assert(avs_decode(data, 16, 0, &image) == CODEC_OK);
    assert(memcmp(image.rgba, "\x10\x20\x30\xff" "\x40\x50\x60\xff", 8) == 0);
    avs_free(&image);
    data[12] = 1;
    assert(avs_decode(data, 16, 0, &image) == CODEC_OK);
    assert(image.rgba[3] == 0 && image.rgba[7] == 1);
    avs_free(&image);
    /* Each image of a file decides for itself. */
    memcpy(data + 16, "\0\0\0\1\0\0\0\1" "\x00\x70\x80\x90", 12);
    assert(avs_count(data, 28) == 2);
    assert(avs_decode(data, 28, 1, &image) == CODEC_OK);
    assert(memcmp(image.rgba, "\x70\x80\x90\xff", 4) == 0);
    avs_free(&image);

    /* AAI: 254 is opaque, and zero alpha everywhere stays transparent. */
    memcpy(data, "\2\0\0\0\1\0\0\0" "\x30\x20\x10\xfe" "\x60\x50\x40\xfd", 16);
    assert(avs_decode(data, 16, 0, &image) == CODEC_OK);
    assert(image.rgba[3] == 255 && image.rgba[7] == 253);
    avs_free(&image);
    data[11] = 0; data[15] = 0;
    assert(avs_decode(data, 16, 0, &image) == CODEC_OK);
    assert(image.rgba[3] == 0 && image.rgba[7] == 0);
    avs_free(&image);

    /* Bad arguments. */
    assert(avs_decode(NULL, 16, 0, &image) == CODEC_TRUNCATED);
    assert(avs_decode(data, 16, 0, NULL) == CODEC_INVALID);
    assert(avs_count(NULL, 16) == 0);
    assert(!avs_detect(NULL, 16, &found));
    assert(!avs_detect(data, 7, &found));

    /* Encoding then decoding gives back every pixel in both variants. */
    for (i = 0; i < 2; i++) {
        enum avs_variant variant = i ? AVS_VARIANT_AAI : AVS_VARIANT_AVS;
        unsigned j;
        assert(!avs_make_header(variant, 0, 1, data));
        assert(!avs_make_header(variant, 1, 0, data));
        assert(!avs_make_header(variant, 65536, 1, data));
        assert(!avs_make_header(variant, 1, 65536, data));
        assert(!avs_make_header(variant, 1, 1, NULL));
        for (j = 0; j < 64 * 4; j++)
            source[j] = (uint8_t)(j * 7u + 3u);
        source[3] = 0;
        assert(avs_make_header(variant, 16, 4, data));
        assert(memcmp(data, variant ? "\x10\0\0\0\4\0\0\0" :
                      "\0\0\0\x10\0\0\0\4", 8) == 0);
        for (j = 0; j < 4; j++)
            avs_encode_row(variant, source + j * 64u, 16, data + 8 + j * 64u);
        assert(avs_decode(data, 8 + 64 * 4, 0, &image) == CODEC_OK);
        assert(image.width == 16 && image.height == 4);
        for (j = 0; j < 64 * 4; j++)
            assert(image.rgba[j] == source[j] ||
                   (variant && j % 4 == 3 && source[j] == 254 &&
                    image.rgba[j] == 255));
        avs_free(&image);
    }

    puts("avs: ok");
    return 0;
}
