#include "../formats/paa/decode.h"
#include "../formats/paa/encode.h"
#include "../formats/paa/lzo.h"
#include "../formats/paa/lzss.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t file[1 << 20];
static size_t len;

static void put8(unsigned v) { file[len++] = (uint8_t)v; }
static void put16(unsigned v) { put8(v & 255); put8(v >> 8); }
static void put24(unsigned long v) { put16(v & 0xFFFF); put8((unsigned)(v >> 16)); }
static void put32(unsigned long v) { put16(v & 0xFFFF); put16((unsigned)(v >> 16)); }
static void put(const void *p, size_t n) { memcpy(file + len, p, n); len += n; }

static void tagg(const char *name, unsigned long value)
{
    put("GGAT", 4);
    put(name, 4);
    put32(4);
    put32(value);
}

/* Type word (0 for none), the usual taggs and an empty palette. */
static void begin(unsigned type)
{
    len = 0;
    if (type)
        put16(type);
    tagg("CGVA", 0xFF808080);
    tagg("CXAM", 0xFFFFFFFF);
    put16(0);
}

static void mip(unsigned w, unsigned h, const void *data, size_t n)
{
    put16(w);
    put16(h);
    put24(n);
    put(data, n);
}

static void finish(void)
{
    put16(0);
    put16(0);
    put16(0);
}

static uint32_t signed_sum(const uint8_t *p, size_t n)
{
    uint32_t sum = 0;
    while (n--) {
        sum += *p < 0x80 ? *p : *p - 0x100u;
        p++;
    }
    return sum;
}

/* LZSS with literals only, the simplest valid stream. */
static size_t literal_lzss(const uint8_t *in, size_t n, uint8_t *out)
{
    size_t i, o = 0;
    uint32_t sum = signed_sum(in, n);
    for (i = 0; i < n; i++) {
        if (i % 8 == 0)
            out[o++] = (uint8_t)(n - i >= 8 ? 0xFF : (1u << (n - i)) - 1);
        out[o++] = in[i];
    }
    out[o++] = (uint8_t)sum; out[o++] = (uint8_t)(sum >> 8);
    out[o++] = (uint8_t)(sum >> 16); out[o++] = (uint8_t)(sum >> 24);
    return o;
}

/* An LZSS level of pixel bytes. */
static void lzss_mip(unsigned w, unsigned h, const uint8_t *pixels, size_t n)
{
    static uint8_t packed[1 << 16];
    mip(w, h, packed, literal_lzss(pixels, n, packed));
}

static struct paa_image image;

static enum codec_result decode(unsigned long index)
{
    paa_free(&image);
    return paa_decode(file, len, index, &image);
}

static void pixel(unsigned x, unsigned y, unsigned r, unsigned g, unsigned b, unsigned a)
{
    const uint8_t *p = image.rgba + ((size_t)y * image.width + x) * 4u;
    if (p[0] != r || p[1] != g || p[2] != b || p[3] != a) {
        fprintf(stderr, "pixel %u,%u is %u %u %u %u, expected %u %u %u %u\n",
                x, y, p[0], p[1], p[2], p[3], r, g, b, a);
        assert(0);
    }
}

static unsigned long count(void)
{
    unsigned long n;
    paa_count(file, len, &n);
    return n;
}

/* DXT1 blocks: 4-colour red/blue, and 3-colour whose index 3 is clear. */
static const uint8_t red_block[8] = { 0x00, 0xF8, 0x1F, 0x00, 0, 0, 0, 0 };
static const uint8_t clear_block[8] = { 0x1F, 0x00, 0x00, 0xF8, 0xFF, 0xFF, 0xFF, 0xFF };

