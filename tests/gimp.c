#include "../formats/gimp/gimp.h"
#include "../formats/gimp/encode.h"
#include "../common/zlib.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t file[1 << 21];
static size_t file_length;

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void add(const void *data, size_t n)
{
    memcpy(file + file_length, data, n);
    file_length += n;
}

static void add32(uint32_t v)
{
    put32(file + file_length, v);
    file_length += 4;
}

static void reset(void)
{
    memset(file, 0xee, sizeof file);
    file_length = 0;
}

/* A version 2 brush named name, pixels width * height * bytes. */
static void add_brush(uint32_t w, uint32_t h, uint32_t bytes, const char *name,
                      const uint8_t *pixels)
{
    add32((uint32_t)(28 + strlen(name) + 1));
    add32(2);
    add32(w);
    add32(h);
    add32(bytes);
    add("GIMP", 4);
    add32(25);
    add(name, strlen(name) + 1);
    add(pixels, (size_t)w * h * bytes);
}

static void add_pattern(uint32_t w, uint32_t h, uint32_t bytes, const char *name,
                        const uint8_t *pixels)
{
    add32((uint32_t)(24 + strlen(name) + 1));
    add32(1);
    add32(w);
    add32(h);
    add32(bytes);
    add("GPAT", 4);
    add(name, strlen(name) + 1);
    add(pixels, (size_t)w * h * bytes);
}

static enum codec_result decode(long index, struct gimp_image *image, unsigned *count)
{
    return gimp_decode(file, file_length, index, image, count);
}

static void expect_pixel(const struct gimp_image *image, unsigned x, unsigned y,
                         uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    const uint8_t *p = image->rgba + ((size_t)y * image->width + x) * 4u;
    if (p[0] != r || p[1] != g || p[2] != b || p[3] != a) {
        fprintf(stderr, "pixel %u,%u is %u %u %u %u, expected %u %u %u %u\n",
                x, y, p[0], p[1], p[2], p[3], r, g, b, a);
        assert(0);
    }
}

static void expect_fail(enum codec_result expected)
{
    struct gimp_image image;
    unsigned count;
    enum codec_result result = decode(0, &image, &count);
    if (result != expected) {
        fprintf(stderr, "result %d, expected %d\n", result, expected);
        assert(0);
    }
    assert(image.rgba == NULL);
}

/* Every prefix of the current file shorter than limit fails, and none reads
   out of bounds. */
static void expect_prefixes_fail_below(size_t limit)
{
    size_t full = file_length, n;
    uint8_t *copy = malloc(full);
    assert(copy != NULL);
    memcpy(copy, file, full);
    for (n = 0; n < full; n++) {
        struct gimp_image image;
        unsigned count;
        uint8_t *prefix = malloc(n ? n : 1);
        assert(prefix != NULL);
        memcpy(prefix, copy, n);
        if (n < limit) {
            assert(gimp_decode(prefix, n, 0, &image, &count) != CODEC_OK);
            assert(image.rgba == NULL);
        } else if (gimp_decode(prefix, n, 0, &image, &count) == CODEC_OK) {
            gimp_free(&image);
        }
        free(prefix);
    }
    free(copy);
}

static void expect_prefixes_fail(void)
{
    expect_prefixes_fail_below(file_length);
}

static void test_grey_brush(void)
{
    static const uint8_t mask[6] = {0, 255, 128, 1, 2, 3};
    struct gimp_image image;
    unsigned count;

    reset();
    add_brush(3, 2, 1, "grey", mask);
    assert(gimp_sniff(file, file_length) == GIMP_GBR);
    assert(decode(0, &image, &count) == CODEC_OK);
    assert(count == 1 && image.width == 3 && image.height == 2);
    /* GIMP opens a mask as black paint on white. */
    expect_pixel(&image, 0, 0, 255, 255, 255, 255);
    expect_pixel(&image, 1, 0, 0, 0, 0, 255);
    expect_pixel(&image, 2, 0, 127, 127, 127, 255);
    expect_pixel(&image, 2, 1, 252, 252, 252, 255);
    gimp_free(&image);
    assert(decode(1, &image, &count) == CODEC_INVALID && count == 1);
    assert(image.rgba == NULL);
    expect_prefixes_fail();

    /* Trailing bytes that aren't a pattern are ignored. */
    add("junk after the brush", 20);
    assert(decode(0, &image, &count) == CODEC_OK);
    expect_pixel(&image, 1, 0, 0, 0, 0, 255);
    gimp_free(&image);
}

static void test_colour_brush(void)
{
    static const uint8_t rgba[8] = {10, 20, 30, 0, 40, 50, 60, 200};
    struct gimp_image image;
    unsigned count;

    reset();
    add_brush(2, 1, 4, "", rgba);
    assert(decode(0, &image, &count) == CODEC_OK);
    expect_pixel(&image, 0, 0, 10, 20, 30, 0);
    expect_pixel(&image, 1, 0, 40, 50, 60, 200);
    gimp_free(&image);
    expect_prefixes_fail();
}

static void test_old_brush(void)
{
    static const uint8_t mask[4] = {0, 64, 128, 255};
    struct gimp_image image;
    unsigned count;

    reset();
    add32(20 + 4);
    add32(1);
    add32(2);
    add32(2);
    add32(1);
    add("old", 4);
    add(mask, 4);
    assert(gimp_sniff(file, file_length) == GIMP_GBR);
    assert(decode(0, &image, &count) == CODEC_OK);
    assert(image.width == 2 && image.height == 2);
    expect_pixel(&image, 1, 0, 191, 191, 191, 255);
    expect_pixel(&image, 1, 1, 0, 0, 0, 255);
    gimp_free(&image);
    expect_prefixes_fail();
}

/* GIMP 1.x pixmap brushes: a grey mask, then an RGB pattern of the same size. */
static void test_pixmap_brush(void)
{
    static const uint8_t mask[2] = {255, 100};
    static const uint8_t rgb[6] = {1, 2, 3, 4, 5, 6};
    struct gimp_image image;
    unsigned count;

    reset();
    add_brush(2, 1, 1, "gpb", mask);
    add_pattern(2, 1, 3, "gpb", rgb);
    assert(decode(0, &image, &count) == CODEC_OK);
    expect_pixel(&image, 0, 0, 1, 2, 3, 255);
    expect_pixel(&image, 1, 0, 4, 5, 6, 100);
    gimp_free(&image);
    /* A cut-short colour pattern is a truncated file. */
    file_length -= 1;
    expect_fail(CODEC_TRUNCATED);

    /* A pattern of another size isn't the brush's colour. */
    reset();
    add_brush(2, 1, 1, "gpb", mask);
    add_pattern(1, 2, 3, "gpb", rgb);
    assert(decode(0, &image, &count) == CODEC_OK);
    expect_pixel(&image, 0, 0, 0, 0, 0, 255);
    gimp_free(&image);
}

