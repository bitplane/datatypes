#include "../formats/lunapaint/decode.h"
#include "../formats/lunapaint/encode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t file[1 << 20];
static size_t length;
static int big;

/* A source over file[0..length), counting reads. */
static unsigned reads;

static int read_memory(void *context, uint64_t offset, void *buffer, size_t n)
{
    (void)context;
    assert(offset <= length && n <= length - offset);
    memcpy(buffer, file + offset, n);
    reads++;
    return 0;
}

static enum codec_result decode(unsigned index, struct lunapaint_image *image)
{
    struct lunapaint_source source = { NULL, 0, read_memory };
    source.size = length;
    return lunapaint_decode(&source, index, image);
}

static void put8(unsigned v)
{
    file[length++] = (uint8_t)v;
}

static void put16(unsigned v)
{
    if (big) {
        put8(v >> 8);
        put8(v);
    } else {
        put8(v);
        put8(v >> 8);
    }
}

static void put32(uint32_t v)
{
    if (big) {
        put16(v >> 16);
        put16(v & 0xffffu);
    } else {
        put16(v & 0xffffu);
        put16(v >> 16);
    }
}

/* One pixel: a 64-bit word, red on top, alpha at the bottom. */
static void pixel16(unsigned r, unsigned g, unsigned b, unsigned a)
{
    if (big) {
        put16(r); put16(g); put16(b); put16(a);
    } else {
        put16(a); put16(b); put16(g); put16(r);
    }
}

static void pixel(unsigned r, unsigned g, unsigned b, unsigned a)
{
    pixel16(r * 257u, g * 257u, b * 257u, a * 257u);
}

static void header(unsigned w, unsigned h, unsigned layers, unsigned frames,
                   unsigned objects, const char *description)
{
    length = 0;
    memset(file, 0, 16 + 256);
    memcpy(file, "Lunapaint_v1", 12);
    memcpy(file + 16, "Project", 7);
    memcpy(file + 144, "Author", 6);
    length = 272;
    put16(w);
    put16(h);
    put16(layers);
    put16(frames);
    put16(objects);
    put32((uint32_t)strlen(description));
    memcpy(file + length, description, strlen(description));
    length += strlen(description);
}

static void object(unsigned type, unsigned layer, unsigned frame,
                   unsigned size)
{
    put32(type);
    put32(layer);
    put32(frame);
    put32(size);
}

static void opacity(unsigned layer, unsigned frame, unsigned value)
{
    object(1, layer, frame, 1);
    put8(value);
}

static void visibility(unsigned layer, unsigned frame, unsigned value)
{
    object(2, layer, frame, 1);
    put8(value);
}

static void name(unsigned layer, unsigned frame, const char *text)
{
    object(3, layer, frame, (unsigned)strlen(text));
    memcpy(file + length, text, strlen(text));
    length += strlen(text);
}

static void expect_rgba(const struct lunapaint_image *image, unsigned i,
                        unsigned r, unsigned g, unsigned b, unsigned a)
{
    const uint8_t *p = image->rgba + i * 4u;
    if (p[0] != r || p[1] != g || p[2] != b || p[3] != a) {
        fprintf(stderr, "pixel %u: %u %u %u %u, want %u %u %u %u\n", i,
                p[0], p[1], p[2], p[3], r, g, b, a);
        assert(0);
    }
}

/* Lunapaint's own blend over an opaque pixel, written out independently. */
static unsigned lunapaint_mix(unsigned under, unsigned over, unsigned a16,
                              unsigned percent)
{
    int a = (int)(a16 / 256);
    double alpha;
    if (percent < 100)
        a = (int)(a / 100.0 * percent);
    if (a <= 0)
        return under;
    alpha = a / 255.0;
    return (unsigned char)(under - ((int)under - (int)(over)) * alpha);
}

