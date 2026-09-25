#include "../formats/psd/decode.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* A file under construction. */
static uint8_t file[1 << 17];
static size_t size;

static void put8(unsigned v) { assert(size < sizeof file); file[size++] = (uint8_t)v; }
static void put16(unsigned v) { put8(v >> 8); put8(v & 255u); }
static void put32(uint32_t v) { put16(v >> 16); put16(v & 65535u); }
static void put(const void *data, size_t n)
{
    assert(size + n <= sizeof file);
    memcpy(file + size, data, n);
    size += n;
}
static void put_length(int wide, uint64_t v)
{
    if (wide)
        put32((uint32_t)(v >> 32));
    put32((uint32_t)v);
}
/* Patch a length written as a placeholder at at. */
static void patch32(size_t at, uint32_t v)
{
    file[at] = (uint8_t)(v >> 24);
    file[at + 1] = (uint8_t)(v >> 16);
    file[at + 2] = (uint8_t)(v >> 8);
    file[at + 3] = (uint8_t)v;
}

static size_t reads;
static size_t short_read = (size_t)-1;   /* a lying source stops here */

static size_t read_memory(void *context, uint64_t offset, void *buffer,
                          size_t length)
{
    size_t limit = *(size_t *)context;
    reads++;
    if (offset >= limit)
        return 0;
    if (length > limit - offset)
        length = (size_t)(limit - offset);
    if (offset + length > short_read)
        length = offset < short_read ? (size_t)(short_read - offset) : 0;
    memcpy(buffer, file + offset, length);
    return length;
}

static enum codec_result decode_n(size_t n, struct psd_image *image)
{
    struct psd_source s;
    s.read = read_memory;
    s.context = &n;
    s.size = n;
    return psd_decode(&s, image);
}

static enum codec_result decode(struct psd_image *image)
{
    return decode_n(size, image);
}

static void header(unsigned version, unsigned channels, unsigned width,
                   unsigned height, unsigned depth, unsigned mode)
{
    size = 0;
    put("8BPS", 4);
    put16(version);
    put32(0);
    put16(0);
    put16(channels);
    put32(height);
    put32(width);
    put16(depth);
    put16(mode);
}

/* Sample of channel c at (x, y), at the given depth. */
static unsigned value(unsigned c, unsigned x, unsigned y, unsigned depth)
{
    unsigned v = (x * 37u + y * 101u + c * 59u + 7u) & 255u;
    if (depth == 16)
        return v * 257u ^ ((x + c) & 0x7fu);
    return v;
}

typedef unsigned sample_fn(unsigned c, unsigned x, unsigned y, unsigned depth);

static size_t row_bytes(unsigned width, unsigned depth)
{
    return depth == 1 ? (width + 7u) / 8u : width * depth / 8u;
}

static void row(sample_fn *f, unsigned c, unsigned y, unsigned width,
                unsigned depth, uint8_t *out)
{
    unsigned x;
    memset(out, 0, row_bytes(width, depth));
    for (x = 0; x < width; x++) {
        unsigned v = f(c, x, y, depth);
        if (depth == 1)
            out[x / 8u] |= (uint8_t)((v & 1u) << (7u - x % 8u));
        else if (depth == 8)
            out[x] = (uint8_t)v;
        else {
            out[x * 2u] = (uint8_t)(v >> 8);
            out[x * 2u + 1u] = (uint8_t)v;
        }
    }
}

/* PackBits with runs where bytes repeat and literals elsewhere. */
static size_t pack(const uint8_t *in, size_t n, uint8_t *out)
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

