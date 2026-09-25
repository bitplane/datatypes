#include "../formats/wal/decode.h"
#include "../formats/wal/encode.h"
#include "../formats/wal/palette.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t file[4096];
static size_t length;

static void put32(size_t at, uint32_t v)
{
    file[at] = (uint8_t)v; file[at + 1] = (uint8_t)(v >> 8);
    file[at + 2] = (uint8_t)(v >> 16); file[at + 3] = (uint8_t)(v >> 24);
}

/* A WAL laid out as the tools write it; level L pixel i is index seed + L * 64 + i. */
static void make(unsigned width, unsigned height, unsigned seed)
{
    size_t offset = WAL_HEADER, i;
    unsigned level;

    memset(file, 0, sizeof file);
    memcpy(file, "e1u1/floor1_1", 13);
    put32(32, width);
    put32(36, height);
    for (level = 0; level < WAL_LEVELS; level++) {
        size_t pixels = (size_t)(width >> level) * (height >> level);
        put32(40 + level * 4u, (uint32_t)offset);
        for (i = 0; i < pixels; i++)
            file[offset + i] = (uint8_t)(seed + level * 64u + i);
        offset += pixels;
    }
    memcpy(file + 56, "e1u1/floor1_2", 13);
    put32(88, 0x10);
    put32(92, 1);
    put32(96, 0);
    length = offset;
}

static void expect_rgb(const uint8_t *p, unsigned index)
{
    const uint8_t *rgb = wal_palette + index * 3u;
    assert(p[0] == rgb[0] && p[1] == rgb[1] && p[2] == rgb[2] && p[3] == 255);
}

static void expect_level(unsigned level, unsigned width, unsigned height, unsigned seed)
{
    struct wal_image image;
    unsigned i;

    assert(wal_decode(file, length, level, &image) == CODEC_OK);
    assert(image.width == width && image.height == height);
    for (i = 0; i < width * height; i++)
        expect_rgb(image.rgba + i * 4u, (seed + level * 64u + i) & 255u);
    wal_free(&image);
}

static void expect_fail(unsigned level, enum codec_result want)
{
    struct wal_image image;
    assert(wal_decode(file, length, level, &image) == want);
    assert(image.rgba == NULL && image.width == 0 && image.height == 0);
}

static void palette(void)
{
    /* Spot checks against colormap.pcx. */
    assert(wal_palette[0] == 0 && wal_palette[1] == 0 && wal_palette[2] == 0);
    assert(wal_palette[15 * 3] == 235 && wal_palette[15 * 3 + 2] == 235);
    assert(wal_palette[16 * 3] == 99 && wal_palette[16 * 3 + 1] == 75 &&
           wal_palette[16 * 3 + 2] == 35);
    assert(wal_palette[255 * 3] == 159 && wal_palette[255 * 3 + 1] == 91 &&
           wal_palette[255 * 3 + 2] == 83);
}