static void test_single_layer(void)
{
    struct lunapaint_image image;
    for (big = 0; big < 2; big++) {
        header(3, 2, 1, 1, 3, " ");
        pixel(255, 0, 0, 255);
        pixel(0, 255, 0, 255);
        pixel(0, 0, 255, 255);
        pixel(10, 20, 30, 0);           /* adds nothing to the picture */
        pixel(1, 2, 3, 77);
        /* 16-bit channels keep their top byte, as Lunapaint shows them. */
        pixel16(0x12ff, 0x3480, 0x5601, 0xffff);
        opacity(0, 0, 100);
        visibility(0, 0, 1);
        name(0, 0, "Background");
        assert(decode(0, &image) == CODEC_OK);
        assert(image.width == 3 && image.height == 2 && image.frames == 1);
        expect_rgba(&image, 0, 255, 0, 0, 255);
        expect_rgba(&image, 1, 0, 255, 0, 255);
        expect_rgba(&image, 2, 0, 0, 255, 255);
        expect_rgba(&image, 3, 0, 0, 0, 0);
        expect_rgba(&image, 4, 1, 2, 3, 77);
        expect_rgba(&image, 5, 0x12, 0x34, 0x56, 255);
        lunapaint_free(&image);
    }
}

/* Opaque bottom layer and a translucent top one, at every alpha and a
   range of opacities: must match Lunapaint's arithmetic exactly. */
static void test_blend_over_opaque(void)
{
    static const unsigned percents[] = { 1, 33, 50, 99, 100 };
    struct lunapaint_image image;
    unsigned p, a;
    for (big = 0; big < 2; big++)
        for (p = 0; p < sizeof percents / sizeof percents[0]; p++) {
            header(256, 1, 2, 1, 6, " ");
            for (a = 0; a < 256; a++)
                pixel(200, 17, a, 255);
            for (a = 0; a < 256; a++)
                pixel16(9 * 257, 250 * 257, 255 - a, a * 256u + (a & 1));
            opacity(0, 0, 100);
            visibility(0, 0, 1);
            name(0, 0, "Bottom");
            opacity(1, 0, percents[p]);
            visibility(1, 0, 1);
            name(1, 0, "Top");
            assert(decode(0, &image) == CODEC_OK);
            for (a = 0; a < 256; a++) {
                unsigned a16 = a * 256u + (a & 1);
                expect_rgba(&image, a,
                            lunapaint_mix(200, 9, a16, percents[p]),
                            lunapaint_mix(17, 250, a16, percents[p]),
                            lunapaint_mix(a, (255 - a) / 256, a16, percents[p]),
                            255);
            }
            lunapaint_free(&image);
        }
}

static void test_blend_translucent(void)
{
    struct lunapaint_image image;
    for (big = 0; big < 2; big++) {
        header(3, 1, 2, 1, 6, " ");
        pixel(100, 150, 200, 128);
        pixel(0, 0, 0, 0);
        pixel(255, 0, 0, 128);
        pixel(0, 0, 0, 0);
        pixel(40, 50, 60, 64);
        pixel(0, 0, 255, 128);
        opacity(0, 0, 100);
        visibility(0, 0, 1);
        opacity(1, 0, 100);
        visibility(1, 0, 1);
        assert(decode(0, &image) == CODEC_OK);
        /* Over nothing, a layer keeps its own colour and alpha. */
        expect_rgba(&image, 0, 100, 150, 200, 128);
        expect_rgba(&image, 1, 40, 50, 60, 64);
        /* Straight-alpha "over": alpha 128 + 128 * 127 / 255 = 192. */
        expect_rgba(&image, 2, 85, 0, 170, 192);
        lunapaint_free(&image);
    }
}

/* Hidden layers, layers at 0% and anything but 1 as visibility are
   skipped. Opacity over 100 counts as 100. */
static void test_visibility(void)
{
    struct lunapaint_image image;
    for (big = 0; big < 2; big++) {
        header(1, 1, 5, 1, 15, " ");
        pixel(10, 10, 10, 255);
        pixel(20, 20, 20, 255);
        pixel(30, 30, 30, 255);
        pixel(40, 40, 40, 255);
        pixel(50, 50, 50, 128);
        visibility(1, 0, 0);
        opacity(2, 0, 0);
        visibility(3, 0, 2);
        opacity(4, 0, 200);
        assert(decode(0, &image) == CODEC_OK);
        expect_rgba(&image, 0, 30, 30, 30, 255);
        lunapaint_free(&image);
    }
}

/* Frames are pictures; each has its own layers and attributes. Lunapaint
   only reads the first objectCount records, which cover frame 0, and
   leaves out empty names, so the table is often shorter than promised. */