/* The composite: compression then planar channel data. */
static void image_data(unsigned version, unsigned channels, unsigned width,
                       unsigned height, unsigned depth, int rle, sample_fn *f)
{
    uint8_t raw[1024], packed[2048];
    size_t bytes = row_bytes(width, depth), table;
    unsigned c, y;

    put16(rle ? 1u : 0u);
    if (!rle) {
        for (c = 0; c < channels; c++)
            for (y = 0; y < height; y++) {
                row(f, c, y, width, depth, raw);
                put(raw, bytes);
            }
        return;
    }
    table = size;
    for (c = 0; c < channels * height; c++)
        if (version == 2)
            put32(0);
        else
            put16(0);
    for (c = 0; c < channels; c++)
        for (y = 0; y < height; y++) {
            size_t n, at = table + (c * height + y) * (version == 2 ? 4u : 2u);
            row(f, c, y, width, depth, raw);
            n = pack(raw, bytes, packed);
            put(packed, n);
            if (version == 2)
                patch32(at, (uint32_t)n);
            else {
                file[at] = (uint8_t)(n >> 8);
                file[at + 1] = (uint8_t)n;
            }
        }
}

/* A simple file: no colour data, no resources, no layers. */
static void simple(unsigned version, unsigned channels, unsigned width,
                   unsigned height, unsigned depth, unsigned mode, int rle,
                   sample_fn *f)
{
    header(version, channels, width, height, depth, mode);
    put32(0);
    put32(0);
    put_length(version == 2, 0);
    image_data(version, channels, width, height, depth, rle, f);
}

static uint8_t to8(unsigned v, unsigned depth)
{
    return depth == 16 ? (uint8_t)((v + 128u) / 257u) : (uint8_t)v;
}

static const uint8_t *at(const struct psd_image *im, unsigned x, unsigned y)
{
    return im->rgba + ((size_t)y * im->width + x) * 4u;
}

static void test_rgb(void)
{
    unsigned version, depth, rle, x, y, c;
    for (version = 1; version <= 2; version++)
        for (depth = 8; depth <= 16; depth += 8)
            for (rle = 0; rle < 2; rle++) {
                struct psd_image im;
                simple(version, 3, 13, 5, depth, 3, (int)rle, value);
                assert(decode(&im) == CODEC_OK);
                assert(im.width == 13 && im.height == 5);
                for (y = 0; y < 5; y++)
                    for (x = 0; x < 13; x++) {
                        for (c = 0; c < 3; c++)
                            assert(at(&im, x, y)[c] ==
                                   to8(value(c, x, y, depth), depth));
                        assert(at(&im, x, y)[3] == 255);
                    }
                psd_free(&im);
            }
}

static void test_gray(void)
{
    unsigned depth, mode, x, y;
    for (depth = 8; depth <= 16; depth += 8)
        for (mode = 1; mode <= 8; mode += 7) {
            struct psd_image im;
            size_t mark;
            /* Duotone keeps its ink specification in the colour data. */
            header(1, 1, 9, 4, depth, mode);
            mark = size;
            put32(0);
            if (mode == 8) {
                put("duotone ink data", 16);
                patch32(mark, 16);
            }
            put32(0);
            put32(0);
            image_data(1, 1, 9, 4, depth, 1, value);
            assert(decode(&im) == CODEC_OK);
            for (y = 0; y < 4; y++)
                for (x = 0; x < 9; x++) {
                    uint8_t g = to8(value(0, x, y, depth), depth);
                    assert(at(&im, x, y)[0] == g && at(&im, x, y)[1] == g &&
                           at(&im, x, y)[2] == g && at(&im, x, y)[3] == 255);
                }
            psd_free(&im);
        }
}

static unsigned bits(unsigned c, unsigned x, unsigned y, unsigned depth)
{
    (void)c;
    (void)depth;
    return (x * 3u + y) % 5u < 2u;
}

static void test_bitmap(void)
{
    struct psd_image im;
    unsigned x, y, rle;
    for (rle = 0; rle < 2; rle++) {
        simple(1, 1, 13, 3, 1, 0, (int)rle, bits);
        assert(decode(&im) == CODEC_OK);
        for (y = 0; y < 3; y++)
            for (x = 0; x < 13; x++)
                assert(at(&im, x, y)[0] == (bits(0, x, y, 1) ? 0 : 255) &&
                       at(&im, x, y)[3] == 255);
        psd_free(&im);
    }
    simple(1, 1, 13, 3, 8, 0, 0, bits);
    assert(decode(&im) == CODEC_INVALID);
}

