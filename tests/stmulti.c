#include "../formats/stmulti/decode.h"
#include "common/atarist.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t file[400000];

static void put16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put32(uint8_t *p, unsigned long v)
{
    put16(p, (unsigned)(v >> 16));
    put16(p + 2, (unsigned)v);
}

static const uint8_t *pixel(const struct stmulti_image *im, unsigned x, unsigned y)
{
    return im->rgba + ((size_t)y * im->width + x) * 4u;
}

static int is_rgb(const uint8_t *p, unsigned r, unsigned g, unsigned b)
{
    return p[0] == r && p[1] == g && p[2] == b && p[3] == 255;
}

/* ---- MPP ---- */

static const struct { unsigned width, height, colours, reload; } modes[4] = {
    { 320, 199, 52, 15 }, { 320, 199, 46, 15 }, { 320, 199, 54, 15 }, { 416, 273, 48, 10 },
};
static const unsigned depths[4] = { 9, 12, 0, 15 };

struct bitwriter { uint8_t *p; size_t bit; };

static void write_bits(struct bitwriter *w, unsigned v, unsigned count)
{
    while (count-- > 0) {
        if ((v >> count) & 1u)
            w->p[w->bit / 8u] |= (uint8_t)(0x80u >> (w->bit % 8u));
        w->bit++;
    }
}

static size_t mpp_palette_size(unsigned mode, unsigned depth)
{
    return ((size_t)modes[mode].colours * modes[mode].height * depths[depth] + 15u) / 16u * 2u;
}

/* Every pixel of line y shows colour index (x / 8 + y) % 16, or index
   `fixed` if it is below 16. Palette entry n of each line is n, plus the
   line's number in the top bits where the depth has room. */
static void mpp_screen(uint8_t *p, unsigned mode, unsigned depth, unsigned fixed,
                       unsigned salt)
{
    unsigned w = modes[mode].width, h = modes[mode].height, x, y, n, plane;
    size_t palette = mpp_palette_size(mode, depth);
    struct bitwriter bw = { p, 0 };
    uint8_t *bitmap = p + palette;

    memset(p, 0, palette + (size_t)w * h / 2u);
    for (y = 0; y < h; y++)
        for (n = 0; n < modes[mode].colours; n++)
            write_bits(&bw, (n + salt) & ((1u << depths[depth]) - 1u), depths[depth]);
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            unsigned c = fixed < 16 ? fixed : (x / 8u + y) % 16u;
            uint8_t *group = bitmap + (size_t)y * (w / 2u) + (x / 16u) * 8u;
            unsigned bit = 15u - x % 16u;
            for (plane = 0; plane < 4; plane++)
                if ((c >> plane) & 1u)
                    group[plane * 2u + (bit < 8u)] |= (uint8_t)(1u << bit % 8u);
        }
    }
}

static size_t make_mpp(unsigned mode, unsigned depth, int two, unsigned extra,
                       unsigned fixed)
{
    size_t screen = mpp_palette_size(mode, depth) +
                    (size_t)modes[mode].width * modes[mode].height / 2u;

    memset(file, 0, 12 + extra);
    memcpy(file, "MPP", 3);
    file[3] = (uint8_t)mode;
    file[4] = (uint8_t)(depth | (two ? 4u : 0u));
    put32(file + 8, extra);
    memset(file + 12, 'x', extra);
    mpp_screen(file + 12 + extra, mode, depth, fixed, 0);
    if (two)
        mpp_screen(file + 12 + extra + screen, mode, depth, fixed, 1);
    return 12 + extra + screen * (two ? 2u : 1u);
}

/* The colour a 12-bit STE entry n decodes to. */
static void ste(unsigned n, uint8_t rgb[3])
{
    rgb[0] = st_level(n >> 8 & 15u, 1);
    rgb[1] = st_level(n >> 4 & 15u, 1);
    rgb[2] = st_level(n & 15u, 1);
}

static int is_ste(const uint8_t *p, unsigned n)
{
    uint8_t rgb[3];

    ste(n, rgb);
    return is_rgb(p, rgb[0], rgb[1], rgb[2]);
}

