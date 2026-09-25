#include "../formats/stscreen/decode.h"
#include "../formats/stscreen/encode.h"
#include "../common/atarist.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t data[300000];
static uint8_t bits[64000];
static uint8_t saved[STSCREEN_MAX_OUTPUT];
static uint8_t rgba[640 * 800 * 4];
static struct stscreen_image image;
static const uint8_t st[8] = { 0, 36, 73, 109, 146, 182, 219, 255 };

static void put16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

/* Colour index of (x, y) in the test pattern. */
static unsigned pattern(unsigned x, unsigned y, unsigned colours)
{
    return (x * 7u + y * 3u + x / 16u) % colours;
}

/* Draw the test pattern into interleaved bitplanes. */
static void draw(uint8_t *out, unsigned width, unsigned height, unsigned planes,
                 size_t stride)
{
    unsigned x, y, p;

    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++) {
            uint8_t *group = out + y * stride + (x / 16u) * planes * 2u;
            unsigned bit = 15u - x % 16u, index = pattern(x, y, 1u << planes);
            for (p = 0; p < planes; p++) {
                uint8_t *b = group + p * 2u + (bit < 8u);
                *b = (uint8_t)(*b & ~(1u << bit % 8u));
                if (index >> p & 1u)
                    *b |= (uint8_t)(1u << bit % 8u);
            }
        }
}

/* A palette of count ST colours whose entry i is distinct. */
static void palette(uint8_t *p, unsigned count)
{
    unsigned i;

    for (i = 0; i < count; i++)
        put16(p + i * 2u, (i & 7u) << 8 | (7u - (i & 7u)) << 4 | (i >> 1));
}

static void expect(unsigned x, unsigned y, unsigned r, unsigned g, unsigned b)
{
    const uint8_t *p = image.rgba + ((size_t)y * image.width + x) * 4u;
    assert(p[0] == r && p[1] == g && p[2] == b && p[3] == 255);
}

/* The decoded image is the test pattern in palette()'s colours, or black on
   white when there are two colours. */
static void verify(unsigned width, unsigned height, unsigned colours)
{
    unsigned x, y;

    assert(image.width == width && image.height == height);
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++) {
            unsigned i = pattern(x, y, colours);
            if (colours == 2)
                expect(x, y, i ? 0 : 255, i ? 0 : 255, i ? 0 : 255);
            else
                expect(x, y, st[i & 7u], st[7u - (i & 7u)], st[i >> 1]);
        }
}

static void decodes(const char *name, size_t length, unsigned width,
                    unsigned height, unsigned colours)
{
    assert(stscreen_decode(data, length, name, &image) == CODEC_OK);
    verify(width, height, colours);
    stscreen_free(&image);
}

static void fails(const char *name, size_t length, enum codec_result result)
{
    assert(stscreen_decode(data, length, name, &image) == result);
    assert(image.rgba == NULL && image.width == 0 && image.height == 0);
}

/* Every prefix shorter than length is truncated; step thins out long runs. */
static void truncations(const char *name, size_t length, size_t step)
{
    size_t n;

    for (n = 0; n < length; n += (n < 400 || length - n < 400) ? 1 : step)
        fails(name, n, CODEC_TRUNCATED);
}

/* Paintworks runs: b < 128 repeats the next byte b times, b >= 128 is b - 128
   literals. The stream holds each plane's words in turn. */
static size_t sc_pack(const uint8_t *in, size_t size, unsigned unit, uint8_t *out)
{
    static uint8_t seq[64000];
    size_t n = 0, i = 0, o = 0, at;
    unsigned plane;

    for (plane = 0; plane < unit; plane += 2)
        for (at = plane; at < size; at += unit) {
            seq[n++] = in[at];
            seq[n++] = in[at + 1];
        }
    /* A zero-length run is legal and skipped. */
    out[o++] = 0;
    out[o++] = 0x55;
    while (i < n) {
        size_t run = 1;
        while (i + run < n && run < 127 && seq[i + run] == seq[i])
            run++;
        if (run >= 3) {
            out[o++] = (uint8_t)run;
            out[o++] = seq[i];
            i += run;
        } else {
            size_t lit = n - i < 127 ? n - i : 127;
            out[o++] = (uint8_t)(128u + lit);
            memcpy(out + o, seq + i, lit);
            o += lit;
            i += lit;
        }
    }
    return o;
}

