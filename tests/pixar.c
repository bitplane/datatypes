#include "../formats/pixar/decode.h"
#include "../formats/pixar/encode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t data[65536];

static void le16(uint8_t *p, unsigned value)
{
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
}

static void le32(uint8_t *p, unsigned long value)
{
    le16(p, (unsigned)(value & 0xffffu)); le16(p + 2, (unsigned)(value >> 16));
}

static void header(unsigned width, unsigned height, unsigned tile_width,
                   unsigned tile_height, unsigned format, unsigned storage,
                   unsigned blocking, unsigned alpha_mode)
{
    memset(data, 0, sizeof data);
    data[0] = 0x80; data[1] = 0xe8;
    le16(data + 4, 1);
    le16(data + 416, height); le16(data + 418, width);
    le16(data + 420, tile_height); le16(data + 422, tile_width);
    le16(data + 424, format); le16(data + 426, storage);
    le16(data + 428, blocking); le16(data + 430, alpha_mode);
}

static void tile(unsigned index, unsigned long offset, unsigned long length)
{
    le32(data + 512 + index * 8u, offset); le32(data + 516 + index * 8u, length);
}

/* Packet word: flag in the top 4 bits, count - 1 in the rest. */
static size_t packet(size_t pos, unsigned flag, unsigned count)
{
    le16(data + pos, flag << 12 | (count - 1u));
    return pos + 2;
}

static void expect(size_t length, const uint8_t *pixels, unsigned width, unsigned height)
{
    struct pixar_image image;
    assert(pixar_decode(data, length, &image) == CODEC_OK);
    assert(image.width == width && image.height == height);
    assert(memcmp(image.rgba, pixels, (size_t)width * height * 4u) == 0);
    pixar_free(&image);
}

static enum codec_result decode(size_t length)
{
    struct pixar_image image;
    enum codec_result result = pixar_decode(data, length, &image);
    assert(result != CODEC_OK || image.rgba != NULL);
    if (result == CODEC_OK)
        pixar_free(&image);
    else
        assert(image.rgba == NULL && image.width == 0 && image.height == 0);
    return result;
}

static void set_format(unsigned format) { le16(data + 424, format); }
static void set_storage(unsigned storage) { le16(data + 426, storage); }