static void test_mpp_sizes(void)
{
    struct stmulti_image im;
    unsigned mode, depth;

    for (mode = 0; mode < 4; mode++) {
        for (depth = 0; depth < 4; depth++) {
            size_t n;
            if (depth == 2)
                continue;
            n = make_mpp(mode, depth, 0, 0, 99);
            assert(stmulti_decode(file, n, &im) == CODEC_OK);
            assert(im.width == modes[mode].width && im.height == modes[mode].height);
            stmulti_free(&im);
            /* Truncated anywhere: in the header, palette or bitmap. */
            assert(stmulti_decode(file, 11, &im) == CODEC_TRUNCATED);
            assert(stmulti_decode(file, 12 + mpp_palette_size(mode, depth) / 2u, &im) ==
                   CODEC_TRUNCATED);
            assert(stmulti_decode(file, n - 1, &im) == CODEC_TRUNCATED);
            assert(im.rgba == NULL);
            /* Anything after the picture is ignored. */
            assert(stmulti_decode(file, n + 100, &im) == CODEC_OK);
            stmulti_free(&im);
        }
    }
}

/* Mode 2, STE: reloads every 8 columns from column 4, slot k % 16. */
static void test_mpp_mode2(void)
{
    struct stmulti_image im;
    size_t n = make_mpp(2, 1, 0, 0, 5);

    assert(stmulti_decode(file, n, &im) == CODEC_OK);
    /* Slot 5 is the 5th colour loaded before the line (entry 4) until
       reload 5 at column 44 (entry 15 + 5), then reload 21 at column 172
       (entry 15 + 21) and reload 37 at 300, after the black reload 32
       takes no entry (entry 15 + 36). */
    assert(is_ste(pixel(&im, 0, 0), 4));
    assert(is_ste(pixel(&im, 43, 0), 4));
    assert(is_ste(pixel(&im, 44, 0), 20));
    assert(is_ste(pixel(&im, 171, 100), 20));
    assert(is_ste(pixel(&im, 172, 100), 36));
    assert(is_ste(pixel(&im, 300, 198), 51));
    stmulti_free(&im);

    /* Slot 0 is black before its first reload, and again after reload 32. */
    n = make_mpp(2, 1, 0, 0, 0);
    assert(stmulti_decode(file, n, &im) == CODEC_OK);
    assert(is_rgb(pixel(&im, 3, 7), 0, 0, 0));
    assert(is_ste(pixel(&im, 4, 7), 15));
    assert(is_ste(pixel(&im, 259, 7), 15 + 16));
    assert(is_rgb(pixel(&im, 260, 7), 0, 0, 0));
    assert(is_rgb(pixel(&im, 319, 198), 0, 0, 0));
    stmulti_free(&im);
}

/* Mode 1: reloads at 9, 13, then pairs 20 columns apart. */
static void test_mpp_mode1(void)
{
    struct stmulti_image im;
    size_t n = make_mpp(1, 1, 0, 0, 3);

    assert(stmulti_decode(file, n, &im) == CODEC_OK);
    /* Reload 3 is at 33, reload 19 at 193. */
    assert(is_ste(pixel(&im, 32, 1), 2));
    assert(is_ste(pixel(&im, 33, 1), 15 + 3));
    assert(is_ste(pixel(&im, 192, 1), 15 + 3));
    /* Reload 16 (black) takes no entry, so reload 19 is entry 15 + 18. */
    assert(is_ste(pixel(&im, 193, 1), 15 + 18));
    stmulti_free(&im);
}

/* Mode 0: reloads 4 apart from 33, jumps after 15, 31 and 37. */
static void test_mpp_mode0(void)
{
    struct stmulti_image im;
    size_t n = make_mpp(0, 1, 0, 0, 15);

    assert(stmulti_decode(file, n, &im) == CODEC_OK);
    assert(is_ste(pixel(&im, 92, 2), 14));
    assert(is_ste(pixel(&im, 93, 2), 15 + 15));
    /* Reload 31 at 181 + 60. */
    assert(is_ste(pixel(&im, 240, 2), 15 + 15));
    assert(is_ste(pixel(&im, 241, 2), 15 + 31));
    stmulti_free(&im);
}