/* PackBits: b < 128 is b + 1 literals, b >= 128 repeats the next 257 - b times. */
static size_t packbits(const uint8_t *in, size_t n, uint8_t *out)
{
    size_t i = 0, o = 0;

    while (i < n) {
        size_t run = 1;
        while (i + run < n && run < 128 && in[i + run] == in[i])
            run++;
        if (run >= 2) {
            out[o++] = (uint8_t)(257u - run);
            out[o++] = in[i];
            i += run;
        } else {
            size_t lit = 1;
            while (i + lit < n && lit < 128 &&
                   !(i + lit + 1 < n && in[i + lit] == in[i + lit + 1]))
                lit++;
            out[o++] = (uint8_t)(lit - 1u);
            memcpy(out + o, in + i, lit);
            o += lit;
            i += lit;
        }
    }
    return o;
}

/* A Paintworks header with the given flags. */
static void paintworks_header(unsigned flags)
{
    memset(data, 0, 128);
    put16(data + 2, flags >> 4 & 3u);
    palette(data + 4, 16);
    memcpy(data + 36, "        .   ", 12);
    memcpy(data + 0x36, "ANvisionA", 9);
    data[0x3f] = (uint8_t)flags;
}

static void test_levels(void)
{
    unsigned i;

    for (i = 0; i < 8; i++) {
        assert(st_level(i, 0) == st[i]);
        assert(st_level(i | 8u, 0) == st[i]);
        assert(st_level(i, 1) == i * 34u);
        assert(st_level(i | 8u, 1) == i * 34u + 17u);
    }
}

static void test_art(void)
{
    /* Art Director: the bitmap, eight palettes and the chosen index. */
    memset(data, 0, 32512);
    draw(data, 320, 200, 4, 160);
    memset(data + 32000, 0xff, 32 * 8);
    palette(data + 32000 + 3 * 32, 16);
    data[0x7e1f] = 3;
    decodes("pic.art", 32512, 320, 200, 16);
    /* An index out of range falls back to the first palette. */
    data[0x7e1f] = 8;
    palette(data + 32000, 16);
    decodes("PIC.ART", 32512, 320, 200, 16);

    /* GFA Artist: the palette, then the bitmap. */
    palette(data, 16);
    draw(data + 32, 320, 200, 4, 160);
    decodes("gfa.art", 32032, 320, 200, 16);
    fails("gfa.art", 32033, CODEC_INVALID);
    fails("gfa.art", 32511, CODEC_INVALID);
    fails("gfa.art", 32513, CODEC_INVALID);

    /* MonoSTar: a bare high resolution bitmap. */
    draw(data, 640, 400, 1, 80);
    decodes("mono.art", 32000, 640, 400, 2);
    fails("mono.art", 32001, CODEC_INVALID);
    truncations("mono.art", 32000, 1);
}

static void test_doodle(void)
{
    memset(data, 0xaa, 33000);
    draw(data, 640, 400, 1, 80);
    decodes("pic.doo", 32000, 640, 400, 2);
    /* Trailing bytes are ignored. */
    decodes("pic.doo", 33000, 640, 400, 2);
    truncations("pic.doo", 32000, 1);

    /* PaintShop DA4: a bitmap twice as tall. */
    draw(data, 640, 800, 1, 80);
    decodes("pic.da4", 64000, 640, 800, 2);
    truncations("pic.da4", 64000, 1);
}

static void test_colorstar(void)
{
    palette(data, 16);
    draw(data + 32, 320, 200, 4, 160);
    decodes("pic.bil", 32032, 320, 200, 16);

    /* Low resolution DEGAS; other resolutions are not ColorSTar's. */
    put16(data, 0);
    palette(data + 2, 16);
    draw(data + 34, 320, 200, 4, 160);
    decodes("pic.bil", 32034, 320, 200, 16);
    put16(data, 1);
    fails("pic.bil", 32034, CODEC_INVALID);
    fails("pic.bil", 32033, CODEC_INVALID);
    fails("pic.bil", 32035, CODEC_INVALID);
    truncations("pic.bil", 32032, 1);
}

