#include "../formats/alias/decode.h"
#include "../formats/alias/encode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Build files byte by byte. */
static uint8_t file[65536];
static size_t used;

static void put(unsigned byte) { file[used++] = (uint8_t)byte; }
static void put16(unsigned value) { put((value >> 8) & 0xffu); put(value & 0xffu); }
static void set16(size_t at, unsigned value)
{
    file[at] = (uint8_t)(value >> 8);
    file[at + 1] = (uint8_t)value;
}
static void set32(size_t at, unsigned long value)
{
    set16(at, (unsigned)(value >> 16));
    set16(at + 2, (unsigned)(value & 0xffffu));
}

static void expect_at(const uint8_t *data, size_t length, unsigned index,
                      const uint8_t *pixels, unsigned width, unsigned height)
{
    struct alias_image image;
    assert(alias_decode(data, length, index, &image) == CODEC_OK);
    assert(image.width == width && image.height == height);
    assert(memcmp(image.rgba, pixels, (size_t)width * height * 4u) == 0);
    alias_free(&image);
}

static void expect(const uint8_t *pixels, unsigned width, unsigned height)
{
    expect_at(file, used, 0, pixels, width, height);
}

static void expect_result(const uint8_t *data, size_t length, unsigned index,
                          enum codec_result want)
{
    struct alias_image image;
    assert(alias_decode(data, length, index, &image) == want);
    assert(image.rgba == NULL);
}

static void expect_count(unsigned want)
{
    unsigned count = 0;
    assert(alias_count(file, used, &count) == CODEC_OK);
    assert(count == want);
}

/* ---- PIX ---- */

static void pix_header(unsigned width, unsigned height, unsigned bits)
{
    used = 0;
    put16(width); put16(height);
    put16(5); put16(7); /* offsets are ignored */
    put16(bits);
}

static void pix_run(unsigned count, unsigned r, unsigned g, unsigned b)
{
    put(count); put(b); put(g); put(r);
}

static void test_pix_rgb(void)
{
    static const uint8_t want[] = {
        1, 2, 3, 255,  1, 2, 3, 255,  9, 8, 7, 255,
        4, 5, 6, 255,  4, 5, 6, 255,  4, 5, 6, 255
    };
    pix_header(3, 2, 24);
    pix_run(2, 1, 2, 3);
    pix_run(1, 9, 8, 7);
    pix_run(3, 4, 5, 6);
    expect(want, 3, 2);
    expect_count(1);
    /* Only one image. */
    expect_result(file, used, 1, CODEC_INVALID);
    /* Bytes after the image are ignored. */
    put(0xaa);
    expect(want, 3, 2);
}

static void test_pix_gray(void)
{
    static const uint8_t want[] = {
        0x10, 0x10, 0x10, 255,  0x80, 0x80, 0x80, 255,
        0x80, 0x80, 0x80, 255,  0xff, 0xff, 0xff, 255
    };
    pix_header(2, 2, 8);
    put(1); put(0x10);
    put(2); put(0x80);
    put(1); put(0xff);
    expect(want, 2, 2);
}

static void test_pix_runs(void)
{
    static const uint8_t want[] = {
        1, 1, 1, 255,  1, 1, 1, 255,
        1, 1, 1, 255,  2, 2, 2, 255
    };
    /* A run carries on into the next row, a zero count is skipped, and a
       run past the last pixel is clamped. */
    pix_header(2, 2, 24);
    pix_run(3, 1, 1, 1);
    pix_run(0, 7, 7, 7);
    pix_run(200, 2, 2, 2);
    expect(want, 2, 2);
}

static void test_pix_malformed(void)
{
    size_t full;

    pix_header(2, 1, 24);
    pix_run(2, 1, 2, 3);
    full = used;
    /* Truncated in the header, inside a record, and before the last run. */
    expect_result(file, 4, 0, CODEC_TRUNCATED);
    expect_result(file, 9, 0, CODEC_TRUNCATED);
    expect_result(file, full - 1, 0, CODEC_TRUNCATED);
    expect_result(file, 10, 0, CODEC_TRUNCATED);
    pix_header(2, 1, 24);
    pix_run(1, 1, 2, 3);
    expect_result(file, used, 0, CODEC_TRUNCATED);
    /* Bad depth, empty sides. */
    pix_header(2, 1, 16);
    pix_run(2, 1, 2, 3);
    expect_result(file, used, 0, CODEC_INVALID);
    pix_header(0, 1, 24);
    pix_run(2, 1, 2, 3);
    expect_result(file, used, 0, CODEC_INVALID);
    pix_header(2, 0, 8);
    put(2); put(1);
    expect_result(file, used, 0, CODEC_INVALID);
    /* Over 16M pixels. */
    pix_header(65535, 65535, 24);
    pix_run(255, 1, 2, 3);
    expect_result(file, used, 0, CODEC_TOO_LARGE);
}