static void test_frames(void)
{
    struct lunapaint_image image;
    unsigned f;
    for (big = 0; big < 2; big++) {
        header(2, 1, 2, 3, 6, " ");
        for (f = 0; f < 3; f++) {
            pixel(f, 0, 0, 255);
            pixel(f, 1, 0, 255);
            pixel(0, 0, 100 + f, 255);
            pixel(0, 0, 200 + f, 0);
        }
        for (f = 0; f < 3; f++) {
            opacity(0, f, 100);
            visibility(0, f, 1);
            opacity(1, f, 100);
            visibility(1, f, f != 2);
        }
        for (f = 0; f < 3; f++) {
            assert(decode(f, &image) == CODEC_OK);
            assert(image.frames == 3);
            expect_rgba(&image, 0, f == 2 ? 2 : 0, 0, f == 2 ? 0 : 100 + f, 255);
            expect_rgba(&image, 1, f, 1, 0, 255);
            lunapaint_free(&image);
        }
        assert(decode(3, &image) == CODEC_INVALID);
        assert(image.frames == 3 && image.rgba == NULL);
        assert(decode(~0u, &image) == CODEC_INVALID);
    }
}

/* Only the layers of the chosen frame are read, a chunk at a time. */
static void test_reads_only_frame(void)
{
    struct lunapaint_image image;
    unsigned f, i;
    big = 0;
    header(64, 64, 2, 4, 6, " ");
    for (f = 0; f < 4 * 2; f++)
        for (i = 0; i < 64 * 64; i++)
            pixel(f, i & 255, 0, 255);
    reads = 0;
    assert(decode(3, &image) == CODEC_OK);
    assert(reads <= 6);
    expect_rgba(&image, 5, 7, 5, 0, 255);
    lunapaint_free(&image);
}

static void test_no_objects(void)
{
    struct lunapaint_image image;
    for (big = 0; big < 2; big++) {
        /* Defaults: every layer visible at 100%. */
        header(1, 1, 2, 1, 0, "");
        pixel(1, 2, 3, 255);
        pixel(9, 9, 9, 0);
        assert(decode(0, &image) == CODEC_OK);
        expect_rgba(&image, 0, 1, 2, 3, 255);
        lunapaint_free(&image);
        /* A long description is skipped. */
        header(1, 1, 1, 1, 3, "A picture of some things, drawn in 2007.");
        pixel(7, 8, 9, 255);
        assert(decode(0, &image) == CODEC_OK);
        expect_rgba(&image, 0, 7, 8, 9, 255);
        lunapaint_free(&image);
    }
}

/* Records that don't make sense are skipped or end the table. */
static void test_bad_objects(void)
{
    struct lunapaint_image image;
    size_t cut;
    for (big = 0; big < 2; big++) {
        header(1, 1, 2, 1, 6, " ");
        pixel(1, 1, 1, 255);
        pixel(2, 2, 2, 255);
        visibility(5, 0, 0);            /* no such layer */
        visibility(0, 9, 0);            /* no such frame */
        object(3, 1, 0, 1000);          /* name past the end */
        assert(decode(0, &image) == CODEC_OK);
        expect_rgba(&image, 0, 2, 2, 2, 255);
        lunapaint_free(&image);

        header(1, 1, 2, 1, 6, " ");
        pixel(1, 1, 1, 255);
        pixel(2, 2, 2, 255);
        object(99, 0, 0, 1);            /* unknown: the table stops */
        visibility(1, 0, 0);
        assert(decode(0, &image) == CODEC_OK);
        expect_rgba(&image, 0, 2, 2, 2, 255);
        lunapaint_free(&image);

        /* Cut anywhere in the table: still loads, with what was read. */
        header(1, 1, 2, 1, 6, " ");
        pixel(1, 1, 1, 255);
        pixel(2, 2, 2, 255);
        name(1, 0, "Top");
        visibility(1, 0, 0);
        for (cut = length; cut >= 286 + 1 + 16; cut--) {
            size_t full = length;
            length = cut;
            assert(decode(0, &image) == CODEC_OK);
            expect_rgba(&image, 0, cut == full ? 1 : 2, cut == full ? 1 : 2,
                        cut == full ? 1 : 2, 255);
            lunapaint_free(&image);
            length = full;
        }
    }
}