static void test_sinbad_and_synthetic(void)
{
    draw(data, 320, 200, 4, 160);
    palette(data + 32000, 16);
    decodes("pic.ssb", 32768, 320, 200, 16);
    truncations("pic.ssb", 32768, 1);

    /* Synthetic Arts: medium resolution, "JHSy", 1, then four colours. */
    draw(data, 640, 200, 2, 160);
    memcpy(data + 32000, "JHSy\0\1", 6);
    palette(data + 32006, 4);
    decodes("pic.srt", 32038, 640, 200, 4);
    truncations("pic.srt", 32038, 1);
    data[32005] = 2;
    fails("pic.srt", 32038, CODEC_INVALID);
    data[32005] = 1;
    data[32000] = 'j';
    fails("pic.srt", 32038, CODEC_INVALID);
}

static void test_construction_kit(void)
{
    memset(data, 0, 63054);
    data[0] = 'K';
    data[1] = 'D';
    palette(data + 2, 16);
    /* 230-byte overscan lines, of which 448 pixels show. */
    draw(data + 34, 448, 274, 4, 230);
    decodes("pic.kid", 63054, 448, 274, 16);
    /* With no extension, the signature and exact size identify it. */
    decodes(NULL, 63054, 448, 274, 16);
    decodes("pic.xyz", 63054, 448, 274, 16);
    fails(NULL, 63055, CODEC_INVALID);
    truncations("pic.kid", 63054, 1);
    data[1] = 'd';
    fails("pic.kid", 63054, CODEC_INVALID);
    fails(NULL, 63054, CODEC_INVALID);
}

static void test_rgb_intermediate(void)
{
    unsigned x, y, c;

    memset(data, 0, 96102);
    /* Three low resolution DEGAS files; the indexes are the levels. */
    for (c = 0; c < 3; c++)
        draw(data + c * 32034u + 34u, 320, 200, 4, 160);
    /* Make green and blue differ from red: invert their plane words. */
    for (x = 0; x < 32000; x++) {
        data[32034 + 34 + x] ^= 0xff;
        data[64068 + 34 + x] = (uint8_t)(x < 16000 ? 0 : 0xff);
    }
    assert(stscreen_decode(data, 96102, "pic.rgb", &image) == CODEC_OK);
    assert(image.width == 320 && image.height == 200);
    for (y = 0; y < 200; y++)
        for (x = 0; x < 320; x++) {
            unsigned r = pattern(x, y, 16);
            expect(x, y, r * 17u, (15u - r) * 17u, y < 100 ? 0 : 255);
        }
    stscreen_free(&image);
    truncations("pic.rgb", 96102, 1);
}

static void test_dali(void)
{
    static const char *names[3] = { "pic.sd0", "pic.sd1", "pic.SD2" };
    static const unsigned widths[3] = { 320, 640, 640 }, heights[3] = { 200, 200, 400 };
    static const unsigned colours[3] = { 16, 4, 2 };
    unsigned mode;

    /* NEOchrome's layout; only the extension gives the resolution. */
    for (mode = 0; mode < 3; mode++) {
        memset(data, 0, 32128);
        palette(data + 4, 16);
        draw(data + 128, widths[mode], heights[mode], 4u >> mode, widths[mode] * (4u >> mode) / 8u);
        decodes(names[mode], 32128, widths[mode], heights[mode], colours[mode]);
        truncations(names[mode], 32128, 1);
    }
    /* Without the extension it's nothing. */
    fails(NULL, 32128, CODEC_INVALID);
    fails("pic.sd3", 32128, CODEC_INVALID);
}