static void resource_header(unsigned id, uint32_t length)
{
    put("8BIM", 4);
    put16(id);
    put16(0);               /* empty name, padded */
    put32(length);
}

static void test_indexed(void)
{
    struct psd_image im;
    unsigned i, x, y, n;
    size_t mark;

    for (n = 0; n < 2; n++) {
        header(1, 1, 7, 3, 8, 2);
        put32(768);
        for (i = 0; i < 768; i++)
            put8(i < 256 ? i : i < 512 ? 255u - (i - 256u) : (i * 7u) & 255u);
        mark = size;
        put32(0);
        if (n == 1) {
            resource_header(1047, 2);
            put16(value(0, 2, 1, 8));
        }
        patch32(mark, (uint32_t)(size - mark - 4));
        put32(0);
        image_data(1, 1, 7, 3, 8, 1, value);
        assert(decode(&im) == CODEC_OK);
        for (y = 0; y < 3; y++)
            for (x = 0; x < 7; x++) {
                unsigned v = value(0, x, y, 8);
                const uint8_t *p = at(&im, x, y);
                assert(p[0] == v && p[1] == 255u - v &&
                       p[2] == ((512u + v) * 7u & 255u));
                assert(p[3] == (n == 1 && v == value(0, 2, 1, 8) ? 0 : 255));
            }
        psd_free(&im);
    }
    /* A short palette leaves the rest black; none at all is invalid. */
    header(1, 1, 2, 1, 8, 2);
    put32(6);
    put("\x10\x20\x30\x40\x50\x60", 6);
    put32(0);
    put32(0);
    put16(0);
    put8(1);
    put8(200);
    assert(decode(&im) == CODEC_OK);
    assert(at(&im, 0, 0)[0] == 0x20 && at(&im, 0, 0)[1] == 0x40 &&
           at(&im, 0, 0)[2] == 0x60);
    assert(at(&im, 1, 0)[0] == 0 && at(&im, 1, 0)[2] == 0);
    psd_free(&im);
    header(1, 1, 2, 1, 8, 2);
    put32(0);
    put32(0);
    put32(0);
    put16(0);
    put16(0);
    assert(decode(&im) == CODEC_INVALID);
}

static void test_cmyk(void)
{
    unsigned depth, x, y, c;
    for (depth = 8; depth <= 16; depth += 8) {
        struct psd_image im;
        simple(1, 4, 11, 3, depth, 4, depth == 8, value);
        assert(decode(&im) == CODEC_OK);
        for (y = 0; y < 3; y++)
            for (x = 0; x < 11; x++) {
                uint64_t k = value(3, x, y, depth) * (depth == 8 ? 257u : 1u);
                for (c = 0; c < 3; c++) {
                    uint64_t v = value(c, x, y, depth) * (depth == 8 ? 257u : 1u);
                    assert(at(&im, x, y)[c] ==
                           to8((unsigned)((v * k + 32767u) / 65535u), 16));
                }
            }
        psd_free(&im);
    }
}

static unsigned lab_value;
static unsigned lab(unsigned c, unsigned x, unsigned y, unsigned depth)
{
    static const unsigned grey[3] = { 0, 128, 128 };
    (void)y;
    if (x == 0)                 /* black */
        return depth == 16 ? grey[c] * 256u : grey[c];
    if (x == 1)                 /* white */
        return depth == 16 ? (c ? 32768u : 65535u) : (c ? 128u : 255u);
    if (x == 2)                 /* L* 50, neutral */
        return depth == 16 ? (c ? 32768u : 32768u) : (c ? 128u : 128u);
    return lab_value;           /* strongly out of gamut */
}