static void test_dxt1(void)
{
    uint8_t blocks[4 * 8];
    unsigned i;

    begin(0xFF01);
    /* 5x3: two blocks across, one down; edge pixels are cropped. */
    memcpy(blocks, red_block, 8);
    memcpy(blocks + 8, clear_block, 8);
    mip(5, 3, blocks, 16);
    mip(2, 1, red_block, 8);
    finish();
    assert(count() == 2);
    assert(decode(0) == CODEC_OK);
    assert(image.width == 5 && image.height == 3);
    pixel(0, 0, 255, 0, 0, 255);
    pixel(3, 2, 255, 0, 0, 255);
    pixel(4, 0, 0, 0, 0, 0);
    pixel(4, 2, 0, 0, 0, 0);
    assert(decode(1) == CODEC_OK);
    assert(image.width == 2 && image.height == 1);
    pixel(1, 0, 255, 0, 0, 255);
    assert(decode(2) == CODEC_INVALID);
    assert(decode(1000) == CODEC_INVALID);

    /* A raw level too small for its size is invalid; spare bytes are fine. */
    begin(0xFF01);
    mip(8, 4, blocks, 8);
    finish();
    assert(decode(0) == CODEC_INVALID);
    for (i = 0; i < 4; i++)
        memcpy(blocks + i * 8, red_block, 8);
    begin(0xFF01);
    mip(4, 4, blocks, 32);
    finish();
    assert(decode(0) == CODEC_OK);
}

static void test_bc2_bc3(void)
{
    /* Colour 0x8410 is 132,130,132; alpha 8/15 and 136 are both 136. */
    static const uint8_t bc2[16] = { 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88,
                                     0x10, 0x84, 0x10, 0x84, 0, 0, 0, 0 };
    static const uint8_t bc3[16] = { 136, 0, 0, 0, 0, 0, 0, 0,
                                     0x10, 0x84, 0x10, 0x84, 0, 0, 0, 0 };
    begin(0xFF03);
    mip(4, 4, bc2, 16);
    finish();
    assert(decode(0) == CODEC_OK);
    pixel(3, 3, 132, 130, 132, 136);
    begin(0xFF05);
    mip(4, 4, bc3, 16);
    finish();
    assert(decode(0) == CODEC_OK);
    pixel(0, 0, 132, 130, 132, 136);
    /* DXT2 and DXT4 are premultiplied: 132 * 255 / 136 rounds to 248. */
    begin(0xFF02);
    mip(4, 4, bc2, 16);
    finish();
    assert(decode(0) == CODEC_OK);
    pixel(2, 1, 248, 244, 248, 136);
    begin(0xFF04);
    mip(4, 4, bc3, 16);
    finish();
    assert(decode(0) == CODEC_OK);
    pixel(1, 2, 248, 244, 248, 136);
}

static void test_uncompressed(void)
{
    static const uint8_t argb4444[] = { 0x4C, 0x8F, 0x00, 0xF0 };
    static const uint8_t argb1555[] = { 0x00, 0xFC, 0x1F, 0x00, 0x63, 0x0C };
    static const uint8_t argb8888[] = { 0x10, 0x20, 0x30, 0x40 };
    static const uint8_t ai88[] = { 0x55, 0xAA, 0xFF, 0x00 };

    begin(0x4444);
    lzss_mip(2, 1, argb4444, sizeof argb4444);
    finish();
    assert(decode(0) == CODEC_OK);
    pixel(0, 0, 255, 68, 204, 136);
    pixel(1, 0, 0, 0, 0, 255);

    begin(0x1555);
    lzss_mip(3, 1, argb1555, sizeof argb1555);
    finish();
    assert(decode(0) == CODEC_OK);
    pixel(0, 0, 255, 0, 0, 255);
    pixel(1, 0, 0, 0, 255, 0);
    pixel(2, 0, 24, 24, 24, 0);

    begin(0x8888);
    lzss_mip(1, 1, argb8888, sizeof argb8888);
    finish();
    assert(decode(0) == CODEC_OK);
    pixel(0, 0, 0x30, 0x20, 0x10, 0x40);

    /* Gray and alpha, both kept, even when alpha is zero. */
    begin(0x8080);
    lzss_mip(1, 2, ai88, sizeof ai88);
    finish();
    assert(decode(0) == CODEC_OK);
    assert(image.width == 1 && image.height == 2);
    pixel(0, 0, 0x55, 0x55, 0x55, 0xAA);
    pixel(0, 1, 0xFF, 0xFF, 0xFF, 0);

    /* These levels are LZSS, but some writers store them raw: that is
       accepted when the size is exactly right. */
    begin(0x8888);
    mip(1, 1, argb8888, sizeof argb8888);
    finish();
    assert(decode(0) == CODEC_OK);
    pixel(0, 0, 0x30, 0x20, 0x10, 0x40);
    begin(0x8888);
    mip(1, 1, argb4444, 3);
    finish();
    assert(decode(0) == CODEC_INVALID);
    begin(0x4444);
    mip(1, 1, argb8888, sizeof argb8888);
    finish();
    assert(decode(0) == CODEC_INVALID);

    /* A palette in a typed file is skipped; the top width bit is size. */
    len = 0;
    put16(0x8080);
    put16(2);
    put("abcdef", 6);
    lzss_mip(1, 1, ai88, 2);
    finish();
    assert(decode(0) == CODEC_OK);
    pixel(0, 0, 0x55, 0x55, 0x55, 0xAA);
}