/* Mode 3: 416x273, slots 0-5 carry over from the line before. */
static void test_mpp_mode3(void)
{
    struct stmulti_image im;
    size_t n = make_mpp(3, 1, 0, 0, 2);

    assert(stmulti_decode(file, n, &im) == CODEC_OK);
    /* Line 0: black until reload 2 at column 77. */
    assert(is_rgb(pixel(&im, 76, 0), 0, 0, 0));
    assert(is_ste(pixel(&im, 77, 0), 10 + 2));
    /* Reload 18 at 241 + 8, then reload 34 at 313 + 8: entry 10 + 34.
       Line 1 starts with it, since slot 2 isn't reloaded before the line. */
    assert(is_ste(pixel(&im, 249, 0), 10 + 18));
    assert(is_ste(pixel(&im, 320, 0), 10 + 18));
    assert(is_ste(pixel(&im, 321, 0), 10 + 34));
    assert(is_ste(pixel(&im, 415, 0), 10 + 34));
    assert(is_ste(pixel(&im, 0, 1), 10 + 34));
    stmulti_free(&im);

    /* Slot 6 is reloaded before each line: entry 0. */
    n = make_mpp(3, 1, 0, 0, 6);
    assert(stmulti_decode(file, n, &im) == CODEC_OK);
    assert(is_ste(pixel(&im, 0, 5), 0));
    stmulti_free(&im);
}

static void test_mpp_depths(void)
{
    struct stmulti_image im;
    size_t n;

    /* 9-bit ST: entry 20 = RRRGGGBBB 000 010 100. */
    n = make_mpp(2, 0, 0, 0, 5);
    assert(stmulti_decode(file, n, &im) == CODEC_OK);
    assert(is_rgb(pixel(&im, 44, 0), 0, st_level(2, 0), st_level(4, 0)));
    stmulti_free(&im);

    /* 15-bit: entry 20 = xyz rRRR gGGG bBBB 000 0000 0001 0100 gives green
       00100 and blue 10000 in 5 bits. */
    n = make_mpp(2, 3, 0, 0, 5);
    assert(stmulti_decode(file, n, &im) == CODEC_OK);
    assert(is_rgb(pixel(&im, 44, 0), 0, (4u << 3) | (4u >> 2), (16u << 3) | (16u >> 2)));
    stmulti_free(&im);
    /* The fifth bits: 0x7000 sets the lowest bit of all three guns. Slot 1
       shows entry 0 at column 0. */
    {
        struct bitwriter bw = { file + 12, 0 };

        n = make_mpp(2, 3, 0, 0, 1);
        assert(stmulti_decode(file, n, &im) == CODEC_OK);
        assert(is_rgb(pixel(&im, 0, 0), 0, 0, 0));
        stmulti_free(&im);
        memset(file + 12, 0, 2);
        write_bits(&bw, 0x7000, 15);
        assert(stmulti_decode(file, n, &im) == CODEC_OK);
        assert(is_rgb(pixel(&im, 0, 0), 8, 8, 8));
        stmulti_free(&im);
    }

    /* Depth 2 is reserved. */
    n = make_mpp(2, 1, 0, 0, 5);
    file[4] = 2;
    assert(stmulti_decode(file, n, &im) == CODEC_INVALID);
    /* Modes above 3 don't exist. */
    file[4] = 1;
    file[3] = 4;
    assert(stmulti_decode(file, n, &im) == CODEC_INVALID);
}