static void test_lab(void)
{
    unsigned depth;
    for (depth = 8; depth <= 16; depth += 8) {
        struct psd_image im;
        const uint8_t *p;
        lab_value = 0;
        simple(1, 3, 4, 1, depth, 9, 0, lab);
        assert(decode(&im) == CODEC_OK);
        p = at(&im, 0, 0);
        assert(p[0] == 0 && p[1] == 0 && p[2] == 0);
        p = at(&im, 1, 0);
        assert(p[0] == 255 && p[1] == 255 && p[2] == 255);
        /* L* 50.2 at 8 bits and 50.0 at 16 are both sRGB 119, neutral. */
        p = at(&im, 2, 0);
        assert(p[0] == 119 && p[1] == 119 && p[2] == 119);
        p = at(&im, 3, 0);
        assert(p[3] == 255);
        psd_free(&im);
    }
}

static void test_multichannel(void)
{
    struct psd_image im;
    unsigned x;
    simple(1, 2, 5, 1, 8, 7, 1, value);
    assert(decode(&im) == CODEC_OK);
    for (x = 0; x < 5; x++)
        assert(at(&im, x, 0)[0] == value(0, x, 0, 8) &&
               at(&im, x, 0)[2] == value(0, x, 0, 8) &&
               at(&im, x, 0)[3] == 255);
    psd_free(&im);
    simple(1, 4, 5, 1, 16, 7, 0, value);
    assert(decode(&im) == CODEC_OK);
    for (x = 0; x < 5; x++)
        assert(at(&im, x, 0)[1] == to8(value(1, x, 0, 16), 16) &&
               at(&im, x, 0)[2] == to8(value(2, x, 0, 16), 16) &&
               at(&im, x, 0)[3] == 255);
    psd_free(&im);
}

/* Straight RGBA: channel 3 is alpha. */
static unsigned straight(unsigned c, unsigned x, unsigned y, unsigned depth)
{
    unsigned a = x * 51u, v;
    (void)y;
    if (c == 3)
        return depth == 16 ? a * 257u : a;
    v = c == 0 ? 0u : c == 1 ? 90u : 250u;
    return depth == 16 ? v * 257u : v;
}

/* The same colours blended with white, as Photoshop writes them. The
   alphas are multiples of 51 so that the blend is exact at 8 bits. */
static unsigned matted(unsigned c, unsigned x, unsigned y, unsigned depth)
{
    unsigned a = x * 51u, v;
    if (c == 3 || a == 0)
        return straight(c, x, y, depth);
    v = (straight(c, x, y, 8) * a + 255u * (255u - a) + 127u) / 255u;
    return depth == 16 ? v * 257u : v;
}

/* Channel names in resource 1006, then a layer section whose count is
   count, and the four-channel composite. */
static void layered(unsigned version, int names, int count, sample_fn *f)
{
    size_t mark;
    int wide = version == 2;

    header(version, 4, 5, 2, 8, 3);
    put32(0);
    mark = size;
    put32(0);
    resource_header(1005, 3);            /* odd length, padded */
    put("abc", 3);
    put8(0);
    if (names) {
        resource_header(1006, 8);
        put("\x07" "Alpha 1", 8);
    }
    patch32(mark, (uint32_t)(size - mark - 4));
    put_length(wide, wide ? 16u : 12u);
    put_length(wide, 4);
    put16((unsigned)count & 65535u);
    put16(0);                            /* padding */
    put32(0);                            /* global mask */
    image_data(version, 4, 5, 2, 8, 1, f);
}

static void check_alpha(const struct psd_image *im, int alpha, sample_fn *f)
{
    unsigned x, c;
    for (x = 0; x < 5; x++) {
        const uint8_t *p = at(im, x, 1);
        assert(p[3] == (alpha ? x * 51u : 255u));
        if (!alpha || x == 0)
            continue;
        for (c = 0; c < 3; c++)
            assert(p[c] == f(c, x, 1, 8));
    }
}