static void index_header(int demo)
{
    len = 0;
    if (!demo)
        tagg("CGVA", 0xFF000000);
    put16(3);
    put("\x01\x02\x03" "\x10\x20\x30" "\xFF\xFF\xFF", 9);
}

static void test_index(void)
{
    /* Runs: 3 literals, then index 1 four times. */
    static const uint8_t rle[] = { 0x02, 0, 2, 5, 0x83, 1 };
    static const uint8_t indices[] = { 1, 0, 2, 2 };
    int demo;

    for (demo = 0; demo < 2; demo++) {
        index_header(demo);
        mip(7, 1, rle, sizeof rle);
        finish();
        assert(count() == 1);
        assert(decode(0) == CODEC_OK);
        pixel(0, 0, 3, 2, 1, 255);
        pixel(1, 0, 255, 255, 255, 255);
        /* Index 5 is past the three-colour palette. */
        pixel(2, 0, 0, 0, 0, 255);
        pixel(6, 0, 0x30, 0x20, 0x10, 255);
    }

    /* A run past the last pixel is clamped. */
    index_header(0);
    mip(2, 1, rle + 3, 3);
    finish();
    assert(decode(0) == CODEC_OK);
    pixel(1, 0, 0x30, 0x20, 0x10, 255);
    /* Runs that stop short are invalid. */
    index_header(0);
    mip(8, 1, rle, sizeof rle);
    finish();
    assert(decode(0) == CODEC_INVALID);
    index_header(0);
    mip(3, 1, rle, 2);
    finish();
    assert(decode(0) == CODEC_INVALID);

    /* 1234x8765 marks an LZSS level with its real size after it. */
    index_header(0);
    put16(1234);
    put16(8765);
    lzss_mip(2, 2, indices, sizeof indices);
    finish();
    assert(count() == 1);
    assert(decode(0) == CODEC_OK);
    assert(image.width == 2 && image.height == 2);
    pixel(0, 0, 0x30, 0x20, 0x10, 255);
    pixel(1, 1, 255, 255, 255, 255);

    /* An index file needs a palette. */
    len = 0;
    tagg("CGVA", 0);
    put16(0);
    mip(1, 1, rle, 2);
    finish();
    assert(decode(0) == CODEC_INVALID);
}