static void test_paintworks(void)
{
    static const unsigned widths[3] = { 320, 640, 640 }, heights[3] = { 200, 200, 400 };
    static const unsigned colours[3] = { 16, 4, 2 };
    static uint8_t packed[70000];
    unsigned mode, page;
    size_t n;

    for (mode = 0; mode < 3; mode++)
        for (page = 0; page < 2; page++) {
            unsigned width = widths[mode], height = heights[mode] << page;
            size_t size = (size_t)32000u << page;

            /* Uncompressed: low nibble 1 or 2 is a screen, 0 a page. */
            paintworks_header(mode << 4 | (page ? 0u : 1u));
            draw(data + 128, width, height, 4u >> mode, width * (4u >> mode) / 8u);
            decodes("pic.sc0", 128 + size, width, height, colours[mode]);
            decodes("pic.pg1", 128 + size + 84, width, height, colours[mode]);
            decodes(NULL, 128 + size, width, height, colours[mode]);
            if (!page) {
                data[0x3f] = (uint8_t)(mode << 4 | 2u);
                decodes("pic.cl2", 128 + size, width, height, colours[mode]);
            }
            truncations("pic.sc1", 128 + size, 97);

            /* Compressed: the same bitmap as runs, plane by plane. */
            memcpy(bits, data + 128, size);
            n = sc_pack(bits, size, 8u >> mode, packed);
            data[0x3f] |= 0x80;
            memcpy(data + 128, packed, n);
            decodes("pic.cl0", 128 + n, width, height, colours[mode]);
            decodes("pic.pg0", 128 + n + 10, width, height, colours[mode]);
            truncations("pic.cl0", 128 + n, 61);
        }

    /* Reserved flags, resolutions and a missing signature. */
    paintworks_header(0x03);
    fails("pic.sc0", 128 + 32000, CODEC_INVALID);
    paintworks_header(0x0f);
    fails("pic.sc0", 128 + 32000, CODEC_INVALID);
    paintworks_header(0x31);
    fails("pic.sc0", 128 + 32000, CODEC_INVALID);
    paintworks_header(0x01);
    data[0x3e] = 'a';
    fails("pic.sc0", 128 + 32000, CODEC_INVALID);
    fails(NULL, 128 + 32000, CODEC_INVALID);
}

static void test_graphics_processor(void)
{
    static const unsigned widths[3] = { 320, 640, 640 }, heights[3] = { 200, 200, 400 };
    static const unsigned colours[3] = { 16, 4, 2 };
    unsigned mode;
    size_t pos, at, n;

    for (mode = 0; mode < 3; mode++) {
        unsigned width = widths[mode], height = heights[mode];
        unsigned unit = 4u >> mode;

        memset(data, 0, 40000);
        data[1] = (uint8_t)mode;
        palette(data + 2, 16);
        draw(data + 331, width, height, 4u >> mode, width * (4u >> mode) / 8u);
        decodes("pic.pg1", 32331, width, height, colours[mode]);
        decodes("pic.pg3", 32400, width, height, colours[mode]);
        truncations("pic.pg2", 32331, 1);

        /* Runs of one unit, each a count then the unit's bytes. */
        memcpy(bits, data + 331, 32000);
        data[1] = (uint8_t)(10u + mode);
        pos = 333;
        for (at = 0; at < 32000; ) {
            unsigned count = 1;
            while (at + (count + 1u) * unit <= 32000 && count < 255 &&
                   memcmp(bits + at, bits + at + count * unit, unit) == 0)
                count++;
            data[pos] = (uint8_t)count;
            memcpy(data + pos + 1, bits + at, unit);
            pos += 1u + unit;
            at += count * unit;
        }
        decodes("pic.pg3", pos, width, height, colours[mode]);
        truncations("pic.pg3", pos, 53);
        /* The last run may overrun the picture. */
        data[pos - 1u - unit] = 255;
        decodes("pic.pg3", pos, width, height, colours[mode]);
        /* A zero count is not a run. */
        n = pos;
        data[333] = 0;
        fails("pic.pg3", n, CODEC_INVALID);
    }

    /* Reserved modes, a bad first byte, and no signature to sniff. */
    memset(data, 0, 32331);
    data[1] = 3;
    fails("pic.pg3", 32331, CODEC_INVALID);
    data[1] = 9;
    fails("pic.pg3", 32331, CODEC_INVALID);
    data[1] = 13;
    fails("pic.pg3", 32331, CODEC_INVALID);
    data[0] = 1;
    data[1] = 0;
    fails("pic.pg3", 32331, CODEC_INVALID);
    data[0] = 0;
    fails(NULL, 32331, CODEC_INVALID);
}