static void test_alpha(void)
{
    struct psd_image im;
    unsigned version;

    for (version = 1; version <= 2; version++) {
        /* A negative count marks merged transparency; undo the matte. */
        layered(version, 1, -1, matted);
        assert(decode(&im) == CODEC_OK);
        check_alpha(&im, 1, straight);
        psd_free(&im);
        /* Straight colour that can't have been matted stays as it is. */
        layered(version, 1, -1, straight);
        assert(decode(&im) == CODEC_OK);
        check_alpha(&im, 1, straight);
        psd_free(&im);
        /* A named channel with a positive count is a saved selection. */
        layered(version, 1, 1, matted);
        assert(decode(&im) == CODEC_OK);
        check_alpha(&im, 0, matted);
        psd_free(&im);
        /* An unnamed extra channel is transparency. */
        layered(version, 0, 1, straight);
        assert(decode(&im) == CODEC_OK);
        check_alpha(&im, 1, straight);
        psd_free(&im);
    }
    /* No layers: the channel names decide. */
    simple(1, 4, 5, 2, 8, 3, 0, straight);
    assert(decode(&im) == CODEC_OK);
    check_alpha(&im, 1, straight);
    psd_free(&im);
}

/* 16-bit Photoshop files keep layers in an Lr16 block; Mt16 also marks
   merged transparency. Blocks are padded to 4 bytes. */
static void tagged(unsigned version, const char *key, int count, int pad)
{
    int wide = version == 2;
    size_t start;

    header(version, 4, 5, 2, 16, 3);
    put32(0);
    put32(20);
    resource_header(1006, 8);
    put("\x07" "Alpha 1", 8);
    start = size;
    put_length(wide, 0);                 /* patched below */
    put_length(wide, 0);                 /* empty layer info */
    put32(0);                            /* global mask */
    put("8BIMlrFX", 8);                  /* padded, with or without saying */
    put32(pad ? 2u : 4u);
    put32(0);
    put("8BIM", 4);
    put(key, 4);
    put_length(wide, 4);
    put16((unsigned)count & 65535u);
    put16(0);
    patch32(start + (wide ? 4u : 0u),
            (uint32_t)(size - start - (wide ? 8u : 4u)));
    image_data(version, 4, 5, 2, 16, 0, matted);
}

static void test_tagged(void)
{
    struct psd_image im;
    unsigned version, pad;
    for (version = 1; version <= 2; version++)
        for (pad = 0; pad < 2; pad++) {
            tagged(version, "Lr16", -1, (int)pad);
            assert(decode(&im) == CODEC_OK);
            check_alpha(&im, 1, straight);
            psd_free(&im);
            tagged(version, "Mt16", 1, (int)pad);
            assert(decode(&im) == CODEC_OK);
            check_alpha(&im, 1, straight);
            psd_free(&im);
            tagged(version, "Lr16", 1, (int)pad);
            assert(decode(&im) == CODEC_OK);
            check_alpha(&im, 0, matted);
            psd_free(&im);
        }
}

/* Spot channels past the alpha are ignored. */
static void test_spot(void)
{
    struct psd_image im;
    size_t mark;
    header(1, 6, 5, 2, 8, 3);
    put32(0);
    mark = size;
    put32(0);
    resource_header(1006, 24);
    put("\x07" "Alpha 1" "\x07" "Spot  1" "\x07" "Spot  2", 24);
    patch32(mark, (uint32_t)(size - mark - 4));
    put32(0);
    image_data(1, 6, 5, 2, 8, 1, matted);
    assert(decode(&im) == CODEC_OK);
    check_alpha(&im, 0, matted);
    psd_free(&im);
}

static void test_no_composite(void)
{
    struct psd_image im;
    size_t mark;
    unsigned real;
    for (real = 0; real < 2; real++) {
        header(1, 3, 2, 2, 8, 3);
        put32(0);
        mark = size;
        put32(0);
        resource_header(1057, 5);
        put32(1);
        put8(real);
        put8(0);
        patch32(mark, (uint32_t)(size - mark - 4));
        put32(0);
        image_data(1, 3, 2, 2, 8, 0, value);
        assert(decode(&im) == (real ? CODEC_OK : CODEC_INVALID));
        psd_free(&im);
    }
}