static void test_mpp_extra_and_blend(void)
{
    struct stmulti_image im;
    uint8_t a[3], b[3];
    size_t n;

    /* An extra header block is skipped. */
    n = make_mpp(2, 1, 0, 64, 5);
    assert(stmulti_decode(file, n, &im) == CODEC_OK);
    assert(is_ste(pixel(&im, 44, 0), 20));
    stmulti_free(&im);
    /* An extra header longer than the file. */
    put32(file + 8, 0xfffffff0ul);
    assert(stmulti_decode(file, n, &im) == CODEC_TRUNCATED);
    put32(file + 8, (unsigned long)n);
    assert(stmulti_decode(file, n, &im) == CODEC_TRUNCATED);

    /* Two screens are averaged: the second's entries are one higher. */
    n = make_mpp(2, 1, 1, 0, 5);
    assert(stmulti_decode(file, n, &im) == CODEC_OK);
    ste(20, a);
    ste(21, b);
    assert(is_rgb(pixel(&im, 44, 0), (a[0] + b[0]) / 2u, (a[1] + b[1]) / 2u,
                  (a[2] + b[2]) / 2u));
    stmulti_free(&im);
    assert(stmulti_decode(file, n - 1, &im) == CODEC_TRUNCATED);
}

/* ---- PhotoChrome ---- */

static uint8_t bitmap[2][PCS_BITMAP];
static uint8_t palette[2][(PCS_PALETTE_WORDS + 48u) * 2u];

static void pcs_set(uint8_t *bm, unsigned x, unsigned y, unsigned c)
{
    unsigned p;

    for (p = 0; p < 4; p++) {
        uint8_t mask = (uint8_t)(0x80u >> (x % 8u));
        uint8_t *byte = bm + p * 8000u + y * 40u + x / 8u;
        *byte = (uint8_t)((c >> p & 1u) ? *byte | mask : *byte & ~mask);
    }
}

/* A block of `count` units stored as one literal command. */
static size_t literal_block(uint8_t *out, const uint8_t *data, unsigned count, unsigned unit)
{
    put16(out, 1);
    out[2] = 1;
    put16(out + 3, count);
    memcpy(out + 5, data, (size_t)count * unit);
    return 5 + (size_t)count * unit;
}

static size_t make_pcs(unsigned flags, unsigned palette_words)
{
    size_t n = 6;

    put16(file, 320);
    put16(file + 2, 200);
    file[4] = (uint8_t)flags;
    file[5] = 1;
    n += literal_block(file + n, bitmap[0], PCS_BITMAP, 1);
    n += literal_block(file + n, palette[0], palette_words, 2);
    if (flags) {
        n += literal_block(file + n, bitmap[1], PCS_BITMAP, 1);
        n += literal_block(file + n, palette[1], palette_words, 2);
    }
    return n;
}

/* Line y's colour index c shows on screen line y + 1 as c; palette word
   w of the stream is w % 0x800, so words are distinct and ST-only. */
static void pcs_picture(void)
{
    unsigned x, y, w;

    memset(bitmap, 0, sizeof bitmap);
    for (y = 1; y < 200; y++)
        for (x = 0; x < 320; x++)
            pcs_set(bitmap[0], x, y, (y + x / 32u) % 16u);
    for (w = 0; w < PCS_PALETTE_WORDS + 48u; w++)
        put16(palette[0] + w * 2u, w % 0x800u & 0x777u);
}

static int is_word(const uint8_t *p, unsigned w, int ste_mode)
{
    w = w % 0x800u & 0x777u;
    return is_rgb(p, st_level(w >> 8, ste_mode), st_level(w >> 4 & 15u, ste_mode),
                  st_level(w & 15u, ste_mode));
}

