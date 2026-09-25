#include "../formats/spectrum/decode.h"
#include "../formats/spectrum/encode.h"
#include "common/atarist.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t spu[SPU_FILE_SIZE + 16];
static uint8_t spc[SPU_FILE_SIZE * 2];
static uint8_t out[SPU_FILE_SIZE];

static void put16(uint8_t *p, unsigned value)
{
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static void put32(uint8_t *p, unsigned long value)
{
    put16(p, (unsigned)(value >> 16));
    put16(p + 2, (unsigned)value);
}

/* Set colour index c at (x, y) of the SPU screen. */
static void set_pixel(uint8_t *screen, unsigned x, unsigned y, unsigned c)
{
    uint8_t *group = screen + y * 160u + (x / 16u) * 8u;
    unsigned bit = 15u - x % 16u, p;

    for (p = 0; p < 4; p++) {
        uint8_t mask = (uint8_t)(1u << bit % 8u);
        uint8_t *byte = group + p * 2u + (bit < 8u);
        *byte = (uint8_t)((c >> p & 1u) ? *byte | mask : *byte & ~mask);
    }
}

static void set_colour(uint8_t *file, unsigned y, unsigned slot, unsigned word)
{
    put16(file + SPU_SCREEN_SIZE + ((y - 1u) * 48u + slot) * 2u, word);
}

static const uint8_t *pixel(const struct spectrum_image *image, unsigned x, unsigned y)
{
    return image->rgba + ((size_t)y * image->width + x) * 4u;
}

static int is_rgb(const uint8_t *p, unsigned r, unsigned g, unsigned b)
{
    return p[0] == r && p[1] == g && p[2] == b && p[3] == 255;
}

/* A test picture: every index on every line, a palette that differs by
   line and slot. ST words only. */
static void make_spu(void)
{
    unsigned x, y, s;

    memset(spu, 0, sizeof spu);
    for (y = 1; y < SPECTRUM_HEIGHT; y++) {
        for (x = 0; x < SPECTRUM_WIDTH; x++)
            set_pixel(spu, x, y, (x / 3u + y) % 16u);
        for (s = 0; s < 48; s++)
            set_colour(spu, y, s, s % 16u == 15u ? 0 : ((y * 7u + s * 5u) * 37u) & 0x777u);
    }
}

/* SPC of the picture in spu: bitmap as literal runs (plus whatever
   extra is given), then the palettes, stored whole. */
static size_t make_spc(const uint8_t *extra, size_t extra_length)
{
    uint8_t plane_bytes[4u * 7960u];
    size_t n, pos = 12, bitmap;
    unsigned p, b;

    for (n = 0; n < sizeof plane_bytes; n++) {
        size_t plane = n / 7960u, k = n % 7960u;
        plane_bytes[n] = spu[160u + plane * 2u + (k / 2u) * 8u + (k & 1u)];
    }
    memcpy(spc, "SP\0\0", 4);
    for (n = 0; n < sizeof plane_bytes; n += 128) {
        size_t count = sizeof plane_bytes - n < 128 ? sizeof plane_bytes - n : 128;
        spc[pos++] = (uint8_t)(count - 1u);
        memcpy(spc + pos, plane_bytes + n, count);
        pos += count;
    }
    if (extra_length > 0)
        memcpy(spc + pos, extra, extra_length);
    pos += extra_length;
    bitmap = pos - 12u;
    for (p = 0; p < 199u * 3u; p++) {
        unsigned mask = 0;
        size_t at = pos;
        pos += 2;
        for (b = 0; b < 15; b++) {
            unsigned word = spu[SPU_SCREEN_SIZE + (p * 16u + b) * 2u] << 8 |
                            spu[SPU_SCREEN_SIZE + (p * 16u + b) * 2u + 1u];
            if (word != 0) {
                mask |= 1u << b;
                put16(spc + pos, word);
                pos += 2;
            }
        }
        put16(spc + at, mask);
    }
    put32(spc + 4, bitmap);
    put32(spc + 8, pos - 12u - bitmap);
    return pos;
}

static void same(const struct spectrum_image *a, const struct spectrum_image *b)
{
    assert(a->width == b->width && a->height == b->height);
    assert(memcmp(a->rgba, b->rgba, (size_t)a->width * a->height * 4u) == 0);
}

static void test_levels_and_slots(void)
{
    unsigned c, n;

    /* ST levels scale like netpbm's maxval 7; STE levels are 4-bit. */
    assert(st_level(0, 0) == 0 && st_level(1, 0) == 36);
    assert(st_level(3, 0) == 109 && st_level(7, 0) == 255);
    assert(st_level(8, 0) == 0 && st_level(0xf, 0) == 255);
    assert(st_level(0, 1) == 0 && st_level(8, 1) == 17);
    assert(st_level(1, 1) == 34 && st_level(0xf, 1) == 255);
    assert(st_level(7, 1) == 238);
    for (n = 0; n < 16; n++)
        assert(st_level(n | 0x70, 1) == st_level(n, 1));

    /* Even indexes change slot at 10c+1 and 10c+161, odd ones at 10c-5 and 10c+155. */
    for (c = 0; c < 16; c++) {
        unsigned x1 = (c & 1u) ? 10u * c - 5u : 10u * c + 1u;
        assert(spectrum_slot(c, 0) == c);
        assert(spectrum_slot(c, x1 - 1u) == c);
        assert(spectrum_slot(c, x1) == c + 16u);
        assert(spectrum_slot(c, x1 + 159u) == c + 16u);
        assert(spectrum_slot(c, x1 + 160u) == c + 32u);
        assert(spectrum_slot(c, 319) == c + 32u);
    }
    assert(spectrum_slot(0, 1) == 16 && spectrum_slot(1, 4) == 1 && spectrum_slot(1, 5) == 17);
    assert(spectrum_slot(15, 144) == 15 && spectrum_slot(15, 145) == 31 && spectrum_slot(15, 305) == 47);
}

static void test_spu(void)
{
    struct spectrum_image image, other;
    unsigned x;

    /* One line, by hand: index 2 changes slot at 21 and 181. */
    memset(spu, 0, sizeof spu);
    for (x = 0; x < SPECTRUM_WIDTH; x++)
        set_pixel(spu, x, 5, 2);
    set_pixel(spu, 3, 5, 1);
    set_colour(spu, 5, 2, 0x700);
    set_colour(spu, 5, 18, 0x070);
    set_colour(spu, 5, 34, 0x007);
    set_colour(spu, 5, 1, 0x123);
    /* Line 0 has pixels but no palette; the high nibble is ignored. */
    set_pixel(spu, 3, 0, 5);
    set_colour(spu, 4, 0, 0xf000);
    assert(spectrum_decode(spu, SPU_FILE_SIZE, &image) == CODEC_OK);
    assert(image.width == 320 && image.height == 200);
    for (x = 0; x < SPECTRUM_WIDTH; x++)
        assert(is_rgb(pixel(&image, x, 0), 0, 0, 0));
    assert(is_rgb(pixel(&image, 0, 5), 255, 0, 0));
    assert(is_rgb(pixel(&image, 20, 5), 255, 0, 0));
    assert(is_rgb(pixel(&image, 21, 5), 0, 255, 0));
    assert(is_rgb(pixel(&image, 180, 5), 0, 255, 0));
    assert(is_rgb(pixel(&image, 181, 5), 0, 0, 255));
    assert(is_rgb(pixel(&image, 319, 5), 0, 0, 255));
    assert(is_rgb(pixel(&image, 3, 5), 36, 73, 109));
    assert(is_rgb(pixel(&image, 0, 4), 0, 0, 0));
    assert(is_rgb(pixel(&image, 0, 199), 0, 0, 0));

    /* Bytes after the picture are ignored. */
    spu[SPU_FILE_SIZE] = 0x55;
    assert(spectrum_decode(spu, SPU_FILE_SIZE + 16, &other) == CODEC_OK);
    same(&image, &other);
    spectrum_free(&other);
    spectrum_free(&image);
    assert(image.rgba == NULL && image.width == 0);

    /* A fourth bit anywhere makes the whole palette STE. */
    set_colour(spu, 199, 47, 0x008);
    assert(spectrum_decode(spu, SPU_FILE_SIZE, &image) == CODEC_OK);
    assert(is_rgb(pixel(&image, 0, 5), 238, 0, 0));
    assert(is_rgb(pixel(&image, 3, 5), 34, 68, 102));
    spectrum_free(&image);
    set_colour(spu, 5, 1, 0x9ab);
    assert(spectrum_decode(spu, SPU_FILE_SIZE, &image) == CODEC_OK);
    assert(is_rgb(pixel(&image, 3, 5), 51, 85, 119));
    spectrum_free(&image);

    /* Truncation, and the enhanced 5BIT variant. */
    assert(spectrum_decode(spu, SPU_FILE_SIZE - 1u, &image) == CODEC_TRUNCATED);
    assert(image.rgba == NULL && image.width == 0);
    assert(spectrum_decode(spu, 3, &image) == CODEC_TRUNCATED);
    assert(spectrum_decode(NULL, 0, &image) == CODEC_TRUNCATED);
    assert(spectrum_decode(spu, SPU_FILE_SIZE, NULL) == CODEC_INVALID);
    memcpy(spu, "5BIT", 4);
    assert(spectrum_decode(spu, SPU_FILE_SIZE, &image) == CODEC_INVALID);

    /* An SPU whose unused line 0 starts like an SPC still loads. */
    memcpy(spu, "SP\0\0", 4);
    assert(spectrum_decode(spu, SPU_FILE_SIZE, &image) == CODEC_OK);
    assert(is_rgb(pixel(&image, 0, 5), 238, 0, 0));
    spectrum_free(&image);
}

static void test_spc(void)
{
    static const uint8_t overrun[] = { 0x80, 0x11 };
    struct spectrum_image image, expect;
    size_t length, bitmap, n;
    unsigned x, y;

    make_spu();
    assert(spectrum_decode(spu, SPU_FILE_SIZE, &expect) == CODEC_OK);
    length = make_spc(NULL, 0);
    assert(spectrum_decode(spc, length, &image) == CODEC_OK);
    same(&image, &expect);
    spectrum_free(&image);

    /* Trailing bytes are ignored, and the palette length isn't checked. */
    put32(spc + 8, 3);
    assert(spectrum_decode(spc, length + 5, &image) == CODEC_OK);
    same(&image, &expect);
    spectrum_free(&image);

    /* A run past the last plane is cut off. */
    length = make_spc(overrun, sizeof overrun);
    assert(spectrum_decode(spc, length, &image) == CODEC_OK);
    same(&image, &expect);
    spectrum_free(&image);
    spectrum_free(&expect);

    /* Repeat runs, crossing from plane 0 into plane 1: a solid picture. */
    memset(spc, 0, sizeof spc);
    memcpy(spc, "SP\0\0", 4);
    n = 12;
    for (x = 0; x < 4u * 7960u;) {
        unsigned count = 4u * 7960u - x < 130u ? 4u * 7960u - x : 130u;
        if (count < 3) {
            spc[n++] = (uint8_t)(count - 1u);
            spc[n++] = x < 7960u ? 0xff : 0x00;
            if (count == 2)
                spc[n++] = 0x00;
        } else {
            spc[n++] = (uint8_t)(258u - count);
            spc[n++] = x < 7960u ? 0xff : 0x00;
        }
        /* Split the run that reaches the plane boundary across it. */
        x += count;
        if (x > 7960u && x - count < 7960u)
            spc[n - 1] = 0xff;
    }
    bitmap = n - 12u;
    put32(spc + 4, bitmap);
    /* Palettes: colour 1 in the first slot bank; mask bit 15 is ignored. */
    for (y = 0; y < 597u; y++) {
        put16(spc + n, 0x8002);
        put16(spc + n + 2, y % 3u == 0 ? 0x357 : 0x000);
        n += 4;
    }
    length = n;
    assert(spectrum_decode(spc, length, &image) == CODEC_OK);
    /* Plane 0 is all set (the run crossing into plane 1 only touches
       line 1), so index 1 shows: slot 1 before x=5, then black. */
    assert(is_rgb(pixel(&image, 0, 150), 109, 182, 255));
    assert(is_rgb(pixel(&image, 4, 150), 109, 182, 255));
    assert(is_rgb(pixel(&image, 5, 150), 0, 0, 0));
    for (y = 1; y < 200; y++)
        assert(is_rgb(pixel(&image, 319, y), 0, 0, 0));
    spectrum_free(&image);

    /* Colour 15 is black even when the mask's bit 15 is set. */
    make_spu();
    for (y = 1; y < 200; y++)
        for (x = 0; x < 320; x++)
            set_pixel(spu, x, y, 15);
    length = make_spc(NULL, 0);
    assert(spectrum_decode(spc, length, &image) == CODEC_OK);
    assert(is_rgb(pixel(&image, 100, 100), 0, 0, 0));
    spectrum_free(&image);

    /* Truncation: header, bitmap length, bitmap data, each palette part. */
    make_spu();
    length = make_spc(NULL, 0);
    bitmap = (spc[4] << 24) | (spc[5] << 16) | (spc[6] << 8) | spc[7];
    assert(spectrum_decode(spc, 11, &image) == CODEC_TRUNCATED);
    assert(spectrum_decode(spc, 12 + bitmap - 1u, &image) == CODEC_TRUNCATED);
    assert(spectrum_decode(spc, 12 + bitmap + 1u, &image) == CODEC_TRUNCATED);
    assert(spectrum_decode(spc, length - 1u, &image) == CODEC_TRUNCATED);
    assert(image.rgba == NULL);
    put32(spc + 4, 0xffffffffUL);
    assert(spectrum_decode(spc, length, &image) == CODEC_TRUNCATED);
    put32(spc + 4, bitmap - 1u);
    assert(spectrum_decode(spc, length, &image) == CODEC_TRUNCATED);
    put32(spc + 4, 0);
    assert(spectrum_decode(spc, length, &image) == CODEC_TRUNCATED);
    /* A literal run that reaches past the bitmap. */
    put32(spc + 4, 1);
    spc[12] = 5;
    assert(spectrum_decode(spc, length, &image) == CODEC_TRUNCATED);
    /* A repeat run missing its byte. */
    spc[12] = 0x80;
    assert(spectrum_decode(spc, length, &image) == CODEC_TRUNCATED);
}

static void test_encode(void)
{
    static uint8_t rgba[320u * 200u * 4u];
    struct spectrum_image image, again;
    unsigned x, y;

    /* A picture with a different palette on every line saves back exactly. */
    make_spu();
    for (y = 1; y < SPECTRUM_HEIGHT; y++)
        for (x = 0; x < SPECTRUM_WIDTH; x++)
            set_pixel(spu, x, y, (x / 20u + y) % 16u);
    assert(spectrum_decode(spu, SPU_FILE_SIZE, &image) == CODEC_OK);
    assert(spectrum_encode(image.rgba, 320, 200, out) == CODEC_OK);
    assert(spectrum_decode(out, SPU_FILE_SIZE, &again) == CODEC_OK);
    same(&image, &again);
    spectrum_free(&again);

    /* Size, and line 0, which has no palette. */
    assert(spectrum_encode(image.rgba, 320, 199, out) == CODEC_INVALID);
    assert(spectrum_encode(image.rgba, 200, 320, out) == CODEC_INVALID);
    assert(spectrum_encode(NULL, 320, 200, out) == CODEC_INVALID);
    memcpy(rgba, image.rgba, sizeof rgba);
    rgba[4] = 36;
    assert(spectrum_encode(rgba, 320, 200, out) == CODEC_INVALID);
    spectrum_free(&image);

    /* A colour that's neither an ST nor an STE level. */
    memset(rgba, 0, sizeof rgba);
    for (x = 0; x < sizeof rgba; x += 4)
        rgba[x + 3] = 255;
    rgba[320u * 4u * 10u] = 100;
    assert(spectrum_encode(rgba, 320, 200, out) == CODEC_INVALID);

    /* Transparent pixels go over white; ST levels give an ST palette. */
    rgba[320u * 4u * 10u] = 0;
    rgba[320u * 4u * 10u + 3u] = 0;
    assert(spectrum_encode(rgba, 320, 200, out) == CODEC_OK);
    assert(spectrum_decode(out, SPU_FILE_SIZE, &image) == CODEC_OK);
    assert(is_rgb(pixel(&image, 0, 10), 255, 255, 255));
    assert(is_rgb(pixel(&image, 1, 10), 0, 0, 0));
    spectrum_free(&image);

    /* STE levels with no fourth bit get a marker in a spare slot. */
    rgba[320u * 4u * 10u + 3u] = 255;
    rgba[320u * 4u * 10u] = 34;
    assert(spectrum_encode(rgba, 320, 200, out) == CODEC_OK);
    assert(spectrum_decode(out, SPU_FILE_SIZE, &image) == CODEC_OK);
    assert(is_rgb(pixel(&image, 0, 10), 34, 0, 0));
    assert(is_rgb(pixel(&image, 1, 10), 0, 0, 0));
    spectrum_free(&image);
    rgba[320u * 4u * 10u] = 17;
    assert(spectrum_encode(rgba, 320, 200, out) == CODEC_OK);
    assert(spectrum_decode(out, SPU_FILE_SIZE, &image) == CODEC_OK);
    assert(is_rgb(pixel(&image, 0, 10), 17, 0, 0));
    spectrum_free(&image);

    /* 17 colours in one segment, or 49 in a line, don't fit. */
    for (x = 0; x < 17; x++) {
        rgba[(320u * 20u + 30u + x) * 4u] = st_level(x % 8u, 0);
        rgba[(320u * 20u + 30u + x) * 4u + 1u] = st_level(x / 8u, 0);
    }
    assert(spectrum_encode(rgba, 320, 200, out) == CODEC_INVALID);
    for (x = 0; x < 320; x++) {
        unsigned w = (x / 6u) % 49u;
        rgba[(320u * 20u + x) * 4u] = st_level(w % 8u, 0);
        rgba[(320u * 20u + x) * 4u + 1u] = st_level(w / 8u, 0);
    }
    assert(spectrum_encode(rgba, 320, 200, out) == CODEC_INVALID);

    /* 48 colours a line, each wanted across a stretch: fits. */
    for (y = 1; y < 200; y++)
        for (x = 0; x < 320; x++) {
            unsigned w = (x / 7u + y) % 48u;
            uint8_t *p = rgba + ((size_t)y * 320u + x) * 4u;
            p[0] = st_level(w % 8u, 0);
            p[1] = st_level(w / 8u, 0);
            p[2] = st_level(y % 8u, 0);
            p[3] = 255;
        }
    assert(spectrum_encode(rgba, 320, 200, out) == CODEC_OK);
    assert(spectrum_decode(out, SPU_FILE_SIZE, &image) == CODEC_OK);
    assert(memcmp(image.rgba, rgba, sizeof rgba) == 0);
    spectrum_free(&image);
}

int main(void)
{
    test_levels_and_slots();
    test_spu();
    test_spc();
    test_encode();
    puts("spectrum codec tests passed");
    return 0;
}