static void test_malformed(void)
{
    struct lunapaint_image image;
    size_t cut, full;
    for (big = 0; big < 2; big++) {
        /* Truncation anywhere before the end of the pixels. */
        header(2, 2, 2, 2, 12, " ");
        for (cut = 0; cut < 16; cut++)
            pixel(cut, cut, cut, 255);
        full = length;
        for (cut = 0; cut < full; cut++) {
            length = cut;
            assert(decode(0, &image) == CODEC_TRUNCATED);
            assert(image.rgba == NULL);
        }
        length = full;
        assert(decode(1, &image) == CODEC_OK);
        lunapaint_free(&image);

        /* Wrong magic, at any length. */
        file[11] = '2';
        assert(decode(0, &image) == CODEC_INVALID);
        length = 20;
        assert(decode(0, &image) == CODEC_INVALID);
        length = 5;
        assert(decode(0, &image) == CODEC_TRUNCATED);

        /* Zero and negative sizes. */
        header(0, 1, 1, 1, 3, " ");
        pixel(0, 0, 0, 0);
        assert(decode(0, &image) == CODEC_INVALID);
        header(1, 1, 0x8001u, 1, 3, " ");
        pixel(0, 0, 0, 0);
        assert(decode(0, &image) == CODEC_INVALID);
        header(1, 1, 1, 0, 3, " ");
        assert(decode(0, &image) == CODEC_INVALID);

        /* Over 16M pixels. */
        header(0x4080u, 0x0480u, 1, 1, 3, " ");
        assert(decode(0, &image) == CODEC_TOO_LARGE);

        /* A description length that runs past the end. */
        header(1, 1, 1, 1, 3, " ");
        pixel(0, 0, 0, 0);
        length = 282;
        put32(0xfffffff0u);
        length = 286 + 1 + 8;
        assert(decode(0, &image) == CODEC_TRUNCATED);

        /* Buffers that multiply past the end of the file. */
        header(0x7fff, 0x1ff, 0x7fff, 0x7fff, 3, " ");
        assert(decode(0, &image) == CODEC_TRUNCATED);
    }
}

static void test_writer(void)
{
    uint8_t rgba[5 * 3 * 4], head[LUNAPAINT_WRITE_HEADER];
    uint8_t trailer[LUNAPAINT_WRITE_TRAILER];
    struct lunapaint_image image;
    unsigned i, y;
    for (i = 0; i < sizeof rgba; i++)
        rgba[i] = (uint8_t)(i * 37u + 11u);
    rgba[3] = 0;                        /* fully transparent pixel */
    for (big = 0; big < 2; big++) {
        assert(lunapaint_make_header(5, 3, big, head) == CODEC_OK);
        memcpy(file, head, sizeof head);
        length = sizeof head;
        for (y = 0; y < 3; y++) {
            lunapaint_encode_row(rgba + y * 20u, 5, big, file + length);
            length += lunapaint_row_size(5);
        }
        lunapaint_make_trailer(big, trailer);
        memcpy(file + length, trailer, sizeof trailer);
        length += sizeof trailer;
        assert(length == 287 + 5 * 3 * 8 + 55);
        assert(memcmp(file, "Lunapaint_v1\0\0\0\0", 16) == 0);
        /* width 5 at offset 272 in the chosen byte order */
        assert(file[272 + (big ? 1 : 0)] == 5 && file[272 + (big ? 0 : 1)] == 0);
        /* red on top of the 64-bit word */
        assert(file[287 + (big ? 0 : 7)] == rgba[0]);
        assert(file[287 + (big ? 6 : 1)] == rgba[3]);
        assert(decode(0, &image) == CODEC_OK);
        assert(image.width == 5 && image.height == 3 && image.frames == 1);
        /* Everything comes back but the colour of the transparent pixel. */
        assert(memcmp(image.rgba, "\0\0\0\0", 4) == 0);
        assert(memcmp(image.rgba + 4, rgba + 4, sizeof rgba - 4) == 0);
        lunapaint_free(&image);
    }
    assert(lunapaint_make_header(0, 1, 0, head) == CODEC_INVALID);
    assert(lunapaint_make_header(1, 0, 0, head) == CODEC_INVALID);
    assert(lunapaint_make_header(32768, 1, 0, head) == CODEC_INVALID);
    assert(lunapaint_make_header(32767, 1, 0, head) == CODEC_OK);
    assert(lunapaint_make_header(5000, 5000, 0, head) == CODEC_TOO_LARGE);
    assert(lunapaint_native_big_endian() == 0 ||
           lunapaint_native_big_endian() == 1);
}

int main(void)
{
    test_single_layer();
    test_blend_over_opaque();
    test_blend_translucent();
    test_visibility();
    test_frames();
    test_reads_only_frame();
    test_no_objects();
    test_bad_objects();
    test_malformed();
    test_writer();
    puts("lunapaint: ok");
    return 0;
}