static void test_pcs_slots(void)
{
    struct stmulti_image im;
    size_t n;
    unsigned x;

    pcs_picture();
    /* Line 13 of the bitmap is picture line 12: index 13 at x < 32. */
    for (x = 0; x < 320; x++)
        pcs_set(bitmap[0], x, 13, 13);
    pcs_set(bitmap[0], 0, 14, 0);
    for (x = 0; x < 320; x++)
        pcs_set(bitmap[0], x, 15, x < 160 ? 14 : 15);
    n = make_pcs(0, PCS_PALETTE_WORDS);
    assert(stmulti_decode(file, n, &im) == CODEC_OK);
    assert(im.width == 320 && im.height == 199);
    /* Colour 13 moves to slots 29, 45 and 61 at columns 52, 128, 300. */
    assert(is_word(pixel(&im, 51, 12), 12 * 48 + 13, 0));
    assert(is_word(pixel(&im, 52, 12), 12 * 48 + 29, 0));
    assert(is_word(pixel(&im, 127, 12), 12 * 48 + 29, 0));
    assert(is_word(pixel(&im, 128, 12), 12 * 48 + 45, 0));
    assert(is_word(pixel(&im, 299, 12), 12 * 48 + 45, 0));
    assert(is_word(pixel(&im, 300, 12), 12 * 48 + 61, 0));
    /* Colour 0 starts in slot 16. */
    assert(is_word(pixel(&im, 0, 13), 13 * 48 + 16, 0));
    /* Colours 14 and 15 have two moves: 56 and 148, 60 and 152. */
    assert(is_word(pixel(&im, 55, 14), 14 * 48 + 14, 0));
    assert(is_word(pixel(&im, 56, 14), 14 * 48 + 30, 0));
    assert(is_word(pixel(&im, 148, 14), 14 * 48 + 46, 0));
    assert(is_word(pixel(&im, 160, 14), 14 * 48 + 47, 0));
    assert(is_word(pixel(&im, 319, 14), 14 * 48 + 47, 0));
    stmulti_free(&im);

    /* The last line reads 16 words past its 48. */
    for (x = 0; x < 320; x++)
        pcs_set(bitmap[0], x, 199, 0);
    n = make_pcs(0, PCS_PALETTE_WORDS);
    assert(stmulti_decode(file, n, &im) == CODEC_OK);
    assert(is_word(pixel(&im, 319, 198), 198 * 48 + 48, 0));
    stmulti_free(&im);

    /* Files that store 48 more words decode the same. */
    n = make_pcs(0, PCS_PALETTE_WORDS + 48);
    assert(stmulti_decode(file, n, &im) == CODEC_OK);
    assert(is_word(pixel(&im, 319, 198), 198 * 48 + 48, 0));
    stmulti_free(&im);
    /* Too few palette words is an error, not a guess. */
    n = make_pcs(0, PCS_PALETTE_WORDS - 1);
    assert(stmulti_decode(file, n, &im) == CODEC_INVALID);
}

static void test_pcs_ste(void)
{
    struct stmulti_image im;
    size_t n;

    pcs_picture();
    /* One STE bit anywhere makes the whole palette STE. */
    put16(palette[0] + (PCS_PALETTE_WORDS - 1u) * 2u, 0x008);
    n = make_pcs(0, PCS_PALETTE_WORDS);
    assert(stmulti_decode(file, n, &im) == CODEC_OK);
    assert(is_word(pixel(&im, 0, 0), 1, 1));
    assert(is_word(pixel(&im, 4, 0), 17, 1));
    stmulti_free(&im);
}

static void test_pcs_two_screens(void)
{
    struct stmulti_image im;
    unsigned i, flags;
    size_t n;

    for (flags = 0x80; flags <= 0x83; flags++) {
        pcs_picture();
        /* Screen 2: every pixel index 0, palette word 0x777 everywhere, so
           the picture is the average of screen 1 and white. */
        memset(bitmap[1], 0, PCS_BITMAP);
        for (i = 0; i < PCS_PALETTE_WORDS; i++)
            put16(palette[1] + i * 2u, 0x777);
        if (!(flags & 1u))
            for (i = 0; i < PCS_BITMAP; i++)
                bitmap[1][i] ^= bitmap[0][i];
        if (!(flags & 2u))
            for (i = 0; i < PCS_PALETTE_WORDS * 2u; i++)
                palette[1][i] ^= palette[0][i];
        n = make_pcs(flags, PCS_PALETTE_WORDS);
        assert(stmulti_decode(file, n, &im) == CODEC_OK);
        {
            unsigned w = (5 * 48 + 12 + 32) % 0x800u & 0x777u;
            const uint8_t *p = pixel(&im, 200, 5);
            /* Picture line 5 is bitmap line 6, index (6 + 6) % 16 = 12 at
               x = 200: slot 12 + 32, until column 296. */
            assert(p[0] == (st_level(w >> 8, 0) + 255u) / 2u);
            assert(p[1] == (st_level(w >> 4 & 15u, 0) + 255u) / 2u);
            assert(p[2] == (st_level(w & 15u, 0) + 255u) / 2u);
        }
        stmulti_free(&im);
        assert(stmulti_decode(file, n - 1, &im) == CODEC_TRUNCATED);
    }
}