static void test_ez_art(void)
{
    static uint8_t lines[32000];
    unsigned y, plane, w;
    size_t n = 0, packed;

    /* Each line holds plane 0's 40 bytes, then plane 1's and so on. */
    draw(bits, 320, 200, 4, 160);
    for (y = 0; y < 200; y++)
        for (plane = 0; plane < 4; plane++)
            for (w = plane * 2u; w < 160; w += 8) {
                lines[n++] = bits[y * 160u + w];
                lines[n++] = bits[y * 160u + w + 1];
            }
    memset(data, 0, 44);
    memcpy(data, "EZ\0\310", 4);
    palette(data + 4, 16);
    packed = packbits(lines, n, data + 44);
    decodes("pic.eza", 44 + packed, 320, 200, 16);
    decodes(NULL, 44 + packed, 320, 200, 16);
    truncations("pic.eza", 44 + packed, 59);

    /* 0x80 repeats 129 times, as EZ-Art reads it: 129 zeros, 129 0xff
       bytes, then runs of 128 zeros to the end. */
    data[44] = 0x80;
    data[45] = 0x00;
    data[46] = 0x80;
    data[47] = 0xff;
    for (n = 0; n < 248; n++) {
        data[48 + n * 2u] = 0x81;
        data[49 + n * 2u] = 0x00;
    }
    fails("pic.eza", 48 + 247 * 2, CODEC_TRUNCATED);
    assert(stscreen_decode(data, 48 + 248 * 2, "pic.eza", &image) == CODEC_OK);
    /* Line 0: the zeros run into plane 3's ninth byte, then plane 3 is set. */
    expect(0, 0, 0, 255, 0);
    expect(71, 0, 0, 255, 0);
    expect(72, 0, 0, 255, 146);
    expect(319, 0, 0, 255, 146);
    /* Line 1: planes 0 and 1, and plane 2's first 18 bytes. */
    expect(143, 1, 255, 0, 109);
    expect(144, 1, 109, 146, 36);
    expect(0, 2, 0, 255, 0);
    stscreen_free(&image);

    data[3] = 0xc7;
    fails("pic.eza", 44 + packed, CODEC_INVALID);
}

static void test_computer_eyes(void)
{
    unsigned x, y;

    /* CE1: planes of 6-bit red, green and blue, one column at a time. */
    memset(data, 0, 192022);
    memcpy(data, "EYES", 4);
    for (x = 0; x < 320; x++)
        for (y = 0; y < 200; y++) {
            size_t at = 22 + x * 200u + y;
            data[at] = (uint8_t)(x % 64u);
            data[at + 64000] = (uint8_t)(y % 64u);
            data[at + 128000] = (uint8_t)((x + y) % 64u | 0xc0u);
        }
    assert(stscreen_decode(data, 192022, "pic.ce1", &image) == CODEC_OK);
    assert(image.width == 320 && image.height == 200);
    expect(0, 0, 0, 0, 0);
    expect(63, 63, 255, 255, 0xfb);
    expect(32, 1, 130, 4, 134);
    stscreen_free(&image);
    assert(stscreen_decode(data, 192022, NULL, &image) == CODEC_OK);
    stscreen_free(&image);
    truncations("pic.ce1", 192022, 997);

    /* CE2: 640x200 words of 5:5:5 RGB. */
    memset(data + 6, 0, 256016);
    data[5] = 1;
    for (x = 0; x < 640; x++)
        for (y = 0; y < 200; y++)
            put16(data + 22 + (x * 200u + y) * 2u, 0x8000u | (x % 32u) << 10 | (y % 32u) << 5 | 31u);
    assert(stscreen_decode(data, 256022, "pic.ce2", &image) == CODEC_OK);
    assert(image.width == 640 && image.height == 200);
    expect(0, 0, 0, 0, 255);
    expect(1, 16, 8, 132, 255);
    expect(31, 31, 255, 255, 255);
    stscreen_free(&image);
    truncations("pic.ce2", 256022, 997);

    /* CE3: 640x400 grey, each column's even lines then its odd lines. */
    data[5] = 2;
    for (x = 0; x < 640; x++)
        for (y = 0; y < 400; y++)
            data[22 + x * 400u + (y & 1u) * 200u + y / 2u] = (uint8_t)(y & 1u ? 191 : x % 256u);
    assert(stscreen_decode(data, 256022, "pic.ce3", &image) == CODEC_OK);
    assert(image.width == 640 && image.height == 400);
    expect(0, 0, 0, 0, 0);
    expect(3, 0, 4, 4, 4);
    expect(0, 1, 254, 254, 254);
    expect(255, 2, 255, 255, 255);
    stscreen_free(&image);

    data[5] = 3;
    fails("pic.ce3", 256022, CODEC_INVALID);
    data[5] = 2;
    data[4] = 1;
    fails("pic.ce3", 256022, CODEC_INVALID);
}