static void test_bad_brushes(void)
{
    static const uint8_t pixels[128] = {0};

    reset();
    add_brush(2, 2, 3, "three bytes", pixels);
    expect_fail(CODEC_INVALID);
    reset();
    add_brush(2, 2, 18, "cinepaint", pixels);
    put32(file + 4, 3);
    expect_fail(CODEC_INVALID);
    reset();
    add_brush(0, 2, 1, "empty", pixels);
    expect_fail(CODEC_INVALID);
    reset();
    add_brush(2, 2, 1, "big", pixels);
    put32(file + 8, 70000);
    expect_fail(CODEC_TOO_LARGE);
    reset();
    add_brush(2, 2, 1, "big", pixels);
    put32(file + 8, 5000);
    put32(file + 12, 5000);
    expect_fail(CODEC_TOO_LARGE);
    reset();
    add_brush(2, 2, 1, "huge", pixels);
    put32(file + 8, 0xffffffffu);
    put32(file + 12, 0xffffffffu);
    expect_fail(CODEC_TOO_LARGE);
    reset();
    add_brush(2, 2, 1, "short header", pixels);
    put32(file, 27);
    expect_fail(CODEC_INVALID);
    reset();
    add_brush(2, 2, 1, "long header", pixels);
    put32(file, 0xfffffff0u);
    expect_fail(CODEC_TRUNCATED);
    reset();
    add_brush(2, 2, 1, "bad magic", pixels);
    file[20] = 'g';
    expect_fail(CODEC_INVALID);
}

static void test_pipe(void)
{
    static const uint8_t grey[4] = {0, 255, 0, 255};
    static const uint8_t rgba[4] = {9, 8, 7, 6};
    static const uint8_t mask[2] = {255, 0};
    static const uint8_t rgb[6] = {1, 2, 3, 4, 5, 6};
    static const char header[] =
        "Pipe name\n3 ncells:3 cellwidth:2 cellheight:2 step:100 dim:1 "
        "cols:1 rows:1 placement:constant rank0:3 sel0:random\n";
    struct gimp_image image;
    unsigned count;
    size_t full, pattern;

    reset();
    add(header, sizeof header - 1);
    add_brush(2, 2, 1, "one", grey);
    add_brush(1, 1, 4, "two", rgba);
    add_brush(2, 1, 1, "three", mask);
    pattern = file_length;
    add_pattern(2, 1, 3, "three", rgb);
    full = file_length;
    assert(gimp_sniff(file, file_length) == GIMP_GIH);
    assert(decode(0, &image, &count) == CODEC_OK);
    assert(count == 3 && image.width == 2 && image.height == 2);
    expect_pixel(&image, 0, 0, 255, 255, 255, 255);
    expect_pixel(&image, 1, 0, 0, 0, 0, 255);
    gimp_free(&image);
    assert(decode(1, &image, &count) == CODEC_OK);
    assert(image.width == 1 && image.height == 1);
    expect_pixel(&image, 0, 0, 9, 8, 7, 6);
    gimp_free(&image);
    assert(decode(2, &image, &count) == CODEC_OK);
    expect_pixel(&image, 0, 0, 1, 2, 3, 255);
    expect_pixel(&image, 1, 0, 4, 5, 6, 0);
    gimp_free(&image);
    assert(decode(3, &image, &count) == CODEC_INVALID && count == 3);
    assert(image.rgba == NULL);
    assert(decode(-1, &image, &count) == CODEC_INVALID && count == 3);
    /* Until its header is complete, a cut-off colour pattern looks like
       bytes after the last brush, which GIMP ignores too. */
    expect_prefixes_fail_below(pattern);

    /* More cells promised than stored. */
    file[sizeof "Pipe name"] = '4';
    expect_fail(CODEC_TRUNCATED);
    file[sizeof "Pipe name"] = '0';
    expect_fail(CODEC_INVALID);
    file[sizeof "Pipe name"] = 'x';
    expect_fail(CODEC_INVALID);
    file[sizeof "Pipe name"] = '3';
    file_length = full;

    /* A number too large to be real doesn't overflow. */
    reset();
    add("n\n99999999999999999999\n", 24);
    add_brush(2, 2, 1, "one", grey);
    expect_fail(CODEC_TRUNCATED);

    /* A name line longer than GIMP reads. */
    reset();
    memset(file, 'a', 2000);
    file_length = 2000;
    add("\n1\n", 3);
    add_brush(2, 2, 1, "one", grey);
    expect_fail(CODEC_INVALID);
}

static void test_patterns(void)
{
    static const uint8_t px[16] = {10, 20, 30, 40, 50, 60, 70, 80,
                                   90, 100, 110, 120, 130, 140, 150, 160};
    struct gimp_image image;
    unsigned count;

    reset();
    add_pattern(2, 1, 1, "grey", px);
    assert(gimp_sniff(file, file_length) == GIMP_PAT);
    assert(decode(0, &image, &count) == CODEC_OK && count == 1);
    expect_pixel(&image, 0, 0, 10, 10, 10, 255);
    expect_pixel(&image, 1, 0, 20, 20, 20, 255);
    gimp_free(&image);
    expect_prefixes_fail();

    reset();
    add_pattern(2, 1, 2, "grey alpha", px);
    assert(decode(0, &image, &count) == CODEC_OK);
    expect_pixel(&image, 0, 0, 10, 10, 10, 20);
    expect_pixel(&image, 1, 0, 30, 30, 30, 40);
    gimp_free(&image);

    reset();
    add_pattern(2, 1, 3, "rgb", px);
    assert(decode(0, &image, &count) == CODEC_OK);
    expect_pixel(&image, 1, 0, 40, 50, 60, 255);
    gimp_free(&image);

    reset();
    add_pattern(1, 2, 4, "rgba", px);
    assert(decode(0, &image, &count) == CODEC_OK);
    assert(image.width == 1 && image.height == 2);
    expect_pixel(&image, 0, 1, 50, 60, 70, 80);
    gimp_free(&image);
    expect_prefixes_fail();

    reset();
    add_pattern(2, 1, 5, "five", px);
    expect_fail(CODEC_INVALID);
    reset();
    add_pattern(2, 1, 1, "v2", px);
    put32(file + 4, 2);
    expect_fail(CODEC_INVALID);
    reset();
    add_pattern(2, 1, 1, "no name", px);
    put32(file, 24);
    expect_fail(CODEC_INVALID);
    reset();
    add_pattern(2, 1, 1, "wide", px);
    put32(file + 8, 65536);
    expect_fail(CODEC_TOO_LARGE);

    /* Photoshop patterns share the extension but not the format. */
    reset();
    add("8BPT", 4);
    add32(1);
    add32(0);
    add(px, 16);
    expect_fail(CODEC_INVALID);
}