/* ---- RLA ---- */

struct rla_spec {
    int left, right, bottom, top;
    unsigned type, colours, mattes, aux, revision;
    unsigned bits, matte_type, matte_bits;
};

static const struct rla_spec rgb8 = {
    0, 0, 0, 0, 0, 3, 0, 0, 0xfffe, 8, 0, 0
};

/* Header at the current position, sized width x height, and an empty
   offset table. Returns the header's offset. */
static size_t rla_header(struct rla_spec s, unsigned width, unsigned height)
{
    size_t base = used;

    memset(file + used, 0, 740u + height * 4u);
    s.right = s.left + (int)width - 1;
    s.top = s.bottom + (int)height - 1;
    set16(base + 0, (unsigned)s.left & 0xffffu);
    set16(base + 2, (unsigned)s.right & 0xffffu);
    set16(base + 4, (unsigned)s.bottom & 0xffffu);
    set16(base + 6, (unsigned)s.top & 0xffffu);
    set16(base + 8, (unsigned)s.left & 0xffffu);
    set16(base + 10, (unsigned)s.right & 0xffffu);
    set16(base + 12, (unsigned)s.bottom & 0xffffu);
    set16(base + 14, (unsigned)s.top & 0xffffu);
    set16(base + 18, s.type);
    set16(base + 20, s.colours);
    set16(base + 22, s.mattes);
    set16(base + 24, s.aux);
    set16(base + 26, s.revision);
    set16(base + 658, s.bits);
    set16(base + 660, s.matte_type);
    set16(base + 662, s.matte_bits);
    used += 740u + height * 4u;
    return base;
}

/* Point scanline y (0 is the bottom) of the image at base here. */
static void rla_scanline(size_t base, unsigned y)
{
    set32(base + 740u + y * 4u, (unsigned long)used);
}

/* A channel record holding the given RLE bytes. */
static void rla_record(const uint8_t *bytes, unsigned count)
{
    unsigned i;
    put16(count);
    for (i = 0; i < count; i++)
        put(bytes[i]);
}

/* A record of width literal values. */
static void rla_literal(const uint8_t *values, unsigned width)
{
    unsigned i;
    put16(width + 1u);
    put(256u - width);
    for (i = 0; i < width; i++)
        put(values[i]);
}

static void test_rla_rgb(void)
{
    static const uint8_t want[] = {
        /* top row */
        10, 20, 30, 255,  11, 20, 31, 255,
        /* bottom row */
        1, 5, 9, 255,  2, 5, 9, 255
    };
    static const uint8_t bottom_r[] = { 1, 2 }, top_r[] = { 10, 11 };
    static const uint8_t run5[] = { 1, 5 }, run9[] = { 1, 9 };
    static const uint8_t run20[] = { 1, 20 }, top_b[] = { 30, 31 };
    size_t base;

    used = 0;
    base = rla_header(rgb8, 2, 2);
    rla_scanline(base, 0);
    rla_literal(bottom_r, 2);
    rla_record(run5, 2);
    rla_record(run9, 2);
    rla_scanline(base, 1);
    rla_literal(top_r, 2);
    rla_record(run20, 2);
    rla_literal(top_b, 2);
    expect(want, 2, 2);
    expect_count(1);
    expect_result(file, used, 1, CODEC_INVALID);

    /* Other revisions, and a negative active window. */
    set16(26, 0xfffd);
    expect(want, 2, 2);
    set16(26, 0);
    expect(want, 2, 2);
    set16(26, 0x1234);
    expect_result(file, used, 0, CODEC_INVALID);
    set16(26, 0xfffe);
    set16(8, 0xfff0); set16(10, 0xfff1);
    set16(12, 0x8000); set16(14, 0x8001);
    expect(want, 2, 2);
}