/* Run commands: 0 (word count, byte), 1 (word count, literals), 2-127
   (repeat), 128-255 (literals); overrunning runs are clipped. */
static void test_pcs_runs(void)
{
    struct stmulti_image im;
    uint8_t *p;
    size_t n;

    put16(file, 320);
    put16(file + 2, 200);
    file[4] = 0;
    file[5] = 0;
    p = file + 6;
    /* Bitmap: 40 bytes of zero (line 0), 2 literals 0xff 0xff, 38 x 0x00,
       then zero to the end, overrunning by 1000. */
    put16(p, 4);
    p += 2;
    *p++ = 0; put16(p, 40); p += 2; *p++ = 0;
    *p++ = 0xfe; *p++ = 0xff; *p++ = 0xff;
    *p++ = 38; *p++ = 0;
    *p++ = 0; put16(p, 32000 - 80 + 1000); p += 2; *p++ = 0;
    /* Palette: an empty run still stores its value, then word 0x700 for
       everything, then a literal run past the end. */
    put16(p, 3);
    p += 2;
    *p++ = 0; put16(p, 0); p += 2; put16(p, 0x070); p += 2;
    *p++ = 0; put16(p, PCS_PALETTE_WORDS - 1); p += 2; put16(p, 0x700); p += 2;
    *p++ = 0xfd; put16(p, 0x007); put16(p + 2, 0x007); put16(p + 4, 0x007); p += 6;
    n = (size_t)(p - file);
    assert(stmulti_decode(file, n, &im) == CODEC_OK);
    /* Picture line 0, x < 16: index 1 (plane 0 set) in slot 17: red. */
    assert(is_rgb(pixel(&im, 0, 0), 255, 0, 0));
    assert(is_rgb(pixel(&im, 16, 0), 255, 0, 0));
    stmulti_free(&im);
    /* Truncated inside each command. */
    {
        size_t cut;
        for (cut = 6; cut < n; cut++)
            assert(stmulti_decode(file, cut, &im) == CODEC_TRUNCATED);
    }
    /* Too few commands to fill the bitmap. */
    put16(file + 6, 3);
    assert(stmulti_decode(file, n, &im) == CODEC_INVALID);
}

static void test_other(void)
{
    struct stmulti_image im;

    memset(file, 0, 64);
    assert(stmulti_decode(file, 64, &im) == CODEC_INVALID);
    assert(stmulti_decode(file, 0, &im) == CODEC_INVALID);
    memcpy(file, "MP", 2);
    assert(stmulti_decode(file, 2, &im) == CODEC_INVALID);
    /* PhotoChrome's size is fixed. */
    put16(file, 320);
    put16(file + 2, 199);
    assert(stmulti_decode(file, 64, &im) == CODEC_INVALID);
    put16(file + 2, 200);
    assert(stmulti_decode(file, 4, &im) == CODEC_TRUNCATED);
    assert(stmulti_decode(file, 7, &im) == CODEC_TRUNCATED);
}

int main(void)
{
    test_mpp_sizes();
    test_mpp_mode2();
    test_mpp_mode1();
    test_mpp_mode0();
    test_mpp_mode3();
    test_mpp_depths();
    test_mpp_extra_and_blend();
    test_pcs_slots();
    test_pcs_ste();
    test_pcs_two_screens();
    test_pcs_runs();
    test_other();
    puts("stmulti: ok");
    return 0;
}