static void test_lzss(void)
{
    uint8_t out[64], in[64], packed[128];
    size_t n;
    uint32_t sum;

    /* "ab", then 3 back 2 (overlapping), then 4 before the start: spaces. */
    n = 0;
    in[n++] = 0x03; in[n++] = 'a'; in[n++] = 'b';
    in[n++] = 0x02; in[n++] = 0x00;
    in[n++] = 0x09; in[n++] = 0x01;
    memcpy(out, "ababa    ", 9);
    sum = signed_sum(out, 9);
    memcpy(packed, in, n);
    packed[n] = (uint8_t)sum; packed[n + 1] = (uint8_t)(sum >> 8);
    packed[n + 2] = (uint8_t)(sum >> 16); packed[n + 3] = (uint8_t)(sum >> 24);
    memset(out, 0, sizeof out);
    assert(paa_lzss_expand(packed, n + 4, out, 9) == CODEC_OK);
    assert(memcmp(out, "ababa    ", 9) == 0);
    /* A shorter output cuts the last reference; the sum no longer fits. */
    assert(paa_lzss_expand(packed, n + 4, out, 8) == CODEC_INVALID);
    /* Missing or wrong sums, and short streams. */
    assert(paa_lzss_expand(packed, n + 3, out, 9) == CODEC_INVALID);
    packed[n] ^= 1;
    assert(paa_lzss_expand(packed, n + 4, out, 9) == CODEC_INVALID);
    assert(paa_lzss_expand(packed, 4, out, 9) == CODEC_INVALID);
    assert(paa_lzss_expand(packed, 6, out, 9) == CODEC_INVALID);
    assert(paa_lzss_expand(packed, 0, out, 9) == CODEC_INVALID);
    /* Distance 0 refers to the byte being written. */
    packed[3] = 0x00; packed[4] = 0x00;
    assert(paa_lzss_expand(packed, n + 4, out, 9) == CODEC_INVALID);

    /* Other BI files sum unsigned bytes; both are accepted. */
    memcpy(packed, "\x03\xC8\x80\x48\x01\x00\x00", 7);
    assert(paa_lzss_expand(packed, 7, out, 2) == CODEC_OK);
    assert(out[0] == 0xC8 && out[1] == 0x80);

    /* The packer round-trips repetitive and random data. */
    for (n = 0; n < sizeof in; n++)
        in[n] = (uint8_t)(n < 40 ? "abc"[n % 3] : rand());
    n = paa_lzss_pack(in, sizeof in, packed);
    assert(n <= paa_lzss_bound(sizeof in) && n < sizeof in);
    assert(paa_lzss_expand(packed, n, out, sizeof in) == CODEC_OK);
    assert(memcmp(in, out, sizeof in) == 0);
    n = paa_lzss_pack(in, 0, packed);
    assert(n == 4 && paa_lzss_expand(packed, n, out, 0) == CODEC_OK);
}

static void check_lzo(const uint8_t *stream, size_t n, const char *expect, size_t out_len)
{
    static uint8_t out[20000];
    size_t cut;
    assert(paa_lzo_expand(stream, n, out, out_len) == CODEC_OK);
    if (expect != NULL)
        assert(memcmp(out, expect, out_len) == 0);
    /* The wrong length either way, and any truncation, is invalid. */
    assert(paa_lzo_expand(stream, n, out, out_len - 1) == CODEC_INVALID);
    assert(paa_lzo_expand(stream, n, out, out_len + 1) == CODEC_INVALID);
    for (cut = 0; cut < n; cut++)
        assert(paa_lzo_expand(stream, cut, out, out_len) == CODEC_INVALID);
}