static void test_rla_gray_matte(void)
{
    /* Colour is stored multiplied by the matte. */
    static const uint8_t want[] = {
        200, 200, 200, 255,  255, 255, 255, 128,  33, 33, 33, 0
    };
    static const uint8_t gray[] = { 200, 128, 33 }, matte[] = { 255, 128, 0 };
    struct rla_spec s = rgb8;
    size_t base;

    s.colours = 1;
    s.mattes = 1;
    s.matte_bits = 8;
    used = 0;
    base = rla_header(s, 3, 1);
    rla_scanline(base, 0);
    rla_literal(gray, 3);
    rla_literal(matte, 3);
    expect(want, 3, 1);
    /* Zero matte bits mean 8. */
    set16(662, 0);
    expect(want, 3, 1);
}

static void test_rla_rgb_mattes_aux(void)
{
    static const uint8_t want[] = { 128, 64, 255, 128,  0, 0, 0, 0 };
    static const uint8_t r[] = { 64, 0 }, g[] = { 32, 0 }, b[] = { 200, 0 };
    static const uint8_t a[] = { 128, 0 }, other[] = { 1, 99 };
    struct rla_spec s = rgb8;
    size_t base;

    /* Two mattes and an aux channel: only the first matte is read. The blue
       value exceeds its matte and is clamped. */
    s.mattes = 2;
    s.aux = 1;
    s.matte_bits = 8;
    used = 0;
    base = rla_header(s, 2, 1);
    rla_scanline(base, 0);
    rla_literal(r, 2);
    rla_literal(g, 2);
    rla_literal(b, 2);
    rla_literal(a, 2);
    rla_record(other, 2);
    rla_record(other, 2);
    expect(want, 2, 1);
}

static void test_rla_deep(void)
{
    static const uint8_t want16[] = {
        0x13, 0x00, 0xff, 255,  0xab, 0x00, 0xff, 255
    };
    static const uint8_t want10[] = {
        0, 128, 255, 255,  255, 128, 0, 255
    };
    /* 16-bit: a span of high bytes, then one of low bytes. 0x12ff rounds to
       0x13, and 0x007f to 0. */
    static const uint8_t r16[] = { 0xfe, 0x12, 0xab, 0xfe, 0xff, 0x80 };
    static const uint8_t g16[] = { 1, 0, 1, 0x7f };
    static const uint8_t b16[] = { 1, 0xff, 1, 0xff };
    static const uint8_t r10[] = { 0xfe, 0x00, 0x03, 0xfe, 0x00, 0xff };
    static const uint8_t g10[] = { 1, 0x02, 1, 0x00 };
    static const uint8_t b10[] = { 0xfe, 0x03, 0x00, 0xfe, 0xff, 0x00 };
    static const uint8_t want4[] = { 0, 0, 0, 255,  255, 255, 255, 255 };
    static const uint8_t v4[] = { 0, 15 };
    struct rla_spec s = rgb8;
    size_t base;

    s.type = 1;
    s.bits = 16;
    used = 0;
    base = rla_header(s, 2, 1);
    rla_scanline(base, 0);
    rla_record(r16, sizeof r16);
    rla_record(g16, sizeof g16);
    rla_record(b16, sizeof b16);
    expect(want16, 2, 1);
    /* Byte storage with more than 8 bits is read as 16-bit. */
    set16(18, 0);
    expect(want16, 2, 1);

    /* 10 bits in 16-bit words: 0x200 is 128, 0x3ff is 255. */
    s.bits = 10;
    used = 0;
    base = rla_header(s, 2, 1);
    rla_scanline(base, 0);
    rla_record(r10, sizeof r10);
    rla_record(g10, sizeof g10);
    rla_record(b10, sizeof b10);
    expect(want10, 2, 1);

    /* 4 bits in bytes; 1-channel gray. */
    s = rgb8;
    s.bits = 4;
    s.colours = 1;
    used = 0;
    base = rla_header(s, 2, 1);
    rla_scanline(base, 0);
    rla_literal(v4, 2);
    expect(want4, 2, 1);
}

static void test_rla_clamp(void)
{
    static const uint8_t want[] = { 7, 7, 7, 255,  7, 7, 7, 255,  7, 7, 7, 255 };
    /* A run past the row, and literals past the row. */
    static const uint8_t run[] = { 100, 7 }, lit[] = { 0xfc, 7, 7, 7, 9 };
    struct rla_spec s = rgb8;
    size_t base;

    s.colours = 1;
    used = 0;
    base = rla_header(s, 3, 1);
    rla_scanline(base, 0);
    rla_record(run, 2);
    expect(want, 3, 1);
    used = 0;
    base = rla_header(s, 3, 1);
    rla_scanline(base, 0);
    rla_record(lit, sizeof lit);
    expect(want, 3, 1);
}