int main(void)
{
    const uint8_t rgb[12] = {255,0,0,255, 0,255,0,255, 0,0,255,255};
    const uint8_t gray[8] = {10,10,10,255, 200,200,200,255};
    const uint8_t gray_alpha[8] = {10,10,10,128, 200,200,200,0};
    const uint8_t straight[8] = {200,100,0,128, 50,60,70,0};
    uint8_t out[16], big[4 * 5 * 4], hdr[PIXAR_ENCODE_HEADER];
    size_t pos, i;

    /* Dumped RGB, as Photoshop writes it: one tile at 1024. */
    header(3, 1, 3, 1, 14, 2, 1024, 0);
    tile(0, 1024, 9);
    memcpy(data + 1024, "\xff\0\0\0\xff\0\0\0\xff", 9);
    expect(1033, rgb, 3, 1);
    assert(decode(1032) == CODEC_TRUNCATED);
    /* Trailing padding is ignored. */
    expect(2048, rgb, 3, 1);

    /* Grey is the R channel alone; any single channel loads as grey. */
    header(2, 1, 2, 1, 8, 2, 1024, 0);
    tile(0, 600, 2);
    data[600] = 10; data[601] = 200;
    expect(602, gray, 2, 1);
    set_format(4); expect(602, gray, 2, 1);
    set_format(2); expect(602, gray, 2, 1);
    set_format(1); expect(602, gray, 2, 1);

    /* R + A is grey with alpha; unassociated alpha is kept as is. */
    header(2, 1, 2, 1, 9, 2, 1024, 1);
    tile(0, 600, 4);
    memcpy(data + 600, "\x0a\x80\xc8\x00", 4);
    expect(604, gray_alpha, 2, 1);

    /* RGBA, unassociated, including a pixel whose alpha is zero. */
    header(2, 1, 2, 1, 15, 2, 1024, 1);
    tile(0, 600, 8);
    memcpy(data + 600, straight, 8);
    expect(608, straight, 2, 1);
    /* Matted to black is premultiplied: 100/128 of 255 rounds to 199, and
       colours brighter than their alpha clamp. */
    header(2, 1, 2, 1, 15, 2, 1024, 0);
    tile(0, 600, 8);
    memcpy(data + 600, "\xc8\x40\x00\x80\x32\x3c\x46\x00", 8);
    {
        const uint8_t expected[8] = {255,128,0,128, 0,0,0,0};
        expect(608, expected, 2, 1);
    }
    data[601] = 100;
    {
        const uint8_t expected[8] = {255,199,0,128, 0,0,0,0};
        expect(608, expected, 2, 1);
    }
    /* All-zero declared alpha stays transparent. */
    memset(data + 600, 0, 8);
    {
        const uint8_t expected[8] = {0};
        expect(608, expected, 2, 1);
    }

    /* Encoded RGBA with every packet type, blocks counted from the tile. */
    header(5, 2, 5, 2, 15, 0, 64, 1);
    tile(0, 640, 128);
    pos = packet(640, 1, 1);                 /* Full dump: one RGBA pixel. */
    memcpy(data + pos, "\x01\x02\x03\x04", 4); pos += 4;
    pos = packet(pos, 2, 2);                 /* Full run: 2 and 1 copies. */
    memcpy(data + pos, "\x01\x05\x06\x07\x08\x00\x09\x0a\x0b\x0c", 10); pos += 10;
    pos = packet(pos, 0, 1);                 /* End of block: skip to 704. */
    pos = packet(704, 3, 2);                 /* Partial dump: A, then 2 RGB. */
    memcpy(data + pos, "\x80\x10\x11\x12\x13\x14\x15", 7); pos += 7;
    pos = packet(pos, 4, 1);                 /* Partial run: A, then 3 copies. */
    memcpy(data + pos, "\xff\x02\x20\x21\x22", 5); pos += 5;
    {
        const uint8_t expected[40] = {
            1,2,3,4, 5,6,7,8, 5,6,7,8, 9,10,11,12, 16,17,18,128,
            19,20,21,128, 32,33,34,255, 32,33,34,255, 32,33,34,255, 0,0,0,0};
        /* Nine of ten pixels so far. */
        assert(decode(pos) == CODEC_TRUNCATED);
        pos = packet(pos, 1, 1);
        memcpy(data + pos, "\0\0\0\0", 4); pos += 4;
        expect(pos, expected, 5, 2);
        for (i = 512; i < pos; i++)
            assert(decode(i) == CODEC_TRUNCATED);
    }

    /* Encoded RGB and grey use full packets only; a run past the end of the
       tile is clamped. */
    header(3, 1, 3, 1, 14, 0, 1024, 0);
    tile(0, 1024, 1024);
    pos = packet(1024, 1, 1);
    memcpy(data + pos, "\xff\0\0", 3); pos += 3;
    pos = packet(pos, 2, 2);
    memcpy(data + pos, "\x00\x00\xff\x00\x09\x00\x00\xff", 8); pos += 8;
    expect(pos, rgb, 3, 1);
    header(2, 1, 2, 1, 8, 0, 1024, 0);
    tile(0, 1024, 1024);
    pos = packet(1024, 1, 1); data[pos++] = 10;
    pos = packet(pos, 2, 1); data[pos++] = 0; data[pos++] = 200;
    expect(pos, gray, 2, 1);
    /* Partial packets need all four channels. */
    header(3, 1, 3, 1, 14, 0, 1024, 0);
    tile(0, 1024, 1024);
    pos = packet(1024, 3, 1);
    assert(decode(pos + 4) == CODEC_INVALID);
    packet(1024, 4, 1);
    assert(decode(pos + 5) == CODEC_INVALID);
    /* Unknown flags are invalid; so is an end of block with no block size. */
    packet(1024, 5, 1);
    assert(decode(1100) == CODEC_INVALID);
    packet(1024, 0, 1);
    le16(data + 428, 0);
    assert(decode(1100) == CODEC_INVALID);
    /* With fewer than two bytes left in a block, the next packet starts in the
       next block. Read one byte early, this one would have flag 5. */
    header(86, 1, 86, 1, 8, 0, 8, 0);
    tile(0, 600, 96);
    pos = packet(600, 1, 5);
    memcpy(data + pos, "\1\2\3\4\5", 5);
    data[607] = 0xee;
    pos = packet(608, 1, 81);
    memset(data + pos, 77, 81);
    {
        uint8_t expected[86 * 4];
        for (i = 0; i < 86; i++) {
            expected[i * 4u] = expected[i * 4u + 1] = expected[i * 4u + 2] =
                (uint8_t)(i < 5 ? i + 1 : 77);
            expected[i * 4u + 3] = 255;
        }
        expect(pos + 81, expected, 86, 1);
        assert(decode(pos + 80) == CODEC_TRUNCATED);
    }

    /* Tiles: 3x2 picture in 2x1 tiles, edge tiles stored full size, a null
       tile left black. */
    header(3, 2, 2, 1, 8, 2, 1024, 0);
    tile(0, 600, 2); tile(1, 602, 2); tile(2, 604, 2); tile(3, 0, 0);
    memcpy(data + 600, "\x01\x02\x03\x63\x04\x05\x06\x07", 8);
    {
        const uint8_t expected[24] = {
            1,1,1,255, 2,2,2,255, 3,3,3,255,
            4,4,4,255, 5,5,5,255, 0,0,0,255};
        expect(606, expected, 3, 2);
        assert(decode(605) == CODEC_TRUNCATED);
    }
    /* A length of -1 marks an incomplete tile; its data still loads. */
    tile(3, 606, 0xfffffffful);
    {
        const uint8_t expected[24] = {
            1,1,1,255, 2,2,2,255, 3,3,3,255,
            4,4,4,255, 5,5,5,255, 6,6,6,255};
        expect(608, expected, 3, 2);
    }
    /* A null tile in an alpha picture is transparent. */
    header(1, 2, 1, 1, 15, 2, 1024, 1);
    tile(0, 600, 4); tile(1, 0, 0);
    memcpy(data + 600, "\x01\x02\x03\x04", 4);
    {
        const uint8_t expected[8] = {1,2,3,4, 0,0,0,0};
        expect(604, expected, 1, 2);
    }
    /* A tile pointer past the end of the file. */
    tile(1, 604, 4);
    assert(decode(604) == CODEC_TRUNCATED);

    /* Header truncation and bad values. */
    header(3, 1, 3, 1, 14, 2, 1024, 0);
    tile(0, 1024, 9);
    memcpy(data + 1024, "\xff\0\0\0\xff\0\0\0\xff", 9);
    assert(decode(0) == CODEC_TRUNCATED);
    assert(decode(511) == CODEC_TRUNCATED);
    assert(decode(519) == CODEC_TRUNCATED);   /* Tile table. */
    assert(decode(1024) == CODEC_TRUNCATED);
    {
        struct pixar_image image;
        assert(pixar_decode(NULL, 0, &image) == CODEC_TRUNCATED);
        assert(pixar_decode(data, 1033, NULL) == CODEC_INVALID);
    }
    data[1] = 0xe9; assert(decode(1033) == CODEC_INVALID); data[1] = 0xe8;
    set_format(0); assert(decode(1033) == CODEC_INVALID);
    set_format(12); assert(decode(1033) == CODEC_INVALID);
    set_format(16 | 14); assert(decode(1033) == CODEC_INVALID);
    set_format(14);
    set_storage(1); assert(decode(1033) == CODEC_INVALID);
    set_storage(3); assert(decode(1033) == CODEC_INVALID);
    set_storage(4); assert(decode(1033) == CODEC_INVALID);
    set_storage(2);
    le16(data + 416, 0); assert(decode(1033) == CODEC_INVALID);
    le16(data + 416, 1); le16(data + 422, 0); assert(decode(1033) == CODEC_INVALID);
    le16(data + 422, 3);
    expect(1033, rgb, 3, 1);
    /* 16M pixels at most, in the picture and in a tile. */
    le16(data + 416, 4097); le16(data + 418, 4097);
    assert(decode(1033) == CODEC_TOO_LARGE);
    le16(data + 416, 1); le16(data + 418, 3);
    le16(data + 420, 65535); le16(data + 422, 65535);
    assert(decode(1033) == CODEC_TOO_LARGE);
    /* Many tiles need a table as large: 4096 x 4096 one-pixel tiles. */
    le16(data + 416, 4096); le16(data + 418, 4096);
    le16(data + 420, 1); le16(data + 422, 1);
    assert(decode(sizeof data) == CODEC_TRUNCATED);

    /* Writer: opaque RGB, composited over white when told there is no alpha. */
    assert(pixar_make_header(3, 1, 0, hdr));
    assert(hdr[0] == 0x80 && hdr[1] == 0xe8 && hdr[424] == 14 && hdr[426] == 2);
    assert(hdr[512] == 0 && hdr[513] == 4 && hdr[516] == 9);
    memcpy(data, hdr, sizeof hdr);
    assert(pixar_encode_row(rgb, 3, 0, data + 1024, 9) == 9);
    expect(1033, rgb, 3, 1);
    assert(pixar_encode_row(straight, 2, 0, out, sizeof out) == 6);
    assert(out[0] == 227 && out[1] == 177 && out[2] == 127);
    assert(out[3] == 255 && out[4] == 255 && out[5] == 255);
    assert(pixar_encode_row(rgb, 3, 0, out, 8) == 0);
    assert(pixar_encode_row(rgb, 0, 0, out, 8) == 0);
    /* RGBA is written straight, and loads back unchanged. */
    for (i = 0; i < sizeof big; i++)
        big[i] = (uint8_t)(i * 37u);
    assert(pixar_make_header(4, 5, 1, hdr));
    assert(hdr[424] == 15 && hdr[430] == 1 && hdr[516] == 80);
    memcpy(data, hdr, sizeof hdr);
    for (i = 0; i < 5; i++)
        assert(pixar_encode_row(big + i * 16u, 4, 1, data + 1024 + i * 16u, 16) == 16);
    expect(1024 + sizeof big, big, 4, 5);
    assert(!pixar_make_header(0, 1, 0, hdr));
    assert(!pixar_make_header(65536, 1, 0, hdr));
    assert(!pixar_make_header(4097, 4097, 0, hdr));

    puts("pixar: ok");
    return 0;
}