static void test_lzo(void)
{
    /* First-byte run of 5, then M2: 7 bytes from 5 back. */
    static const uint8_t m2[] = { 22, 'A', 'B', 'C', 'D', 'E', 0xD0, 0x00, 0x11, 0, 0 };
    /* First-byte run of 3, M3 of 31 + 7 + 2 from 3 back, then 1 literal. */
    static const uint8_t m3[] = { 20, 'x', 'y', 'z', 0x20, 7, 0x09, 0x00, '!', 0x11, 0, 0 };
    /* 3 literals then M1 after a short run: 2 from 3 back, then 2 literals. */
    static const uint8_t m1[] = { 20, 'a', 'b', 'c', 0x0A, 0x00, 'X', 'Y', 0x11, 0, 0 };
    static uint8_t long_run[20000];
    char expect[64];
    size_t n = 0, i;

    check_lzo(m2, sizeof m2, "ABCDEABCDEAB", 12);
    memcpy(expect, "xyz", 3);
    for (i = 3; i < 43; i++)
        expect[i] = "xyz"[i % 3];
    expect[43] = '!';
    check_lzo(m3, sizeof m3, expect, 44);
    check_lzo(m1, sizeof m1, "abcabXY", 7);

    /* A run of 2060 (extended by 8 zero bytes), then a 3-byte M1 2049 back. */
    long_run[n++] = 0;
    for (i = 0; i < 8; i++)
        long_run[n++] = 0;
    long_run[n++] = 2;
    for (i = 0; i < 2060; i++)
        long_run[n++] = (uint8_t)i;
    long_run[n++] = 0x00; long_run[n++] = 0x00;
    long_run[n++] = 0x11; long_run[n++] = 0; long_run[n++] = 0;
    check_lzo(long_run, n, NULL, 2063);

    /* A run of 16390, then M4: 3 bytes from 0x4001 back. */
    n = 0;
    long_run[n++] = 0;
    for (i = 0; i < 64; i++)
        long_run[n++] = 0;
    long_run[n++] = 52;
    for (i = 0; i < 16390; i++)
        long_run[n++] = (uint8_t)(i * 7);
    long_run[n++] = 0x11; long_run[n++] = 0x04; long_run[n++] = 0x00;
    long_run[n++] = 0x11; long_run[n++] = 0; long_run[n++] = 0;
    check_lzo(long_run, n, NULL, 16393);

    /* Matches reaching before the start. */
    {
        static const uint8_t far[] = { 20, 'a', 'b', 'c', 0x0C, 0x00, 0x11, 0, 0 };
        static const uint8_t m1_far[] = { 0x01, 'a', 'b', 'c', 'd', 0x04, 0x00, 0x11, 0, 0 };
        uint8_t out[16];
        assert(paa_lzo_expand(far, sizeof far, out, 5) == CODEC_INVALID);
        assert(paa_lzo_expand(m1_far, sizeof m1_far, out, 7) == CODEC_INVALID);
    }
}