static void test_rla_chain(void)
{
    static const uint8_t first[] = { 1, 1, 1, 255 }, second[] = {
        2, 2, 2, 255,  3, 3, 3, 255
    };
    static const uint8_t one[] = { 0, 1 }, two[] = { 0xfe, 2, 3 };
    struct rla_spec s = rgb8;
    size_t a, b, c;

    s.colours = 1;
    used = 0;
    a = rla_header(s, 1, 1);
    rla_scanline(a, 0);
    rla_record(one, 2);
    b = rla_header(s, 2, 1);
    set32(a + 736, (unsigned long)b);
    rla_scanline(b, 0);
    rla_record(two, 3);
    expect_count(2);
    expect_at(file, used, 0, first, 1, 1);
    expect_at(file, used, 1, second, 2, 1);
    expect_result(file, used, 2, CODEC_INVALID);

    /* A third header pointing back at the first ends the chain. */
    c = rla_header(s, 1, 1);
    set32(b + 736, (unsigned long)c);
    rla_scanline(c, 0);
    rla_record(one, 2);
    set32(c + 736, 0);
    expect_count(3);
    set32(c + 736, (unsigned long)a);
    expect_count(3);
    set32(c + 736, (unsigned long)c);
    expect_count(3);
    /* A next offset past the end, or to a bad header, ends it too. */
    set32(b + 736, 0xfffffff0ul);
    expect_count(2);
    set32(b + 736, (unsigned long)c);
    set16(c + 20, 2);
    expect_count(2);
    expect_result(file, used, 2, CODEC_INVALID);
}

static void test_rla_malformed(void)
{
    static const uint8_t one[] = { 0, 1 }, short_run[] = { 0 };
    static const uint8_t white1[] = { 1, 1, 1, 255 };
    struct rla_spec s = rgb8;
    size_t base, full;

    s.colours = 1;
    used = 0;
    base = rla_header(s, 1, 2);
    rla_scanline(base, 0);
    rla_record(one, 2);
    rla_scanline(base, 1);
    rla_record(one, 2);
    full = used;
    /* Truncated in the header, the offset table, a record length, a
       record. */
    expect_result(file, 30, 0, CODEC_TRUNCATED);
    expect_result(file, 739, 0, CODEC_TRUNCATED);
    expect_result(file, 745, 0, CODEC_TRUNCATED);
    expect_result(file, full - 3, 0, CODEC_TRUNCATED);
    expect_result(file, full - 1, 0, CODEC_TRUNCATED);
    {
        unsigned count;
        assert(alias_count(file, 745, &count) == CODEC_TRUNCATED);
        assert(alias_count(file, 5, &count) == CODEC_TRUNCATED);
    }
    /* A scanline offset past the end. */
    set32(base + 744, 0x7ffffffful);
    expect_result(file, used, 0, CODEC_TRUNCATED);
    set32(base + 744, (unsigned long)(full - 4));

    /* A record too short for the row. */
    used = 0;
    base = rla_header(s, 2, 1);
    rla_scanline(base, 0);
    rla_record(short_run, 1);
    put(0);
    expect_result(file, used, 0, CODEC_INVALID);
    used = 0;
    base = rla_header(s, 2, 1);
    rla_scanline(base, 0);
    rla_record(one, 2);
    expect_result(file, used, 0, CODEC_INVALID);

    /* Reserved or unsupported header values. */
    used = 0;
    base = rla_header(s, 1, 1);
    rla_scanline(base, 0);
    rla_record(one, 2);
    expect(white1, 1, 1);
    set16(20, 2); expect_result(file, used, 0, CODEC_INVALID);
    set16(20, 0); expect_result(file, used, 0, CODEC_INVALID);
    set16(20, 4); expect_result(file, used, 0, CODEC_INVALID);
    set16(20, 1);
    set16(22, 4); expect_result(file, used, 0, CODEC_INVALID);
    set16(22, 0xffff); expect_result(file, used, 0, CODEC_INVALID);
    set16(22, 0);
    set16(18, 4); expect_result(file, used, 0, CODEC_INVALID); /* float */
    set16(18, 2); expect_result(file, used, 0, CODEC_INVALID); /* 32-bit */
    set16(18, 7); expect_result(file, used, 0, CODEC_INVALID);
    set16(18, 0);
    set16(658, 32); expect_result(file, used, 0, CODEC_INVALID);
    set16(658, 17); expect_result(file, used, 0, CODEC_INVALID);
    set16(658, 0xffff); expect_result(file, used, 0, CODEC_INVALID);
    set16(658, 8);
    set16(22, 1); set16(660, 4); expect_result(file, used, 0, CODEC_INVALID);
    set16(22, 0);
    /* Window with right before left, and too many pixels. */
    set16(10, 0xffff); expect_result(file, used, 0, CODEC_INVALID);
    set16(8, 0x8000); set16(10, 0x7fff);
    set16(12, 0x8000); set16(14, 0x7fff);
    expect_result(file, used, 0, CODEC_TOO_LARGE);
    set16(8, 0); set16(10, 0); set16(12, 0); set16(14, 0);
    expect(white1, 1, 1);
}