static void test_invalid_headers(void)
{
    static const struct { unsigned version, channels, depth, mode; } bad[] = {
        { 3, 3, 8, 3 }, { 0, 3, 8, 3 }, { 1, 0, 8, 3 }, { 1, 57, 8, 3 },
        { 1, 3, 32, 3 }, { 1, 1, 32, 1 }, { 1, 2, 8, 3 }, { 1, 3, 4, 3 },
        { 1, 1, 16, 2 }, { 1, 1, 16, 0 }, { 1, 1, 8, 5 }, { 1, 1, 8, 6 },
        { 1, 3, 8, 10 }, { 1, 3, 1, 3 }, { 1, 3, 8, 4 },
    };
    struct psd_image im;
    size_t i;

    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        simple(bad[i].version, bad[i].channels, 2, 2,
               bad[i].depth == 32 ? 8 : bad[i].depth, bad[i].mode, 0, value);
        file[22] = (uint8_t)(bad[i].depth >> 8);
        file[23] = (uint8_t)bad[i].depth;
        assert(decode(&im) == CODEC_INVALID);
        assert(im.rgba == NULL);
    }
    simple(1, 3, 2, 2, 8, 3, 0, value);
    file[0] = '7';
    assert(decode(&im) == CODEC_INVALID);
    /* Zero or oversized dimensions. */
    header(1, 3, 0, 2, 8, 3);
    assert(decode(&im) == CODEC_INVALID);
    header(1, 3, 2, 0, 8, 3);
    assert(decode(&im) == CODEC_INVALID);
    header(2, 3, 65536, 1, 8, 3);
    assert(decode(&im) == CODEC_TOO_LARGE);
    header(2, 3, 1, 300000, 8, 3);
    assert(decode(&im) == CODEC_TOO_LARGE);
    header(1, 3, 4097, 4097, 8, 3);
    assert(decode(&im) == CODEC_TOO_LARGE);
    header(1, 3, 4096, 4096, 8, 3);
    assert(decode(&im) == CODEC_TRUNCATED);
    /* ZIP composites, with or without prediction, and unknown methods. */
    for (i = 2; i < 5; i++) {
        simple(1, 3, 2, 2, 8, 3, 0, value);
        file[size - 12 - 1] = (uint8_t)i;
        assert(decode(&im) == CODEC_INVALID);
    }
}

/* Every prefix of a valid file is truncated, whichever part it cuts. */
static void truncate_all(void)
{
    struct psd_image im;
    size_t n, full = size;
    assert(decode(&im) == CODEC_OK);
    psd_free(&im);
    for (n = 0; n < full; n++) {
        assert(decode_n(n, &im) == CODEC_TRUNCATED);
        assert(im.rgba == NULL);
    }
}

static void test_truncation(void)
{
    unsigned version, rle;
    for (version = 1; version <= 2; version++) {
        layered(version, 1, -1, matted);
        truncate_all();
        tagged(version, "Lr16", -1, 1);
        truncate_all();
        for (rle = 0; rle < 2; rle++) {
            simple(version, 3, 7, 3, 16, 3, (int)rle, value);
            truncate_all();
        }
    }
    header(1, 1, 7, 3, 8, 2);
    put32(768);
    size += 768;
    put32(0);
    put32(0);
    image_data(1, 1, 7, 3, 8, 1, value);
    truncate_all();
}

/* Hand-made RLE rows: a run past the end is clipped, trailing bytes in a
   row's count are skipped, and a row that runs out is invalid. */
static void rle_file(const uint8_t *row0, unsigned n0, const uint8_t *row1,
                     unsigned n1)
{
    header(1, 1, 4, 2, 8, 1);
    put32(0);
    put32(0);
    put32(0);
    put16(1);
    put16(n0);
    put16(n1);
    put(row0, n0);
    put(row1, n1);
}