static void test_truncation(void)
{
    static const uint8_t pixels[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
    size_t full, first_end, cut;
    enum codec_result r;

    len = 0;
    put16(0x8888);
    tagg("CGVA", 0x80808080);
    tagg("GALF", 1);
    put16(0);
    lzss_mip(2, 2, pixels, 16);
    first_end = len;
    lzss_mip(1, 1, pixels, 4);
    finish();
    full = len;
    assert(count() == 2);
    for (cut = 0; cut < full; cut++) {
        len = cut;
        r = decode(0);
        if (cut < first_end)
            assert(r == CODEC_TRUNCATED);
        else
            assert(r == CODEC_OK);
        /* Levels cut short aren't counted. */
        assert(count() == (cut < first_end ? 0ul : 1ul) + (cut >= full - 6 ? 1ul : 0ul));
    }

    /* A tagg that runs past the end of the file. */
    len = 0;
    put16(0xFF01);
    put("GGATCGVA", 8);
    put32(100);
    assert(decode(0) == CODEC_TRUNCATED);
    /* The end marker straight after the palette: no levels. */
    begin(0xFF01);
    finish();
    assert(count() == 0);
    assert(decode(0) == CODEC_TRUNCATED);
}

static void test_invalid_sizes(void)
{
    static const uint8_t block[8];

    /* A level with one zero side. */
    begin(0xFF01);
    mip(0, 4, block, 8);
    finish();
    assert(decode(0) == CODEC_INVALID);
    begin(0xFF01);
    mip(4, 0, block, 8);
    finish();
    assert(decode(0) == CODEC_INVALID);
    /* 32767x32767 is past the 16M pixel limit. */
    begin(0xFF01);
    mip(32767, 32767, block, 8);
    finish();
    assert(decode(0) == CODEC_TOO_LARGE);
    /* 65535 wide 8888 is size, not an LZO flag, and too large too. */
    begin(0x8888);
    mip(65535, 257, block, 8);
    finish();
    assert(decode(0) == CODEC_TOO_LARGE);
    /* The LZO flag with a bad stream. */
    begin(0xFF01);
    mip(0x8004, 4, block, 8);
    finish();
    assert(decode(0) == CODEC_INVALID);
}

static void test_lzo_level(void)
{
    /* One DXT1 block: first-byte run of 8 literals, then the end marker. */
    uint8_t stream[12];
    stream[0] = 17 + 8;
    memcpy(stream + 1, red_block, 8);
    stream[9] = 0x11; stream[10] = 0; stream[11] = 0;
    begin(0xFF01);
    mip(0x8000 | 4, 4, stream, sizeof stream);
    finish();
    assert(decode(0) == CODEC_OK);
    assert(image.width == 4);
    pixel(3, 3, 255, 0, 0, 255);
}

static void test_encode(void)
{
    static uint8_t rgba[64 * 48 * 4];
    unsigned w, h;
    size_t i, n;
    uint8_t *out;
    int opaque;

    for (opaque = 0; opaque < 2; opaque++)
        for (w = 1; w <= 64; w += 21)
            for (h = 1; h <= 48; h += 23) {
                for (i = 0; i < (size_t)w * h * 4; i++)
                    rgba[i] = (uint8_t)(i % 4 == 3 && opaque ? 255 : (i / 5) * 37);
                assert(paa_encode(rgba, w, h, &out, &n) == CODEC_OK);
                assert(out[0] == 0x88 && out[1] == 0x88);
                assert(memcmp(out + 2, "GGATCGVA", 8) == 0);
                /* The alpha flag only when a pixel isn't opaque. */
                assert((memcmp(out + 34, "GGATGALF", 8) == 0) == !opaque);
                assert(memcmp(out + n - 6, "\0\0\0\0\0\0", 6) == 0);
                assert(n <= sizeof file);
                memcpy(file, out, n);
                len = n;
                free(out);
                assert(count() == 1);
                assert(decode(0) == CODEC_OK);
                assert(image.width == w && image.height == h);
                assert(memcmp(image.rgba, rgba, (size_t)w * h * 4) == 0);
            }

    /* The average colour tagg is an A8R8G8B8 word. */
    memcpy(rgba, "\x10\x20\x30\x40\x30\x40\x50\x60", 8);
    assert(paa_encode(rgba, 2, 1, &out, &n) == CODEC_OK);
    assert(memcmp(out + 14, "\x40\x30\x20\x50", 4) == 0);
    /* The offset tagg points at the first level. */
    assert(memcmp(out + 50, "GGATSFFO", 8) == 0);
    assert(out[62] == 128 && out[63] == 0);
    assert(out[128] == 2 && out[130] == 1);
    free(out);

    assert(paa_encode(rgba, 0, 1, &out, &n) == CODEC_INVALID);
    assert(paa_encode(rgba, 65536, 1, &out, &n) == CODEC_INVALID);
    assert(paa_encode(rgba, 8192, 4096, &out, &n) == CODEC_TOO_LARGE);
    assert(out == NULL);
}

/* 2048x2048 noise packs past the 24-bit level size. */
static void test_encode_too_large(void)
{
    size_t i, n, pixels = 2048u * 2048u;
    uint8_t *rgba = malloc(pixels * 4), *out;
    uint32_t x = 1;
    assert(rgba != NULL);
    for (i = 0; i < pixels * 4; i++) {
        x = x * 1103515245u + 12345u;
        rgba[i] = (uint8_t)(x >> 24);
    }
    assert(paa_encode(rgba, 2048, 2048, &out, &n) == CODEC_TOO_LARGE);
    assert(out == NULL);
    free(rgba);
}

int main(void)
{
    test_dxt1();
    test_bc2_bc3();
    test_uncompressed();
    test_index();
    test_lzss();
    test_lzo();
    test_lzo_level();
    test_truncation();
    test_invalid_sizes();
    test_encode();
    test_encode_too_large();
    paa_free(&image);
    puts("paa: ok");
    return 0;
}