static void decoding(void)
{
    size_t n;

    /* Every mip level, largest first; index 255 stays opaque. */
    make(16, 8, 200);
    assert(wal_count(file, length) == 4);
    expect_level(0, 16, 8, 200);
    expect_level(1, 8, 4, 200);
    expect_level(2, 4, 2, 200);
    expect_level(3, 2, 1, 200);
    expect_fail(4, CODEC_INVALID);
    expect_fail(0xffffffffu, CODEC_INVALID);

    /* Truncated in the header, then in each level. */
    for (n = 0; n < WAL_HEADER; n++) {
        struct wal_image image;
        assert(wal_count(file, n) == 0);
        assert(wal_decode(file, n, 0, &image) == CODEC_TRUNCATED);
        assert(image.rgba == NULL);
    }
    {
        size_t saved = length;
        length = WAL_HEADER + 127;
        expect_fail(0, CODEC_TRUNCATED);
        length = WAL_HEADER + 128;
        expect_level(0, 16, 8, 200);
        expect_fail(1, CODEC_TRUNCATED);
        length = saved - 1;
        expect_level(2, 4, 2, 200);
        expect_fail(3, CODEC_TRUNCATED);
        length = saved;
    }

    /* Sides under 8 have fewer mips: 4>>3 is empty. */
    make(4, 12, 0);
    assert(wal_count(file, length) == 3);
    expect_level(2, 1, 3, 0);
    expect_fail(3, CODEC_INVALID);
    make(5, 3, 0);
    assert(wal_count(file, length) == 2);
    expect_level(1, 2, 1, 0);
    make(1, 1, 7);
    assert(wal_count(file, length) == 1);
    expect_level(0, 1, 1, 7);

    /* A zero mip offset ends the levels. */
    make(16, 16, 0);
    put32(48, 0);
    assert(wal_count(file, length) == 2);
    expect_level(1, 8, 8, 0);
    expect_fail(2, CODEC_INVALID);
    put32(44, 0);
    assert(wal_count(file, length) == 1);

    /* Offsets may point anywhere in the file, even into the header. */
    make(4, 4, 0);
    put32(40, 0);
    {
        struct wal_image image;
        assert(wal_decode(file, length, 0, &image) == CODEC_OK);
        expect_rgb(image.rgba, 'e');
        wal_free(&image);
    }
    put32(40, 0xffffffffu);
    expect_fail(0, CODEC_TRUNCATED);
    put32(40, (uint32_t)length - 15);
    expect_fail(0, CODEC_TRUNCATED);
    put32(40, (uint32_t)length - 16);
    assert(wal_count(file, length) == 3);

    /* Bad sizes. */
    make(4, 4, 0);
    put32(32, 0);
    expect_fail(0, CODEC_INVALID);
    assert(wal_count(file, length) == 0);
    put32(32, 4); put32(36, 0);
    expect_fail(0, CODEC_INVALID);
    put32(32, 65536); put32(36, 1);
    expect_fail(0, CODEC_TOO_LARGE);
    put32(32, 1); put32(36, 65536);
    expect_fail(0, CODEC_TOO_LARGE);
    put32(32, 4096); put32(36, 4097);
    expect_fail(0, CODEC_TOO_LARGE);
    put32(32, 0xffffffffu); put32(36, 0xffffffffu);
    expect_fail(0, CODEC_TOO_LARGE);
    /* 16M pixels passes the limit, then runs out of file. */
    put32(32, 4096); put32(36, 4096);
    assert(wal_count(file, length) == 4);
    expect_fail(0, CODEC_TRUNCATED);
    put32(32, 65535); put32(36, 256);
    expect_fail(0, CODEC_TRUNCATED);

    /* NULL input. */
    assert(wal_count(NULL, 100) == 0);
    {
        struct wal_image image;
        assert(wal_decode(NULL, 100, 0, &image) == CODEC_TRUNCATED);
        assert(wal_decode(file, length, 0, NULL) == CODEC_INVALID);
    }
}

static const uint8_t *encode(unsigned width, unsigned height, const uint8_t *rgba,
                             struct wal_encoder **encoder, size_t *size)
{
    unsigned y;
    *encoder = wal_encoder_new(width, height);
    assert(*encoder != NULL);
    for (y = 0; y < height; y++)
        assert(wal_encoder_row(*encoder, rgba + (size_t)y * width * 4u));
    return wal_encoder_finish(*encoder, size);
}

static void set_index(uint8_t *p, unsigned index)
{
    memcpy(p, wal_palette + index * 3u, 3);
    p[3] = 255;
}