/* ---- Writer ---- */

static void encode(const uint8_t *rgba, unsigned width, unsigned height)
{
    unsigned y;
    int alpha = 0;
    size_t offset;

    for (y = 0; y < height; y++)
        alpha |= alias_row_has_alpha(rgba + (size_t)y * width * 4u, width);
    assert(alias_make_header(width, height, alpha, file));
    used = RLA_HEADER_SIZE + height * 4u;
    for (y = 0; y < height; y++) {
        offset = used;
        alias_put32(file + RLA_HEADER_SIZE + y * 4u, (unsigned long)offset);
        used += alias_encode_row(rgba + (size_t)(height - 1u - y) * width * 4u,
                                 width, alpha, file + used);
        assert(used - offset <= alias_row_capacity(width));
    }
}

static void test_write_opaque(void)
{
    enum { W = 300, H = 3 };
    static uint8_t rgba[W * H * 4];
    unsigned i;

    /* Long runs, long literal stretches and short mixes. */
    for (i = 0; i < W * H; i++) {
        rgba[i * 4] = (uint8_t)(i < 200 ? 5 : i * 7);
        rgba[i * 4 + 1] = (uint8_t)(i / 3);
        rgba[i * 4 + 2] = (uint8_t)((i % 5) < 2 ? 9 : i);
        rgba[i * 4 + 3] = 255;
    }
    encode(rgba, W, H);
    assert(file[23] == 0);  /* no matte */
    expect(rgba, W, H);
    expect_count(1);
}

static void test_write_alpha(void)
{
    static const uint8_t rgba[] = {
        10, 200, 255, 255,  255, 128, 0, 128,  0, 0, 0, 0,  50, 60, 70, 255
    };
    static const uint8_t want[] = {
        10, 200, 255, 255,  255, 128, 0, 128,  0, 0, 0, 0,  50, 60, 70, 255
    };
    encode(rgba, 2, 2);
    assert(file[23] == 1);
    expect(want, 2, 2);
}

static void test_write_composite(void)
{
    /* Without a matte, colour is composited over white. */
    static const uint8_t rgba[] = { 0, 0, 0, 128,  100, 0, 0, 0 };
    static const uint8_t want[] = { 127, 127, 127, 255,  255, 255, 255, 255 };
    uint8_t row[64];
    size_t size;

    assert(alias_make_header(2, 1, 0, file));
    alias_put32(file + RLA_HEADER_SIZE, RLA_HEADER_SIZE + 4u);
    size = alias_encode_row(rgba, 2, 0, row);
    memcpy(file + RLA_HEADER_SIZE + 4u, row, size);
    used = RLA_HEADER_SIZE + 4u + size;
    expect(want, 2, 1);
}

static void test_write_limits(void)
{
    static uint8_t header[RLA_HEADER_SIZE];
    assert(alias_make_header(32768, 1, 0, header));
    assert(!alias_make_header(32769, 1, 0, header));
    assert(!alias_make_header(1, 32769, 0, header));
    assert(!alias_make_header(0, 1, 0, header));
}

int main(void)
{
    test_pix_rgb();
    test_pix_gray();
    test_pix_runs();
    test_pix_malformed();
    test_rla_rgb();
    test_rla_gray_matte();
    test_rla_rgb_mattes_aux();
    test_rla_deep();
    test_rla_clamp();
    test_rla_chain();
    test_rla_malformed();
    test_write_opaque();
    test_write_alpha();
    test_write_composite();
    test_write_limits();
    puts("alias: ok");
    return 0;
}