static void test_rle(void)
{
    static const uint8_t overrun[] = { 0xfb, 9 };          /* 6 of 9 */
    static const uint8_t mixed[] = { 0x80, 0x01, 1, 2, 0xff, 3, 0xaa };
    static const uint8_t lit_over[] = { 0x05, 1, 2, 3, 4, 5, 6 };
    static const uint8_t lit_short[] = { 0x03, 1, 2, 3 };
    static const uint8_t short_run[] = { 0xfe };
    static const uint8_t empty[] = { 0 };
    struct psd_image im;
    unsigned x;

    rle_file(overrun, 2, mixed, 7);
    assert(decode(&im) == CODEC_OK);
    for (x = 0; x < 4; x++)
        assert(at(&im, x, 0)[0] == 9);
    assert(at(&im, 0, 1)[0] == 1 && at(&im, 1, 1)[0] == 2 &&
           at(&im, 2, 1)[0] == 3 && at(&im, 3, 1)[0] == 3);
    psd_free(&im);
    rle_file(mixed, 7, overrun, 2);
    assert(decode(&im) == CODEC_OK);
    assert(at(&im, 3, 0)[0] == 3 && at(&im, 0, 1)[0] == 9);
    psd_free(&im);
    rle_file(lit_over, 7, overrun, 2);
    assert(decode(&im) == CODEC_OK);
    assert(at(&im, 3, 0)[0] == 4);
    psd_free(&im);
    rle_file(lit_short, 4, overrun, 2);
    assert(decode(&im) == CODEC_INVALID);
    rle_file(overrun, 2, short_run, 1);
    assert(decode(&im) == CODEC_INVALID);
    rle_file(overrun, 2, empty, 0);
    assert(decode(&im) == CODEC_INVALID);
    rle_file(lit_over, 6, overrun, 2);       /* literal past the count */
    assert(decode(&im) == CODEC_INVALID);
    /* A count past the end of the file. */
    rle_file(overrun, 2, overrun, 2);
    file[size - 5] = 9;
    assert(decode(&im) == CODEC_TRUNCATED);
}

/* Malformed resources and layer sections don't stop the composite. */
static void test_forgiving(void)
{
    struct psd_image im;
    size_t mark;
    int variant;

    for (variant = 0; variant < 5; variant++) {
        header(1, 4, 5, 2, 8, 3);
        put32(0);
        mark = size;
        put32(0);
        resource_header(1006, 8);
        put("\x07" "Alpha 1", 8);
        if (variant == 0) {
            resource_header(1047, 9999);  /* longer than the section */
            put16(0);
        } else if (variant == 1) {
            put("8BIM\x04", 5);           /* cut short */
        }
        patch32(mark, (uint32_t)(size - mark - 4));
        mark = size;
        put32(0);
        if (variant == 2) {
            put32(9999);                  /* layer info past the section */
            put16(0xffffu);
        } else if (variant == 3) {
            put32(0);
            put32(9999);                  /* global mask past the section */
            put("8BIMMt16", 8);
            put32(0);
        } else if (variant == 4) {
            put32(0);
            put32(0);
            put("8BIMLr16", 8);
            put32(9999);                  /* block past the section */
            put16(0xffffu);
        }
        patch32(mark, (uint32_t)(size - mark - 4));
        image_data(1, 4, 5, 2, 8, 0, matted);
        assert(decode(&im) == CODEC_OK);
        check_alpha(&im, 0, matted);
        psd_free(&im);
    }
    /* A source that reads less than it promised. */
    simple(1, 3, 40, 40, 8, 3, 1, value);
    short_read = size - 40;
    assert(decode(&im) == CODEC_TRUNCATED);
    short_read = 30;
    assert(decode(&im) == CODEC_TRUNCATED);
    short_read = (size_t)-1;
}

/* The composite streams through buffers instead of one read per row. */
static void test_reads(void)
{
    struct psd_image im;
    simple(1, 3, 200, 100, 8, 3, 1, value);
    reads = 0;
    assert(decode(&im) == CODEC_OK);
    assert(reads < 60);
    psd_free(&im);
}

int main(void)
{
    test_rgb();
    test_gray();
    test_bitmap();
    test_indexed();
    test_cmyk();
    test_lab();
    test_multichannel();
    test_alpha();
    test_tagged();
    test_spot();
    test_no_composite();
    test_invalid_headers();
    test_truncation();
    test_rle();
    test_forgiving();
    test_reads();
    return 0;
}