static void test_unknown(void)
{
    struct gimp_image image;
    unsigned count;

    reset();
    add("hello", 5);
    expect_fail(CODEC_TRUNCATED);
    reset();
    memset(file, 0, 64);
    file_length = 64;
    expect_fail(CODEC_INVALID);
    assert(gimp_decode(NULL, 0, 0, &image, &count) == CODEC_TRUNCATED);
    assert(gimp_decode(file, 64, 0, NULL, &count) == CODEC_INVALID);
}

/* Encode rows of rgba with fn, after the header, into file. */
static void encode(const uint8_t *rgba, unsigned w, unsigned h, unsigned bytes,
                   void (*fn)(const uint8_t *, unsigned, unsigned, uint8_t *))
{
    unsigned y;
    for (y = 0; y < h; y++) {
        fn(rgba + (size_t)y * w * 4u, w, bytes, file + file_length);
        file_length += (size_t)w * bytes;
    }
}

static unsigned scan(const uint8_t *rgba, unsigned w, unsigned h)
{
    unsigned flags = 0, y;
    for (y = 0; y < h; y++)
        gimp_scan_row(rgba + (size_t)y * w * 4u, w, &flags);
    return flags;
}

static void expect_round_trip(const uint8_t *rgba, unsigned w, unsigned h,
                              enum gimp_kind kind)
{
    struct gimp_image image;
    unsigned count;
    assert(gimp_sniff(file, file_length) == kind);
    assert(decode(0, &image, &count) == CODEC_OK && count == 1);
    assert(image.width == w && image.height == h);
    assert(memcmp(image.rgba, rgba, (size_t)w * h * 4u) == 0);
    gimp_free(&image);
}

static void test_writers(void)
{
    static const uint8_t grey[16] = {0, 0, 0, 255, 90, 90, 90, 255,
                                     200, 200, 200, 255, 255, 255, 255, 255};
    static const uint8_t grey_alpha[16] = {0, 0, 0, 0, 90, 90, 90, 255,
                                           200, 200, 200, 7, 255, 255, 255, 255};
    static const uint8_t rgb[16] = {1, 2, 3, 255, 4, 5, 6, 255,
                                    7, 8, 9, 255, 10, 11, 12, 255};
    static const uint8_t rgba[16] = {1, 2, 3, 0, 4, 5, 6, 60,
                                     7, 8, 9, 255, 10, 11, 12, 128};
    static const uint8_t *pictures[4] = {grey, grey_alpha, rgb, rgba};
    char long_name[400];
    unsigned i, bytes;

    assert(scan(grey, 2, 2) == 0);
    assert(scan(grey_alpha, 2, 2) == GIMP_SCAN_ALPHA);
    assert(scan(rgb, 2, 2) == GIMP_SCAN_COLOUR);
    assert(scan(rgba, 2, 2) == (GIMP_SCAN_ALPHA | GIMP_SCAN_COLOUR));
    assert(gbr_bytes(0) == 1 && gbr_bytes(GIMP_SCAN_ALPHA) == 4);
    assert(pat_bytes(0) == 1 && pat_bytes(GIMP_SCAN_ALPHA) == 2);
    assert(pat_bytes(GIMP_SCAN_COLOUR) == 3);
    assert(pat_bytes(GIMP_SCAN_ALPHA | GIMP_SCAN_COLOUR) == 4);

    for (i = 0; i < 4; i++) {
        const uint8_t *p = pictures[i];
        unsigned flags = scan(p, 2, 2);

        reset();
        bytes = gbr_bytes(flags);
        file_length = gbr_make_header(2, 2, bytes, "brush", file);
        encode(p, 2, 2, bytes, gbr_encode_row);
        expect_round_trip(p, 2, 2, GIMP_GBR);

        reset();
        file_length = gih_make_header(2, 2, bytes, "pipe\nline", file);
        encode(p, 2, 2, bytes, gbr_encode_row);
        expect_round_trip(p, 2, 2, GIMP_GIH);

        reset();
        bytes = pat_bytes(flags);
        file_length = pat_make_header(2, 2, bytes, NULL, file);
        encode(p, 2, 2, bytes, pat_encode_row);
        expect_round_trip(p, 2, 2, GIMP_PAT);
    }
    memset(long_name, 'n', sizeof long_name - 1);
    long_name[sizeof long_name - 1] = '\0';
    reset();
    file_length = gih_make_header(2, 2, 4, long_name, file);
    assert(file_length > 0 && file_length <= GIMP_ENCODED_HEADER);
    encode(rgba, 2, 2, 4, gbr_encode_row);
    expect_round_trip(rgba, 2, 2, GIMP_GIH);
    assert(gbr_make_header(0, 2, 1, "x", file) == 0);
    assert(pat_make_header(65536, 2, 1, "x", file) == 0);
    assert(gih_make_header(2, 0, 1, "x", file) == 0);
}

/* ---- XCF ---- */

struct xlayer {
    unsigned w, h, type;   /* 0 RGB, 1 RGBA, 2 grey, 3 grey alpha, 4, 5 indexed */
    long x, y;
    int opacity;           /* 0..255, or -1 for no property */
    int mode;              /* -1: no property */
    int visible;           /* -1: no property */
    int group;
    unsigned path[48], depth;
    const uint8_t *px;     /* w * h * channels * bpc, big-endian */
    const uint8_t *mask;   /* w * h * bpc, or NULL */
    int apply, show;
    int effect;            /* -1 none, else the visibility of one effect */
    int floats_on;         /* the layer (index) it floats on, or -1 */
};

static int xcf_version, xcf_compression;
static unsigned xcf_bpc;