static void test_names(void)
{
    draw(data, 640, 400, 1, 80);
    decodes("Work:Pics/ST/PIC.Doo", 32000, 640, 400, 2);
    decodes("a.b.c.doo", 32000, 640, 400, 2);
    /* Dots in directories and other lengths of extension don't count. */
    fails("Work:pics.doo/pic", 32000, CODEC_INVALID);
    fails("dir.doo:pic", 32000, CODEC_INVALID);
    fails("pic.do", 32000, CODEC_INVALID);
    fails("pic.dooo", 32000, CODEC_INVALID);
    fails("pic.", 32000, CODEC_INVALID);
    fails("", 32000, CODEC_INVALID);
    fails(NULL, 32000, CODEC_INVALID);
    fails(NULL, 0, CODEC_INVALID);
    assert(stscreen_decode(NULL, 32000, "pic.doo", &image) == CODEC_TRUNCATED);
    assert(stscreen_decode(data, 32000, "pic.doo", NULL) == CODEC_INVALID);
}

/* Encode rgba, decode it as a Paintworks file, and require identical pixels. */
static void round_trip(unsigned width, unsigned height, unsigned flags, size_t size)
{
    size_t n;

    assert(stscreen_encode(rgba, width, height, saved, &n) == CODEC_OK);
    assert(n == size);
    assert(memcmp(saved + 0x36, "ANvisionA", 9) == 0 && saved[0x3f] == flags);
    assert(saved[0] == 0 && saved[1] == 0 && saved[2] == 0 && saved[3] == (flags >> 4 & 3u));
    assert(memcmp(saved + 36, "        .   ", 12) == 0);
    assert(stscreen_decode(saved, n, "saved.sc0", &image) == CODEC_OK);
    assert(image.width == width && image.height == height);
    assert(memcmp(image.rgba, rgba, (size_t)width * height * 4u) == 0);
    stscreen_free(&image);
}

static void fill(unsigned width, unsigned height, const uint8_t (*colours)[3],
                 unsigned count)
{
    size_t i;

    for (i = 0; i < (size_t)width * height; i++) {
        const uint8_t *c = colours[(i * 7u + i / width) % count];
        rgba[i * 4u] = c[0];
        rgba[i * 4u + 1u] = c[1];
        rgba[i * 4u + 2u] = c[2];
        rgba[i * 4u + 3u] = 255;
    }
}