static void encoding(void)
{
    static uint8_t rgba[16 * 16 * 4];
    struct wal_encoder *e;
    struct wal_image image;
    const uint8_t *out;
    size_t size;
    unsigned i, j;

    /* Every palette colour round-trips; the header matches the tools'. */
    for (i = 0; i < 256; i++)
        set_index(rgba + i * 4u, i);
    out = encode(16, 16, rgba, &e, &size);
    assert(out != NULL && size == WAL_HEADER + 256 + 64 + 16 + 4);
    assert(out[32] == 16 && out[36] == 16);
    assert(out[40] == 100 && out[41] == 0);          /* 100 */
    assert(out[44] == 100 && out[45] == 1);          /* 356 */
    assert(out[48] == 164 && out[49] == 1);          /* 420 */
    assert(out[52] == 180 && out[53] == 1);          /* 436 */
    for (i = 0; i < 32; i++)
        assert(out[i] == 0 && out[56 + i] == 0);
    assert(wal_count(out, size) == 4);
    assert(wal_decode(out, size, 0, &image) == CODEC_OK);
    assert(memcmp(image.rgba, rgba, sizeof rgba) == 0);
    wal_free(&image);
    /* Mips come from the palette too, never 255. */
    for (j = 1; j < 4; j++) {
        assert(wal_decode(out, size, j, &image) == CODEC_OK);
        assert(image.width == 16u >> j);
        wal_free(&image);
    }
    for (i = WAL_HEADER + 256; i < size; i++)
        assert(out[i] != 255);
    wal_encoder_free(e);

    /* A flat colour keeps its colour in every mip. */
    for (i = 0; i < 255; i++) {
        for (j = 0; j < 64; j++)
            set_index(rgba + j * 4u, i);
        out = encode(8, 8, rgba, &e, &size);
        for (j = 1; j < 4; j++) {
            assert(wal_decode(out, size, j, &image) == CODEC_OK);
            assert(memcmp(image.rgba, rgba, 4) == 0);
            wal_free(&image);
        }
        wal_encoder_free(e);
    }

    /* Odd sizes: 5x3 gives mips 2x1, then nothing. */
    for (j = 0; j < 15; j++)
        set_index(rgba + j * 4u, j * 7u);
    out = encode(5, 3, rgba, &e, &size);
    assert(size == WAL_HEADER + 15 + 2);
    assert(wal_count(out, size) == 2);
    assert(wal_decode(out, size, 0, &image) == CODEC_OK);
    assert(memcmp(image.rgba, rgba, 15 * 4) == 0);
    wal_free(&image);
    wal_encoder_free(e);

    /* Alpha composites over white, which is index 215. */
    set_index(rgba, 0);
    rgba[3] = 0;
    set_index(rgba + 4, 5);
    out = encode(2, 1, rgba, &e, &size);
    assert(out[WAL_HEADER] == 215 && out[WAL_HEADER + 1] == 5);
    assert(!wal_encoder_row(e, rgba));
    wal_encoder_free(e);

    /* Colours outside the palette fail. */
    e = wal_encoder_new(2, 1);
    set_index(rgba, 3);
    rgba[4] = 1; rgba[5] = 2; rgba[6] = 3; rgba[7] = 255;
    assert(!wal_encoder_row(e, rgba));
    assert(wal_encoder_finish(e, &size) == NULL);
    wal_encoder_free(e);

    /* Missing rows. */
    e = wal_encoder_new(1, 2);
    assert(wal_encoder_row(e, rgba));
    assert(wal_encoder_finish(e, &size) == NULL);
    wal_encoder_free(e);

    /* Sizes. */
    assert(wal_encoder_new(0, 1) == NULL);
    assert(wal_encoder_new(1, 0) == NULL);
    assert(wal_encoder_new(65536, 1) == NULL);
    assert(wal_encoder_new(4097, 4096) == NULL);
    assert(wal_encoder_row(NULL, rgba) == 0);
    assert(wal_encoder_finish(NULL, &size) == NULL);
    wal_encoder_free(NULL);
}

int main(void)
{
    palette();
    decoding();
    encoding();
    puts("wal: ok");
    return 0;
}