static void xlayer_init(struct xlayer *l, unsigned w, unsigned h, unsigned type,
                        const uint8_t *px)
{
    memset(l, 0, sizeof *l);
    l->w = w;
    l->h = h;
    l->type = type;
    l->px = px;
    l->opacity = 255;
    l->mode = 28;
    l->visible = 1;
    l->apply = 1;
    l->effect = -1;
    l->floats_on = -1;
}

static unsigned offset_size(void)
{
    return xcf_version >= 11 ? 8u : 4u;
}

static void put_offset(uint8_t *p, size_t v)
{
    if (xcf_version >= 11) {
        put32(p, 0);
        put32(p + 4, (uint32_t)v);
    } else {
        put32(p, (uint32_t)v);
    }
}

static size_t add_offset(size_t v)
{
    size_t at = file_length;
    put_offset(file + at, v);
    file_length += offset_size();
    return at;
}

static void add_prop(uint32_t type, uint32_t size, const uint32_t *values)
{
    uint32_t i;
    add32(type);
    add32(size);
    for (i = 0; i < size / 4; i++)
        add32(values[i]);
}

static void add_prop32(uint32_t type, uint32_t value)
{
    add_prop(type, 4, &value);
}

/* RLE per byte plane: repeats of three or more as runs (long runs in the
   16-bit form), the rest as literals. */
static void add_rle(const uint8_t *tile, unsigned pixels, unsigned bpp)
{
    unsigned plane, i, n;
    for (plane = 0; plane < bpp; plane++) {
        i = 0;
        while (i < pixels) {
            uint8_t v = tile[(size_t)i * bpp + plane];
            for (n = 1; i + n < pixels && tile[(size_t)(i + n) * bpp + plane] == v; n++)
                ;
            if (n >= 3) {
                if (n < 128) {
                    file[file_length++] = (uint8_t)(n - 1);
                } else {
                    file[file_length++] = 127;
                    file[file_length++] = (uint8_t)(n >> 8);
                    file[file_length++] = (uint8_t)n;
                }
                file[file_length++] = v;
                i += n;
                continue;
            }
            for (n = 0; i + n < pixels && n < 127; n++) {
                unsigned k = i + n;
                if (k + 2 < pixels && tile[(size_t)k * bpp + plane] == tile[(size_t)(k + 1) * bpp + plane] &&
                    tile[(size_t)k * bpp + plane] == tile[(size_t)(k + 2) * bpp + plane])
                    break;
            }
            file[file_length++] = (uint8_t)(256 - n);
            while (n-- > 0)
                file[file_length++] = tile[(size_t)(i++) * bpp + plane];
        }
    }
}

/* A hierarchy, its level and the tiles for w * h pixels of bpp bytes. */
static void add_buffer(unsigned w, unsigned h, unsigned bpp, const uint8_t *px)
{
    static uint8_t tile[64 * 64 * 16];
    unsigned cols = (w + 63) / 64, rows = (h + 63) / 64, i, x, y;
    size_t table, level;

    add32(w);
    add32(h);
    add32(bpp);
    level = add_offset(0);
    add_offset(0);
    put_offset(file + level, file_length);
    add32(w);
    add32(h);
    table = file_length;
    for (i = 0; i <= cols * rows; i++)
        add_offset(0);
    for (i = 0; i < cols * rows; i++) {
        unsigned tx = i % cols, ty = i / cols;
        unsigned tw = w - tx * 64 < 64 ? w - tx * 64 : 64;
        unsigned th = h - ty * 64 < 64 ? h - ty * 64 : 64;
        size_t n = 0;
        for (y = 0; y < th; y++)
            for (x = 0; x < tw; x++) {
                memcpy(tile + n, px + ((size_t)(ty * 64 + y) * w + tx * 64 + x) * bpp, bpp);
                n += bpp;
            }
        put_offset(file + table + i * offset_size(), file_length);
        if (xcf_compression == 1) {
            add_rle(tile, tw * th, bpp);
        } else if (xcf_compression == 2) {
            size_t written;
            assert(zlib_deflate(tile, n, file + file_length, sizeof file - file_length,
                                -1, &written) == CODEC_OK);
            file_length += written;
        } else {
            add(tile, n);
        }
    }
}

static unsigned channels_of(unsigned type)
{
    return (type < 2 ? 3u : 1u) + (type & 1u);
}

/* Build an XCF of the given layers, top first. */
static void build_xcf(int version, unsigned w, unsigned h, unsigned base,
                      uint32_t precision, int compression,
                      const uint8_t *map, unsigned colours,
                      struct xlayer *layers, unsigned count)
{
    size_t table, starts[64], floats_at[64];
    unsigned i;
    char magic[16];

    reset();
    xcf_version = version;
    xcf_compression = compression;
    xcf_bpc = precision >= 300 ? 4 : precision >= 200 ? 2 : 1;
    if (version == 0)
        add("gimp xcf file", 14);
    else {
        sprintf(magic, "gimp xcf v%03d", version);
        add(magic, 14);
    }
    add32(w);
    add32(h);
    add32(base);
    if (version >= 4)
        add32(precision);
    add32(17);
    add32(1);
    file[file_length++] = (uint8_t)compression;
    if (colours > 0) {
        add32(1);
        add32(4 + colours * 3);
        add32(colours);
        add(map, colours * 3);
    }
    add32(0);
    add32(0);
    table = file_length;
    for (i = 0; i <= count; i++)
        add_offset(0);
    add_offset(0); /* no channels */
    for (i = 0; i < count; i++) {
        struct xlayer *l = &layers[i];
        size_t pointers;
        unsigned ch = channels_of(l->type);

        put_offset(file + table + i * offset_size(), file_length);
        starts[i] = file_length;
        floats_at[i] = 0;
        add32(l->w);
        add32(l->h);
        add32(l->type);
        add32(2);
        add("L", 2);
        if (l->opacity >= 0)
            add_prop32(6, (uint32_t)l->opacity);
        if (l->mode >= 0)
            add_prop32(7, (uint32_t)l->mode);
        if (l->visible >= 0)
            add_prop32(8, (uint32_t)l->visible);
        if (l->x != 0 || l->y != 0) {
            uint32_t xy[2];
            xy[0] = (uint32_t)l->x;
            xy[1] = (uint32_t)l->y;
            add_prop(15, 8, xy);
        }
        if (l->group)
            add_prop(29, 0, NULL);
        if (l->depth > 0)
            add_prop(30, l->depth * 4, l->path);
        if (l->mask != NULL) {
            add_prop32(11, (uint32_t)l->apply);
            add_prop32(13, (uint32_t)l->show);
        }
        if (l->floats_on >= 0) {
            add32(5);
            add32(offset_size());
            floats_at[i] = add_offset(0);
        }
        add32(0);
        add32(0);
        pointers = file_length;
        add_offset(0);
        add_offset(0);
        if (version >= 20) {
            add_offset(0);
            add_offset(0);
        }
        if (!l->group) {
            put_offset(file + pointers, file_length);
            add_buffer(l->w, l->h, ch * xcf_bpc, l->px);
        }
        if (l->mask != NULL) {
            size_t hier;
            put_offset(file + pointers + offset_size(), file_length);
            add32(l->w);
            add32(l->h);
            add32(2);
            add("M", 2);
            add32(0);
            add32(0);
            hier = add_offset(0);
            put_offset(file + hier, file_length);
            add_buffer(l->w, l->h, xcf_bpc, l->mask);
        }
        if (l->effect >= 0) {
            put_offset(file + pointers + 2 * offset_size(), file_length);
            add32(2);
            add("E", 2);
            add32(1);
            add("", 1);
            add32(9);
            add("gegl:foo", 9);
            if (version >= 22) {
                add32(2);
                add("1", 2);
            }
            add_prop32(8, (uint32_t)l->effect);
            add32(0);
            add32(0);
            add_offset(0);
        }
    }
    for (i = 0; i < count; i++)
        if (layers[i].floats_on >= 0)
            put_offset(file + floats_at[i], starts[layers[i].floats_on]);
}