static void test_encode(void)
{
    uint8_t colours[17][3];
    size_t n;
    unsigned i;

    /* Every ST level in 16 colours: a low resolution screen or page. */
    for (i = 0; i < 16; i++) {
        colours[i][0] = st[i & 7u];
        colours[i][1] = st[i >> 1];
        colours[i][2] = st[7u - (i & 7u)];
    }
    fill(320, 200, colours, 16);
    round_trip(320, 200, 0x01, 32128);
    for (i = 0; i < 16; i++)
        assert(((saved[4 + i * 2u] << 8 | saved[5 + i * 2u]) & 0xf888u) == 0);
    fill(320, 400, colours, 16);
    round_trip(320, 400, 0x00, 64128);

    /* 17 colours don't fit. */
    colours[16][0] = 1;
    colours[16][1] = colours[16][2] = 0;
    fill(320, 200, colours, 17);
    assert(stscreen_encode(rgba, 320, 200, saved, &n) == CODEC_INVALID);
    assert(n == 0);

    /* STE levels, and an unused entry to mark only even ones as STE. */
    for (i = 0; i < 16; i++) {
        colours[i][0] = (uint8_t)(i * 17u);
        colours[i][1] = (uint8_t)((15u - i) * 17u);
        colours[i][2] = 0;
    }
    fill(320, 200, colours, 16);
    round_trip(320, 200, 0x01, 32128);
    for (i = 0; i < 15; i++) {
        colours[i][0] = (uint8_t)((i % 8u) * 34u);
        colours[i][1] = (uint8_t)((i / 8u) * 34u);
        colours[i][2] = 34;
    }
    fill(320, 200, colours, 15);
    round_trip(320, 200, 0x01, 32128);
    assert(saved[4 + 15 * 2u] == 0x08 && saved[5 + 15 * 2u] == 0x88);
    colours[15][0] = colours[15][1] = 102;
    colours[15][2] = 34;
    fill(320, 200, colours, 16);
    assert(stscreen_encode(rgba, 320, 200, saved, &n) == CODEC_INVALID);
    colours[0][0] = 36;
    colours[1][0] = 17;
    fill(320, 200, colours, 2);
    assert(stscreen_encode(rgba, 320, 200, saved, &n) == CODEC_INVALID);

    /* Four colours: a medium resolution screen, or a page at 640x400. */
    for (i = 0; i < 4; i++)
        colours[i][0] = colours[i][1] = colours[i][2] = st[i * 2u + 1u];
    fill(640, 200, colours, 4);
    round_trip(640, 200, 0x11, 32128);
    fill(640, 400, colours, 4);
    round_trip(640, 400, 0x10, 64128);
    rgba[0] = 0;
    assert(stscreen_encode(rgba, 640, 400, saved, &n) == CODEC_INVALID);

    /* Black and white: a high resolution screen, or a page at 640x800. */
    colours[0][0] = colours[0][1] = colours[0][2] = 0;
    colours[1][0] = colours[1][1] = colours[1][2] = 255;
    fill(640, 400, colours, 2);
    round_trip(640, 400, 0x21, 32128);
    fill(640, 800, colours, 2);
    round_trip(640, 800, 0x20, 64128);
    rgba[0] = rgba[1] = rgba[2] = 146;
    assert(stscreen_encode(rgba, 640, 800, saved, &n) == CODEC_INVALID);

    /* Transparency is composited over white. */
    memset(rgba, 0, 320u * 200u * 4u);
    rgba[3] = 255;
    assert(stscreen_encode(rgba, 320, 200, saved, &n) == CODEC_OK);
    assert(stscreen_decode(saved, n, "t.sc0", &image) == CODEC_OK);
    expect(0, 0, 0, 0, 0);
    expect(1, 0, 255, 255, 255);
    stscreen_free(&image);

    /* Other sizes and missing buffers. */
    assert(stscreen_encode(rgba, 320, 199, saved, &n) == CODEC_INVALID);
    assert(stscreen_encode(rgba, 448, 274, saved, &n) == CODEC_INVALID);
    assert(stscreen_encode(rgba, 1, 1, saved, &n) == CODEC_INVALID);
    assert(stscreen_encode(NULL, 320, 200, saved, &n) == CODEC_INVALID);
    assert(stscreen_encode(rgba, 320, 200, NULL, &n) == CODEC_INVALID);
    assert(stscreen_encode(rgba, 320, 200, saved, NULL) == CODEC_INVALID);
}

int main(void)
{
    test_levels();
    test_art();
    test_doodle();
    test_colorstar();
    test_sinbad_and_synthetic();
    test_construction_kit();
    test_rgb_intermediate();
    test_dali();
    test_paintworks();
    test_graphics_processor();
    test_ez_art();
    test_computer_eyes();
    test_names();
    test_encode();
    puts("stscreen: ok");
    return 0;
}