static void expect_xcf(unsigned w, unsigned h, const uint8_t *rgba, int tolerance)
{
    struct gimp_image image;
    unsigned count, i;
    enum codec_result result = decode(0, &image, &count);
    if (result != CODEC_OK) {
        fprintf(stderr, "xcf result %d\n", result);
        assert(0);
    }
    assert(count == 1 && image.width == w && image.height == h);
    for (i = 0; i < w * h * 4; i++) {
        int d = image.rgba[i] - rgba[i];
        /* The colour of a transparent pixel doesn't matter. */
        if (rgba[i | 3] == 0 && (i & 3) != 3)
            continue;
        if (d < -tolerance || d > tolerance) {
            fprintf(stderr, "byte %u is %u, expected %u\n", i, image.rgba[i], rgba[i]);
            assert(0);
        }
    }
    gimp_free(&image);
}

static const uint8_t red_green[] = {255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 0,
                                    10, 20, 30, 255, 200, 100, 50, 255, 7, 8, 9, 1};

static void test_xcf_basics(void)
{
    static const int versions[] = {0, 3, 11, 12, 13, 20, 22, 25};
    struct xlayer l;
    unsigned v, c;

    for (v = 0; v < sizeof versions / sizeof *versions; v++)
        for (c = 0; c < 3; c++) {
            xlayer_init(&l, 3, 2, 1, red_green);
            build_xcf(versions[v], 3, 2, 0, 150, (int)c, NULL, 0, &l, 1);
            assert(gimp_sniff(file, file_length) == GIMP_XCF);
            expect_xcf(3, 2, red_green, 0);
        }
    /* Not the one picture there is. */
    {
        struct gimp_image image;
        unsigned count;
        assert(decode(1, &image, &count) == CODEC_INVALID && count == 1);
    }
    /* Opacity scales alpha; the bottom layer is drawn as Normal whatever
       its mode. */
    xlayer_init(&l, 3, 2, 1, red_green);
    l.opacity = 128;
    l.mode = 30;
    build_xcf(11, 3, 2, 0, 150, 1, NULL, 0, &l, 1);
    {
        uint8_t expect[24];
        unsigned i;
        memcpy(expect, red_green, sizeof expect);
        for (i = 3; i < 24; i += 4)
            expect[i] = (uint8_t)((red_green[i] * 128 + 127) / 255);
        expect_xcf(3, 2, expect, 1);
    }
    /* A layer with no properties is visible, opaque and Normal. */
    xlayer_init(&l, 3, 2, 1, red_green);
    l.opacity = l.mode = l.visible = -1;
    build_xcf(11, 3, 2, 0, 150, 0, NULL, 0, &l, 1);
    expect_xcf(3, 2, red_green, 0);
    /* Modes GIMP doesn't allow on layers fall back to Normal. */
    l.mode = 99;
    build_xcf(11, 3, 2, 0, 150, 0, NULL, 0, &l, 1);
    expect_xcf(3, 2, red_green, 0);
}

static void test_xcf_stack(void)
{
    static const uint8_t white[12] = {255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
    static const uint8_t black[9] = {0};
    static const uint8_t top[12] = {10, 20, 30, 200, 100, 50, 7, 8, 9, 0, 0, 0};
    static const uint8_t opaque_top[12] = {10, 20, 30, 200, 100, 50, 7, 8, 9, 99, 99, 99};
    static const uint8_t show_top[16] = {10, 20, 30, 255, 200, 100, 50, 255,
                                         7, 8, 9, 255, 1, 2, 3, 255};
    static const uint8_t half_mask[4] = {0, 255, 255, 0};
    struct xlayer l[4];
    uint8_t expect[16];
    unsigned i, mode;

    /* Normal, multiply and legacy multiply over white are the layer. */
    for (mode = 0; mode < 3; mode++) {
        xlayer_init(&l[0], 2, 2, 0, opaque_top);
        l[0].mode = mode == 0 ? 28 : mode == 1 ? 30 : 3;
        xlayer_init(&l[1], 2, 2, 0, white);
        build_xcf(11, 2, 2, 0, 150, 1, NULL, 0, l, 2);
        for (i = 0; i < 4; i++) {
            memcpy(expect + i * 4, opaque_top + i * 3, 3);
            expect[i * 4 + 3] = 255;
        }
        expect_xcf(2, 2, expect, 0);
    }
    /* Multiply over black is black. */
    xlayer_init(&l[0], 2, 2, 0, opaque_top);
    l[0].mode = 30;
    xlayer_init(&l[1], 3, 1, 0, black);
    l[1].x = -1;
    build_xcf(11, 2, 2, 0, 150, 1, NULL, 0, l, 2);
    memset(expect, 0, sizeof expect);
    expect[3] = expect[7] = 255;
    /* Clip to backdrop: nothing where the bottom layer isn't. */
    memcpy(expect + 8, "\0\0\0\0\0\0\0\0", 8);
    expect_xcf(2, 2, expect, 0);

    /* Invisible layers don't count. */
    xlayer_init(&l[0], 2, 2, 0, opaque_top);
    l[0].visible = 0;
    xlayer_init(&l[1], 2, 2, 0, white);
    build_xcf(11, 2, 2, 0, 150, 1, NULL, 0, l, 2);
    memset(expect, 255, 16);
    expect_xcf(2, 2, expect, 0);

    /* Offsets: only the part on the canvas shows. */
    xlayer_init(&l[0], 2, 2, 1, show_top);
    l[0].x = -1;
    l[0].y = -1;
    build_xcf(11, 2, 2, 0, 150, 2, NULL, 0, l, 1);
    memset(expect, 0, 16);
    memcpy(expect, show_top + 12, 4);
    expect_xcf(2, 2, expect, 0);
    l[0].x = 5;
    build_xcf(11, 2, 2, 0, 150, 2, NULL, 0, l, 1);
    memset(expect, 0, 16);
    expect_xcf(2, 2, expect, 0);

    /* Masks multiply alpha, can be switched off, or be shown instead. */
    xlayer_init(&l[0], 2, 2, 1, show_top);
    l[0].mask = half_mask;
    xlayer_init(&l[1], 2, 2, 0, white);
    build_xcf(11, 2, 2, 0, 150, 1, NULL, 0, l, 2);
    memset(expect, 255, 16);
    memcpy(expect + 4, show_top + 4, 8);
    expect_xcf(2, 2, expect, 0);
    l[0].apply = 0;
    build_xcf(11, 2, 2, 0, 150, 1, NULL, 0, l, 2);
    expect_xcf(2, 2, show_top, 0);
    l[0].show = 1;
    build_xcf(11, 2, 2, 0, 150, 1, NULL, 0, l, 2);
    for (i = 0; i < 4; i++) {
        memset(expect + i * 4, half_mask[i], 3);
        expect[i * 4 + 3] = 255;
    }
    expect_xcf(2, 2, expect, 0);
    (void)top;
}

static void test_xcf_groups(void)
{
    static const uint8_t white[12] = {255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
    static const uint8_t colour[16] = {10, 20, 30, 255, 200, 100, 50, 255,
                                       7, 8, 9, 255, 1, 2, 3, 0};
    static const uint8_t mask[4] = {255, 255, 0, 255};
    struct xlayer l[40];
    uint8_t expect[16];
    unsigned i, mode;

    /* Isolated and pass-through groups over white, holding a Multiply
       layer: the colour either way (drawn as Normal inside an isolated
       group, multiplied with white through a pass-through one). */
    for (mode = 0; mode < 2; mode++) {
        xlayer_init(&l[0], 0, 0, 1, NULL);
        l[0].group = 1;
        l[0].mode = mode ? 61 : 28;
        xlayer_init(&l[1], 2, 2, 1, colour);
        l[1].mode = 30;
        l[1].path[0] = 0;
        l[1].path[1] = 0;
        l[1].depth = 2;
        xlayer_init(&l[2], 2, 2, 0, white);
        build_xcf(11, 2, 2, 0, 150, 1, NULL, 0, l, 3);
        memcpy(expect, colour, 16);
        memset(expect + 12, 255, 4);
        expect_xcf(2, 2, expect, 0);
    }
    /* A group's mask is its own, sized to its children. */
    xlayer_init(&l[0], 2, 2, 1, NULL);
    l[0].group = 1;
    l[0].mask = mask;
    xlayer_init(&l[1], 2, 2, 1, colour);
    l[1].path[0] = 0;
    l[1].path[1] = 0;
    l[1].depth = 2;
    xlayer_init(&l[2], 2, 2, 0, white);
    build_xcf(11, 2, 2, 0, 150, 1, NULL, 0, l, 3);
    memcpy(expect, colour, 16);
    memset(expect + 8, 255, 8);
    expect_xcf(2, 2, expect, 0);
    /* An invisible group hides its children. */
    l[0].visible = 0;
    build_xcf(11, 2, 2, 0, 150, 1, NULL, 0, l, 3);
    memset(expect, 255, 16);
    expect_xcf(2, 2, expect, 0);
    /* Deep nesting, and nesting deeper than the decoder allows. */
    for (mode = 16; mode <= 40; mode += 24) {
        for (i = 0; i < mode; i++) {
            unsigned k;
            xlayer_init(&l[i], 2, 2, 1, i == mode - 1 ? colour : NULL);
            l[i].group = i < mode - 1;
            for (k = 0; k <= i; k++)
                l[i].path[k] = 0;
            l[i].depth = i + 1;
        }
        build_xcf(11, 2, 2, 0, 150, 0, NULL, 0, l, mode);
        if (mode == 16)
            expect_xcf(2, 2, colour, 0);
        else
            expect_fail(CODEC_INVALID);
    }
}

static void test_xcf_formats(void)
{
    static const uint8_t grey[4] = {0, 128, 255, 7};
    static const uint8_t indexed[8] = {0, 255, 1, 128, 5, 255, 1, 0};
    static const uint8_t map[6] = {255, 0, 0, 0, 255, 0};
    static const uint8_t deep[16] = {0x80, 0x80, 0xff, 0xff, 0x7f, 0x80, 0x00, 0x00,
                                     0x12, 0x34, 0x56, 0x78, 0xff, 0xff, 0x00, 0x01};
    static const uint8_t linear[3] = {0, 128, 255};
    static const uint8_t two_pixels[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t expect[16];
    struct xlayer l;
    unsigned i;

    xlayer_init(&l, 2, 2, 2, grey);
    build_xcf(11, 2, 2, 1, 150, 1, NULL, 0, &l, 1);
    for (i = 0; i < 4; i++) {
        memset(expect + i * 4, grey[i], 3);
        expect[i * 4 + 3] = 255;
    }
    expect_xcf(2, 2, expect, 0);

    /* Indexes past the colour map are black. */
    xlayer_init(&l, 2, 2, 5, indexed);
    build_xcf(11, 2, 2, 2, 150, 1, map, 2, &l, 1);
    memcpy(expect, "\xff\0\0\xff\0\xff\0\x80\0\0\0\xff\0\xff\0\0", 16);
    expect_xcf(2, 2, expect, 0);
    /* Version 0 saved colour maps wrongly: GIMP uses a grey ramp. */
    build_xcf(0, 2, 2, 2, 150, 1, map, 2, &l, 1);
    memcpy(expect, "\0\0\0\xff\1\1\1\x80\0\0\0\xff\1\1\1\0", 16);
    expect_xcf(2, 2, expect, 0);

    /* 16-bit, big-endian from version 12. */
    xlayer_init(&l, 2, 2, 3, deep);
    build_xcf(12, 2, 2, 1, 250, 2, NULL, 0, &l, 1);
    memcpy(expect, "\x80\x80\x80\xff\x7f\x7f\x7f\0\x12\x12\x12\x56\xff\xff\xff\0", 16);
    expect_xcf(2, 2, expect, 0);

    /* 8-bit linear light shows sRGB-encoded. */
    xlayer_init(&l, 3, 1, 2, linear);
    build_xcf(11, 3, 1, 1, 100, 0, NULL, 0, &l, 1);
    memcpy(expect, "\0\0\0\xff\xbc\xbc\xbc\xff\xff\xff\xff\xff", 12);
    expect_xcf(3, 1, expect, 0);

    /* Floating point precision is HDR: not supported. */
    xlayer_init(&l, 1, 1, 1, deep);
    build_xcf(11, 1, 1, 0, 600, 0, NULL, 0, &l, 1);
    expect_fail(CODEC_INVALID);
    build_xcf(11, 1, 1, 0, 550, 0, NULL, 0, &l, 1);
    expect_fail(CODEC_INVALID);
    /* Version 5 numbered half precision 400. */
    build_xcf(5, 1, 1, 0, 400, 0, NULL, 0, &l, 1);
    expect_fail(CODEC_INVALID);
    l.px = two_pixels;
    build_xcf(5, 1, 1, 0, 150, 0, NULL, 0, &l, 1);
    memcpy(expect, two_pixels, 4);
    expect_xcf(1, 1, expect, 0);
}

static void test_xcf_effects_and_floating(void)
{
    static const uint8_t blue[4] = {0, 0, 255, 255};
    static const uint8_t red[4] = {255, 0, 0, 255};
    static const uint8_t blue4[16] = {0, 0, 255, 255, 0, 0, 255, 255,
                                      0, 0, 255, 255, 0, 0, 255, 255};
    struct xlayer l[2];
    uint8_t expect[16];

    /* Visible non-destructive filters can't be shown faithfully. */
    xlayer_init(&l[0], 1, 1, 1, blue);
    l[0].effect = 1;
    build_xcf(22, 1, 1, 0, 150, 1, NULL, 0, l, 1);
    expect_fail(CODEC_INVALID);
    l[0].effect = 0;
    build_xcf(22, 1, 1, 0, 150, 1, NULL, 0, l, 1);
    expect_xcf(1, 1, blue, 0);
    l[0].effect = 1;
    l[0].visible = 0;
    build_xcf(20, 1, 1, 0, 150, 1, NULL, 0, l, 1);
    memset(expect, 0, 4);
    expect_xcf(1, 1, expect, 0);

    /* A floating selection shows on the layer it floats on, which grows
       to cover it. */
    xlayer_init(&l[0], 1, 1, 1, red);
    l[0].floats_on = 1;
    l[0].x = 1;
    xlayer_init(&l[1], 1, 2, 1, blue4);
    build_xcf(11, 2, 2, 0, 150, 1, NULL, 0, l, 2);
    memcpy(expect, blue, 4);
    memcpy(expect + 4, red, 4);
    memcpy(expect + 8, blue, 4);
    memset(expect + 12, 0, 4);
    expect_xcf(2, 2, expect, 0);
    l[0].visible = 0;
    build_xcf(11, 2, 2, 0, 150, 1, NULL, 0, l, 2);
    memset(expect + 4, 0, 4);
    expect_xcf(2, 2, expect, 0);
}

static void test_xcf_malformed(void)
{
    static uint8_t big[65 * 2 * 4];
    static const uint8_t mask[4] = {255, 0, 255, 0};
    static const uint8_t px[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    struct xlayer l[8];
    size_t at;

    /* Every prefix of a file with a group, a mask and several tiles. */
    xlayer_init(&l[0], 0, 0, 1, NULL);
    l[0].group = 1;
    xlayer_init(&l[1], 2, 2, 1, px);
    l[1].mask = mask;
    l[1].path[0] = 0;
    l[1].path[1] = 0;
    l[1].depth = 2;
    xlayer_init(&l[2], 65, 2, 1, big);
    build_xcf(11, 65, 2, 0, 150, 1, NULL, 0, l, 3);
    expect_prefixes_fail();
    build_xcf(3, 65, 2, 0, 150, 2, NULL, 0, l, 3);
    expect_prefixes_fail();
    build_xcf(20, 65, 2, 0, 150, 0, NULL, 0, l, 3);
    expect_prefixes_fail();

    /* The one-layer file used below: its tile table is at the end of the
       level header, after the two tiles' offsets. */
    xlayer_init(&l[0], 65, 2, 1, big);
    build_xcf(11, 65, 2, 0, 150, 0, NULL, 0, l, 1);
    {
        struct gimp_image image;
        unsigned count;
        assert(decode(0, &image, &count) == CODEC_OK);
        gimp_free(&image);
    }
    /* Find the tile table: the first tile follows it directly. */
    at = file_length - 65 * 2 * 4 - 3 * 8;

    /* Tile offsets going backwards. */
    put_offset(file + at + 8, 10);
    expect_fail(CODEC_INVALID);
    build_xcf(11, 65, 2, 0, 150, 0, NULL, 0, l, 1);
    /* Too few tiles. */
    put_offset(file + at + 8, 0);
    expect_fail(CODEC_INVALID);
    build_xcf(11, 65, 2, 0, 150, 0, NULL, 0, l, 1);
    /* No end to the table. */
    put_offset(file + at + 16, 30);
    expect_fail(CODEC_INVALID);
    build_xcf(11, 65, 2, 0, 150, 0, NULL, 0, l, 1);
    /* Offsets past the end of the file. */
    put_offset(file + at, file_length + 100);
    expect_fail(CODEC_TRUNCATED);
    build_xcf(11, 65, 2, 0, 150, 0, NULL, 0, l, 1);
    put32(file + at, 0x7fffffffu);
    expect_fail(CODEC_TRUNCATED);
    /* A hierarchy with the wrong depth. */
    build_xcf(11, 65, 2, 0, 150, 0, NULL, 0, l, 1);
    put32(file + at - 8 - 8 - 8 - 4, 3);
    expect_fail(CODEC_INVALID);

    /* RLE runs past the tile, or cut short. */
    xlayer_init(&l[0], 2, 2, 1, px);
    build_xcf(11, 2, 2, 0, 150, 1, NULL, 0, l, 1);
    file[file_length - 4 * 5] = 0x7e;
    expect_fail(CODEC_INVALID);
    build_xcf(11, 2, 2, 0, 150, 1, NULL, 0, l, 1);
    file_length -= 2;
    expect_fail(CODEC_TRUNCATED);
    /* zlib data that isn't. */
    build_xcf(11, 2, 2, 0, 150, 2, NULL, 0, l, 1);
    memset(file + file_length - 8, 0x55, 8);
    expect_fail(CODEC_INVALID);

    /* Sizes, types and header values. */
    build_xcf(11, 0, 2, 0, 150, 0, NULL, 0, l, 1);
    expect_fail(CODEC_INVALID);
    build_xcf(11, 70000, 2, 0, 150, 0, NULL, 0, l, 1);
    expect_fail(CODEC_TOO_LARGE);
    build_xcf(11, 5000, 5000, 0, 150, 0, NULL, 0, l, 1);
    expect_fail(CODEC_TOO_LARGE);
    build_xcf(11, 2, 2, 3, 150, 0, NULL, 0, l, 1);
    expect_fail(CODEC_INVALID);
    /* An RGB layer in a grey image. */
    build_xcf(11, 2, 2, 1, 150, 0, NULL, 0, l, 1);
    expect_fail(CODEC_INVALID);
    l[0].type = 6;
    build_xcf(11, 2, 2, 0, 150, 0, NULL, 0, l, 1);
    expect_fail(CODEC_INVALID);
    xlayer_init(&l[0], 0, 2, 1, px);
    build_xcf(11, 2, 2, 0, 150, 0, NULL, 0, l, 1);
    expect_fail(CODEC_INVALID);
    xlayer_init(&l[0], 2, 2, 1, px);
    build_xcf(26, 2, 2, 0, 150, 0, NULL, 0, l, 1);
    expect_fail(CODEC_INVALID);
    build_xcf(11, 2, 2, 0, 150, 3, NULL, 0, l, 1);
    expect_fail(CODEC_INVALID);
    build_xcf(11, 2, 2, 0, 150, 0, NULL, 0, l, 1);
    memcpy(file + 9, "v1x1", 4);
    expect_fail(CODEC_INVALID);
    /* No layers at all. */
    build_xcf(11, 2, 2, 0, 150, 0, NULL, 0, l, 0);
    expect_fail(CODEC_INVALID);
    /* A colour map too big. */
    build_xcf(11, 2, 2, 2, 150, 0, px, 1, l, 0);
    put32(file + 14 + 16 + 9 + 8, 300);
    expect_fail(CODEC_INVALID);
    /* One costly layer listed many times: a small file mustn't cost more
       to read than its size allows. */
    {
        static uint8_t flat[4096 * 64 * 4];
        size_t start, table = 14 + 16 + 9 + 8;
        unsigned k;
        xlayer_init(&l[0], 4096, 64, 1, flat);
        for (k = 1; k < 8; k++) {
            xlayer_init(&l[k], 1, 1, 1, px);
            l[k].visible = 0;
        }
        build_xcf(11, 4096, 64, 0, 150, 1, NULL, 0, l, 8);
        expect_xcf(4096, 64, flat, 0);
        start = file[table + 7] | (size_t)file[table + 6] << 8 | (size_t)file[table + 5] << 16;
        for (k = 1; k < 8; k++)
            put_offset(file + table + k * 8, start);
        expect_fail(CODEC_INVALID);
    }
    /* A layer pointer back into the header. */
    build_xcf(11, 2, 2, 0, 150, 0, NULL, 0, l, 1);
    put_offset(file + 14 + 16 + 9 + 8, 8);
    expect_fail(CODEC_INVALID);
}

static void test_xcf_writer(void)
{
    static uint8_t rgba[130 * 66 * 4];
    static uint32_t sizes[3 * 2];
    static uint8_t band[130 * 64 * 4 * 2 + 64];
    unsigned i, alpha;

    /* Noise, flat areas and runs longer than 127 pixels. */
    for (i = 0; i < sizeof rgba; i++)
        rgba[i] = (uint8_t)(i < sizeof rgba / 2 ? i * 7 + i / 13 : (i / 700) * 40);
    for (alpha = 0; alpha < 2; alpha++) {
        unsigned bytes = alpha ? 4 : 3, y;
        size_t header;
        if (!alpha)
            for (i = 3; i < sizeof rgba; i += 4)
                rgba[i] = 255;
        for (y = 0; y < 66; y += 64)
            xcf_encode_tiles(rgba + (size_t)y * 130 * 4, 130, 66 - y < 64 ? 66 - y : 64,
                             bytes, NULL, sizes + (y / 64) * 3);
        reset();
        header = xcf_make_header(130, 66, bytes, sizes, file);
        assert(header == xcf_header_size(130, 66));
        file_length = header;
        for (y = 0; y < 66; y += 64) {
            size_t n = xcf_encode_tiles(rgba + (size_t)y * 130 * 4, 130,
                                        66 - y < 64 ? 66 - y : 64, bytes, band, NULL);
            assert(n <= XCF_BAND_BOUND(130, bytes));
            add(band, n);
        }
        memset(file + file_length, 0, XCF_TRAILER(bytes));
        file_length += XCF_TRAILER(bytes);
        assert(gimp_sniff(file, file_length) == GIMP_XCF);
        expect_xcf(130, 66, rgba, 0);
    }
    assert(xcf_make_header(0, 1, 3, sizes, file) == 0);
    assert(xcf_make_header(65536, 1, 3, sizes, file) == 0);
    assert(xcf_make_header(4096, 4097, 3, sizes, file) == 0);
}

void test_xcf(void)
{
    test_xcf_basics();
    test_xcf_stack();
    test_xcf_groups();
    test_xcf_formats();
    test_xcf_effects_and_floating();
    test_xcf_malformed();
    test_xcf_writer();
}

int main(void)
{
    test_grey_brush();
    test_colour_brush();
    test_old_brush();
    test_pixmap_brush();
    test_bad_brushes();
    test_pipe();
    test_patterns();
    test_unknown();
    test_writers();
    test_xcf();
    puts("gimp: ok");
    return 0;
}
