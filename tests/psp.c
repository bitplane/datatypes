#include "../formats/psp/psp.h"
#include "../common/zlib.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A file under construction. */
static uint8_t file[1 << 20];
static size_t size;
static unsigned major;
static unsigned compression;   /* for layer channels */

static void put8(unsigned v) { assert(size < sizeof file); file[size++] = (uint8_t)v; }
static void put16(unsigned v) { put8(v & 255u); put8(v >> 8); }
static void put32(uint32_t v) { put16(v & 65535u); put16(v >> 16); }
static void put(const void *data, size_t n)
{
    assert(size + n <= sizeof file);
    memcpy(file + size, data, n);
    size += n;
}
static void zeros(size_t n) { while (n--) put8(0); }
static void patch32(size_t at, uint32_t v) { psp_put32(file + at, v); }

static void begin(unsigned version)
{
    size = 0;
    major = version;
    compression = 2;
    put("Paint Shop Pro Image File\n\x1a\0\0\0\0\0", 32);
    put16(version);
    put16(0);
}

/* Open a block; returns what close_block needs. Format 3.0 blocks also
   carry the length of their first chunk, set with set_init. */
static size_t open_block(unsigned id)
{
    size_t at = size;
    put("~BK", 4);
    put16(id);
    put32(0);
    if (major < 4)
        put32(0);
    return at;
}
static size_t body_of(size_t at) { return at + (major < 4 ? 14u : 10u); }
static void close_block(size_t at)
{
    size_t body = body_of(at);
    patch32(at + (major < 4 ? 10u : 6u), (uint32_t)(size - body));
}
static void set_init(size_t at, uint32_t init) { if (major < 4) patch32(at + 6, init); }

static void image_block(unsigned width, unsigned height, unsigned depth,
                        unsigned grey, unsigned layers)
{
    size_t at = open_block(0);
    if (major >= 4)
        put32(46);
    put32(width);
    put32(height);
    zeros(8);
    put8(1);
    put16(compression);
    put16(depth);
    put16(1);
    put32(depth >= 24 ? 1u << 24 : 1u << (depth > 16 ? 16 : depth));
    put8(grey);
    put32(0);
    put32(0);
    put16(layers);
    if (major >= 4)
        put32(1);
    set_init(at, 38);
    close_block(at);
}

static void palette_body(const uint8_t (*rgb)[3], unsigned count)
{
    unsigned i;
    if (major >= 4) {
        put32(8);
        put32(count);
    } else {
        put32(count);
    }
    for (i = 0; i < count; i++) {
        put8(rgb[i][2]);
        put8(rgb[i][1]);
        put8(rgb[i][0]);
        put8(0);
    }
}

static void palette_block(const uint8_t (*rgb)[3], unsigned count)
{
    size_t at = open_block(2);
    palette_body(rgb, count);
    close_block(at);
}

/* RLE with a mix of runs and literals. */
static size_t rle(const uint8_t *src, size_t n, uint8_t *out)
{
    size_t i = 0, o = 0;
    while (i < n) {
        size_t run = 1;
        while (i + run < n && run < 127 && src[i + run] == src[i])
            run++;
        if (run >= 3) {
            out[o++] = (uint8_t)(128 + run);
            out[o++] = src[i];
            i += run;
        } else {
            size_t lit = 0;
            while (i + lit < n && lit < 127 &&
                   !(i + lit + 2 < n && src[i + lit] == src[i + lit + 1] &&
                     src[i + lit] == src[i + lit + 2]))
                lit++;
            out[o++] = (uint8_t)lit;
            memcpy(out + o, src + i, lit);
            o += lit;
            i += lit;
        }
    }
    return o;
}

/* A channel sub-block holding n bytes of plane data. */
static void channel(unsigned comp, unsigned bitmap, unsigned type,
                    const uint8_t *plane, size_t n)
{
    static uint8_t packed[1 << 19];
    size_t length = n, at;
    const uint8_t *data = plane;

    if (comp == 1) {
        length = rle(plane, n, packed);
        data = packed;
    } else if (comp == 2) {
        assert(zlib_deflate(plane, n, packed, sizeof packed, 6, &length) == CODEC_OK);
        data = packed;
    }
    at = open_block(5);
    if (major >= 4)
        put32(16);
    put32((uint32_t)length);
    put32((uint32_t)n);
    put16(bitmap);
    put16(type);
    set_init(at, 12);
    put(data, length);
    close_block(at);
}

struct tlayer {
    const char *name;
    unsigned type, opacity, blend, visible;
    long x, y;
    unsigned width, height;
    const uint8_t *colour;       /* 3 planes, or one for grey/indexed */
    unsigned planes, plane_bytes;/* colour planes and bytes in each */
    const uint8_t *trans;
    long mask_x, mask_y;
    unsigned mask_width, mask_height;
    const uint8_t *mask;
    unsigned mask_disabled, mask_invert;
    unsigned long children;      /* groups */
    int vector;                  /* write a vector extension block */
};

static void rect(long x, long y, unsigned width, unsigned height)
{
    put32((uint32_t)x);
    put32((uint32_t)y);
    put32((uint32_t)(x + (long)width));
    put32((uint32_t)(y + (long)height));
}

static void layer(const struct tlayer *l)
{
    size_t at = open_block(4), start, n = (size_t)l->width * l->height;
    unsigned p, bitmaps = (l->colour ? 1u : 0u) + (l->trans ? 1u : 0u) + (l->mask ? 1u : 0u);
    unsigned channels = (l->colour ? l->planes : 0u) + (l->trans ? 1u : 0u) + (l->mask ? 1u : 0u);
    const char *name = l->name ? l->name : "Layer";

    start = size;
    if (major >= 4) {
        put32(0);
        put16((unsigned)strlen(name));
        put(name, strlen(name));
        put8(l->type);
    } else {
        char fixed[256] = { 0 };
        strncpy(fixed, name, 255);
        put(fixed, 256);
        put8(l->type == 2 ? 1 : 0);
    }
    /* The saved rectangle is relative to the image rectangle. */
    rect(l->x - 3, l->y - 2, l->width + 5, l->height + 4);
    rect(3, 2, l->width, l->height);
    put8(l->opacity);
    put8(l->blend);
    put8(l->visible ? (major >= 4 ? 1u | (l->mask ? 2u : 0u) : 1u) : 0u);
    put8(0);
    put8(0);
    if (l->mask) {
        rect(l->mask_x - 1, l->mask_y, l->mask_width + 1, l->mask_height);
        rect(1, 0, l->mask_width, l->mask_height);
    } else {
        zeros(32);
    }
    put8(1);
    put8(l->mask_disabled);
    put8(l->mask_invert);
    put16(0);
    for (p = 0; p < 10; p++)
        put("\0\0\xff\xff", 4);
    if (major >= 4) {
        put8(0);
        put32(0);
        patch32(start, (uint32_t)(size - start));
        if (l->type == 5) {
            size_t g = open_block(25);
            put32(9);
            put32((uint32_t)l->children);
            put8(0);
            close_block(g);
        } else if (l->type == 6) {
            size_t m = open_block(26);
            put32(9);
            put32(0x0000ff);
            put8(128);
            close_block(m);
        } else if (l->vector) {
            size_t v = open_block(13);
            put32(8);
            put32(0);
            close_block(v);
        }
        put32(8);
        put16(bitmaps);
        put16(channels);
    } else {
        put16(bitmaps);
        put16(channels);
        set_init(at, (uint32_t)(size - start));
    }
    for (p = 0; l->colour && p < l->planes; p++)
        channel(compression, 0, l->planes == 3 ? p + 1 : 0,
                l->colour + p * l->plane_bytes, l->plane_bytes);
    if (l->trans)
        channel(compression, 1, 0, l->trans, n);
    if (l->mask)
        channel(compression, 2, 0, l->mask,
                (size_t)l->mask_width * l->mask_height);
    close_block(at);
}

static size_t bank;
static void open_bank(void) { bank = open_block(3); }
static void close_bank(void) { close_block(bank); }

/* A composite image bank with a JPEG thumbnail and, unless jpeg, a raw
   full-size composite. rgb holds three planes; alpha may be NULL. */
static void composite_bank(unsigned width, unsigned height, const uint8_t *rgb,
                           const uint8_t *alpha, int jpeg, unsigned comp)
{
    size_t at = open_block(16), a, n = (size_t)width * height;
    unsigned c;

    put32(8);
    put32(2);
    a = open_block(17);
    put32(24); put32(4); put32(4); put16(24); put16(3); put16(1);
    put32(1u << 24); put16(1);
    close_block(a);
    a = open_block(17);
    put32(24); put32(width); put32(height); put16(24); put16(jpeg ? 3 : comp);
    put16(1); put32(1u << 24); put16(0);
    close_block(a);
    a = open_block(18);
    put32(14); put32(4); put32(48); put16(5);
    put("\xff\xd8\xff\xd9", 4);
    close_block(a);
    a = open_block(jpeg ? 18 : 9);
    if (jpeg) {
        put32(14); put32(4); put32(48); put16(8);
        put("\xff\xd8\xff\xd9", 4);
    } else {
        put32(8);
        put16(alpha ? 2 : 1);
        put16(alpha ? 4 : 3);
        for (c = 0; c < 3; c++)
            channel(comp, 8, c + 1, rgb + c * n, n);
        if (alpha)
            channel(comp, 9, 0, alpha, n);
    }
    close_block(a);
    close_block(at);
}

static enum codec_result decode(struct psp_image *image)
{
    return psp_decode(file, size, image);
}

#define expect_pixel(image, x, y, r, g, b, a) \
    expect_pixel_at(__LINE__, image, x, y, r, g, b, a)
static void expect_pixel_at(int line, const struct psp_image *image, unsigned x,
                            unsigned y, unsigned r, unsigned g, unsigned b,
                            unsigned a)
{
    const uint8_t *p = image->rgba + ((size_t)y * image->width + x) * 4u;
    if (p[0] != r || p[1] != g || p[2] != b || p[3] != a)
        fprintf(stderr, "line %d: pixel %u,%u is %u %u %u %u\n", line, x, y,
                p[0], p[1], p[2], p[3]);
    assert(p[0] == r && p[1] == g && p[2] == b && p[3] == a);
}

/* Planes for a width by height RGB layer of one colour. Each call gets a
   fresh buffer, from a ring of eight. */
static uint8_t planes[8][3 * 64 * 64];
static unsigned next_planes;
static const uint8_t *solid(unsigned width, unsigned height, unsigned r,
                            unsigned g, unsigned b)
{
    size_t n = (size_t)width * height;
    uint8_t *p = planes[next_planes++ % 8];
    assert(n * 3 <= sizeof planes[0]);
    memset(p, (int)r, n);
    memset(p + n, (int)g, n);
    memset(p + 2 * n, (int)b, n);
    return p;
}

static struct tlayer raster(unsigned width, unsigned height, const uint8_t *rgb)
{
    struct tlayer l;
    memset(&l, 0, sizeof l);
    l.type = 1;
    l.opacity = 255;
    l.visible = 1;
    l.width = width;
    l.height = height;
    l.colour = rgb;
    l.planes = 3;
    l.plane_bytes = width * height;
    return l;
}

/* A gradient layer's planes, distinct per channel and position. */
static uint8_t gradient_planes[3 * 16 * 8];
static const uint8_t *gradient(void)
{
    unsigned i;
    for (i = 0; i < 16 * 8; i++) {
        gradient_planes[i] = (uint8_t)(i * 2);
        gradient_planes[128 + i] = (uint8_t)(255 - i);
        gradient_planes[256 + i] = (uint8_t)(i * 7);
    }
    return gradient_planes;
}

static void check_gradient(const struct psp_image *image)
{
    unsigned i;
    assert(image->width == 16 && image->height == 8);
    for (i = 0; i < 16 * 8; i++)
        assert(image->rgba[i * 4] == (uint8_t)(i * 2) &&
               image->rgba[i * 4 + 1] == (uint8_t)(255 - i) &&
               image->rgba[i * 4 + 2] == (uint8_t)(i * 7) &&
               image->rgba[i * 4 + 3] == 255);
}

/* One opaque layer in each file format and compression. */
static void test_basic(void)
{
    static const unsigned versions[] = { 3, 4, 5, 6, 7, 13 };
    unsigned v, comp;
    struct psp_image image;

    for (v = 0; v < 6; v++) {
        for (comp = 0; comp < 3; comp++) {
            struct tlayer l = raster(16, 8, gradient());
            begin(versions[v]);
            compression = comp;
            image_block(16, 8, 24, 0, 1);
            open_bank();
            layer(&l);
            close_bank();
            assert(decode(&image) == CODEC_OK);
            check_gradient(&image);
            psp_free(&image);
        }
    }
}

/* Blocks the reader doesn't use are skipped, wherever they are. */
static void test_other_blocks(void)
{
    struct tlayer l = raster(16, 8, gradient());
    struct psp_image image;
    size_t at;

    begin(6);
    image_block(16, 8, 24, 0, 1);
    at = open_block(10);           /* extended data */
    put("~FL", 4); put16(0); put32(4); put32(0);
    close_block(at);
    at = open_block(1);            /* creator */
    put("~FL", 4); put16(0); put32(3); put("abc", 3);
    close_block(at);
    at = open_block(99);           /* unknown */
    zeros(7);
    close_block(at);
    open_bank();
    layer(&l);
    close_bank();
    at = open_block(6);            /* selection, after the layers */
    zeros(40);
    close_block(at);
    assert(decode(&image) == CODEC_OK);
    check_gradient(&image);
    psp_free(&image);
}

/* Transparency, opacity, the saved rectangle's offset and clipping. */
static void test_transparency(void)
{
    uint8_t trans[4 * 4], rgb[3 * 4 * 4];
    struct tlayer base, top;
    struct psp_image image;
    unsigned i, v;

    for (v = 3; v <= 6; v += 3) {
        for (i = 0; i < 16; i++)
            trans[i] = (uint8_t)(i < 8 ? 255 : 0);
        memcpy(rgb, solid(4, 4, 0, 0, 255), sizeof rgb);
        begin(v);
        image_block(6, 6, 24, 0, 2);
        open_bank();
        base = raster(6, 6, solid(6, 6, 255, 0, 0));
        layer(&base);
        top = raster(4, 4, rgb);
        top.x = -1;
        top.y = 3;
        top.trans = trans;
        top.opacity = 128;
        layer(&top);
        close_bank();
        assert(decode(&image) == CODEC_OK);
        assert(image.width == 6 && image.height == 6);
        /* Rows 3 and 4 of the canvas hold the layer's opaque rows 0, 1. */
        expect_pixel(&image, 0, 3, 127, 0, 128, 255);
        expect_pixel(&image, 2, 4, 127, 0, 128, 255);
        expect_pixel(&image, 3, 3, 255, 0, 0, 255);   /* past the layer */
        expect_pixel(&image, 0, 5, 255, 0, 0, 255);   /* transparent row */
        expect_pixel(&image, 0, 2, 255, 0, 0, 255);   /* above the layer */
        psp_free(&image);
    }

    /* Over nothing, a layer keeps its own transparency. */
    begin(5);
    image_block(4, 4, 24, 0, 1);
    open_bank();
    for (i = 0; i < 16; i++)
        trans[i] = (uint8_t)(i * 16);
    top = raster(4, 4, solid(4, 4, 10, 20, 30));
    top.trans = trans;
    layer(&top);
    close_bank();
    assert(decode(&image) == CODEC_OK);
    expect_pixel(&image, 0, 0, 0, 0, 0, 0);
    expect_pixel(&image, 1, 0, 10, 20, 30, 16);
    expect_pixel(&image, 3, 3, 10, 20, 30, 240);
    psp_free(&image);

    /* Hidden and zero-opacity layers draw nothing. */
    memset(trans, 255, sizeof trans);
    begin(6);
    image_block(2, 2, 24, 0, 3);
    open_bank();
    base = raster(2, 2, solid(2, 2, 1, 2, 3));
    layer(&base);
    top = raster(2, 2, solid(2, 2, 200, 200, 200));
    top.trans = trans;
    top.visible = 0;
    layer(&top);
    top.visible = 1;
    top.opacity = 0;
    layer(&top);
    close_bank();
    assert(decode(&image) == CODEC_OK);
    expect_pixel(&image, 1, 1, 1, 2, 3, 255);
    psp_free(&image);

    /* Paint Shop Pro ignores the opacity of a normal layer that has no
       transparency channel, as it writes a background layer; other modes
       still use it. */
    top.trans = NULL;
    top.opacity = 100;
    begin(6);
    image_block(2, 2, 24, 0, 2);
    open_bank();
    layer(&base);
    layer(&top);
    close_bank();
    assert(decode(&image) == CODEC_OK);
    expect_pixel(&image, 1, 1, 200, 200, 200, 255);
    psp_free(&image);
    top.blend = 2;
    begin(6);
    image_block(2, 2, 24, 0, 2);
    open_bank();
    layer(&base);
    layer(&top);
    close_bank();
    assert(decode(&image) == CODEC_OK);
    expect_pixel(&image, 1, 1, 79, 80, 80, 255);
    psp_free(&image);
}

#include "psp_cases.h"

/* Each case as a one-pixel, two-layer file, against Paint Shop Pro's own
   result. Dodge and burn divide by small numbers near the ends of their
   ranges, where rounding differs by more. */
static void test_blend(void)
{
    struct psp_image image;
    struct tlayer base, top;
    uint8_t lower[3], upper[3], lower_a, upper_a;
    size_t k;
    unsigned c;

    for (k = 0; k < sizeof psp_cases / sizeof psp_cases[0]; k++) {
        const struct psp_case *pc = &psp_cases[k];
        unsigned tolerance = pc->mode == 14 || pc->mode == 15 ? 9 : 3;
        for (c = 0; c < 3; c++) {
            lower[c] = pc->lower[c];
            upper[c] = pc->upper[c];
        }
        lower_a = pc->lower[3];
        upper_a = pc->upper[3];
        base = raster(1, 1, lower);
        base.plane_bytes = 1;
        if (pc->lower_trans)
            base.trans = &lower_a;
        top = raster(1, 1, upper);
        top.plane_bytes = 1;
        top.trans = &upper_a;
        top.blend = pc->mode;
        top.opacity = pc->opacity;
        begin(5);
        image_block(1, 1, 24, 0, 2);
        open_bank();
        layer(&base);
        layer(&top);
        close_bank();
        assert(decode(&image) == CODEC_OK);
        for (c = 0; c < 3; c++) {
            unsigned v = image.rgba[c], want = pc->result[c];
            if (pc->flattened)
                v = (unsigned)((v * image.rgba[3] + 255u * (255u - image.rgba[3]) + 127u) / 255u);
            if ((v > want ? v - want : want - v) > tolerance)
                fprintf(stderr, "case %lu mode %u: channel %u is %u, Paint Shop Pro %u\n",
                        (unsigned long)k, pc->mode, c, v, want);
            assert((v > want ? v - want : want - v) <= tolerance);
        }
        psp_free(&image);
    }

    /* Dissolve shows the layer or the lower colour, never a mix. */
    base = raster(1, 1, solid(1, 1, 10, 20, 30));
    base.plane_bytes = 1;
    top = raster(1, 1, solid(1, 1, 200, 100, 50));
    top.plane_bytes = 1;
    upper_a = 255;
    top.trans = &upper_a;
    top.blend = 9;
    top.opacity = 128;
    begin(6);
    image_block(1, 1, 24, 0, 2);
    open_bank();
    layer(&base);
    layer(&top);
    close_bank();
    assert(decode(&image) == CODEC_OK);
    assert((image.rgba[0] == 10 && image.rgba[1] == 20) ||
           (image.rgba[0] == 200 && image.rgba[1] == 100));
    psp_free(&image);

    /* An unknown mode is invalid. */
    top.blend = 100;
    begin(6);
    image_block(1, 1, 24, 0, 2);
    open_bank();
    layer(&base);
    layer(&top);
    close_bank();
    assert(decode(&image) == CODEC_INVALID);
}

/* User masks: placement, inversion, disabling, and the area outside. */
static void test_user_mask(void)
{
    uint8_t mask[2 * 2] = { 255, 0, 128, 255 };
    struct tlayer base, top;
    struct psp_image image;
    unsigned v;

    for (v = 3; v <= 6; v += 3) {
        begin(v);
        image_block(4, 4, 24, 0, 2);
        open_bank();
        base = raster(4, 4, solid(4, 4, 0, 0, 0));
        layer(&base);
        top = raster(4, 4, solid(4, 4, 255, 255, 255));
        top.mask = mask;
        top.mask_x = 1;
        top.mask_y = 1;
        top.mask_width = 2;
        top.mask_height = 2;
        layer(&top);
        close_bank();
        assert(decode(&image) == CODEC_OK);
        expect_pixel(&image, 0, 0, 0, 0, 0, 255);          /* outside */
        expect_pixel(&image, 1, 1, 255, 255, 255, 255);
        expect_pixel(&image, 2, 1, 0, 0, 0, 255);
        expect_pixel(&image, 1, 2, 128, 128, 128, 255);
        psp_free(&image);
    }

    top.mask_invert = 1;
    begin(6);
    image_block(4, 4, 24, 0, 2);
    open_bank();
    layer(&base);
    layer(&top);
    close_bank();
    assert(decode(&image) == CODEC_OK);
    expect_pixel(&image, 1, 1, 0, 0, 0, 255);
    expect_pixel(&image, 2, 1, 255, 255, 255, 255);
    expect_pixel(&image, 0, 0, 0, 0, 0, 255);
    psp_free(&image);

    top.mask_invert = 0;
    top.mask_disabled = 1;
    begin(6);
    image_block(4, 4, 24, 0, 2);
    open_bank();
    layer(&base);
    layer(&top);
    close_bank();
    assert(decode(&image) == CODEC_OK);
    expect_pixel(&image, 0, 0, 255, 255, 255, 255);
    expect_pixel(&image, 2, 1, 255, 255, 255, 255);
    psp_free(&image);
}

static struct tlayer mask_layer(const uint8_t *mask, long x, long y,
                                unsigned width, unsigned height)
{
    struct tlayer l;
    memset(&l, 0, sizeof l);
    l.type = 6;
    l.opacity = 255;
    l.visible = 1;
    l.mask = mask;
    l.mask_x = x;
    l.mask_y = y;
    l.mask_width = width;
    l.mask_height = height;
    return l;
}

static struct tlayer group(unsigned long children)
{
    struct tlayer l;
    memset(&l, 0, sizeof l);
    l.type = 5;
    l.opacity = 255;
    l.visible = 1;
    l.children = children;
    return l;
}

/* Mask layers mask what's below them in their group; groups composite
   their layers first. */
static void test_groups(void)
{
    uint8_t mask[2] = { 255, 0 };
    struct tlayer base, red, g, m, blue;
    struct psp_image image;

    base = raster(2, 2, solid(2, 2, 0, 255, 0));
    red = raster(2, 2, solid(2, 2, 255, 0, 0));
    blue = raster(2, 1, solid(2, 1, 0, 0, 255));
    m = mask_layer(mask, 0, 0, 2, 1);

    /* At the top level, a mask layer masks everything below it. */
    begin(6);
    image_block(2, 2, 24, 0, 3);
    open_bank();
    layer(&base);
    layer(&red);
    layer(&m);
    close_bank();
    assert(decode(&image) == CODEC_OK);
    expect_pixel(&image, 0, 0, 255, 0, 0, 255);
    /* Masked pixels keep their colour, fully transparent. */
    expect_pixel(&image, 1, 0, 255, 0, 0, 0);
    expect_pixel(&image, 0, 1, 255, 0, 0, 0);
    psp_free(&image);

    /* In a group, it masks only the group's layers below it. */
    begin(6);
    image_block(2, 2, 24, 0, 5);
    open_bank();
    layer(&base);
    g = group(2);
    layer(&g);
    layer(&red);
    layer(&m);
    layer(&blue);                   /* above the group */
    close_bank();
    assert(decode(&image) == CODEC_OK);
    expect_pixel(&image, 0, 0, 0, 0, 255, 255);
    expect_pixel(&image, 0, 1, 0, 255, 0, 255);
    psp_free(&image);
    begin(6);
    image_block(2, 2, 24, 0, 4);
    open_bank();
    layer(&base);
    layer(&g);
    layer(&red);
    layer(&m);
    close_bank();
    assert(decode(&image) == CODEC_OK);
    expect_pixel(&image, 0, 0, 255, 0, 0, 255);
    expect_pixel(&image, 1, 0, 0, 255, 0, 255);
    expect_pixel(&image, 1, 1, 0, 255, 0, 255);
    psp_free(&image);

    /* Group opacity applies to the composited group. */
    g.opacity = 0;
    begin(6);
    image_block(2, 2, 24, 0, 3);
    open_bank();
    layer(&base);
    layer(&g);
    layer(&red);
    layer(&blue);
    close_bank();
    assert(decode(&image) == CODEC_OK);
    expect_pixel(&image, 0, 0, 0, 255, 0, 255);
    psp_free(&image);

    /* A hidden group hides its layers, nested groups included; the
       layer after it is still drawn. */
    g.opacity = 255;
    g.visible = 0;
    begin(6);
    image_block(2, 2, 24, 0, 6);
    open_bank();
    layer(&base);
    layer(&g);
    {
        struct tlayer inner = group(1);
        layer(&inner);
        layer(&red);
    }
    layer(&red);
    layer(&blue);
    close_bank();
    assert(decode(&image) == CODEC_OK);
    expect_pixel(&image, 0, 0, 0, 0, 255, 255);
    expect_pixel(&image, 0, 1, 0, 255, 0, 255);
    psp_free(&image);

    /* A group that claims more layers than remain takes what there is. */
    g.visible = 1;
    g.children = 40;
    begin(6);
    image_block(2, 2, 24, 0, 3);
    open_bank();
    layer(&base);
    layer(&g);
    layer(&red);
    close_bank();
    assert(decode(&image) == CODEC_OK);
    expect_pixel(&image, 1, 1, 255, 0, 0, 255);
    psp_free(&image);

    /* Nesting past the limit is invalid. */
    {
        unsigned i;
        begin(6);
        image_block(2, 2, 24, 0, PSP_MAX_DEPTH + 3);
        open_bank();
        for (i = 0; i < PSP_MAX_DEPTH + 2; i++) {
            struct tlayer nest = group(1);
            layer(&nest);
        }
        layer(&red);
        close_bank();
        assert(decode(&image) == CODEC_INVALID);
    }
}

/* Vector layers need the stored composite; others prefer it only when it
   keeps what the layers show. */
static void test_stored(void)
{
    uint8_t alpha[4] = { 255, 0, 255, 128 };
    struct tlayer base, vec, clear;
    struct psp_image image;
    const uint8_t *stored_rgb;
    static uint8_t stored[12];

    memcpy(stored, solid(2, 2, 9, 99, 199), 12);
    stored_rgb = stored;
    base = raster(2, 2, solid(2, 2, 1, 2, 3));
    memset(&vec, 0, sizeof vec);
    vec.type = 3;
    vec.opacity = 255;
    vec.visible = 1;
    vec.vector = 1;

    begin(6);
    image_block(2, 2, 24, 0, 2);
    composite_bank(2, 2, stored_rgb, NULL, 0, 2);
    open_bank();
    layer(&base);
    layer(&vec);
    close_bank();
    assert(decode(&image) == CODEC_OK);
    expect_pixel(&image, 1, 1, 9, 99, 199, 255);
    psp_free(&image);

    /* With alpha, and uncompressed or RLE. */
    begin(6);
    image_block(2, 2, 24, 0, 2);
    composite_bank(2, 2, stored_rgb, alpha, 0, 0);
    open_bank();
    layer(&base);
    layer(&vec);
    close_bank();
    assert(decode(&image) == CODEC_OK);
    expect_pixel(&image, 1, 0, 9, 99, 199, 0);
    expect_pixel(&image, 1, 1, 9, 99, 199, 128);
    psp_free(&image);
    begin(6);
    image_block(2, 2, 24, 0, 2);
    composite_bank(2, 2, stored_rgb, alpha, 0, 1);
    open_bank();
    layer(&base);
    layer(&vec);
    close_bank();
    assert(decode(&image) == CODEC_OK);
    expect_pixel(&image, 1, 1, 9, 99, 199, 128);
    psp_free(&image);

    /* Only a JPEG composite, or none: the vector layer can't be shown. */
    begin(6);
    image_block(2, 2, 24, 0, 2);
    composite_bank(2, 2, stored_rgb, NULL, 1, 2);
    open_bank();
    layer(&base);
    layer(&vec);
    close_bank();
    assert(decode(&image) == CODEC_INVALID);
    begin(6);
    image_block(2, 2, 24, 0, 2);
    open_bank();
    layer(&base);
    layer(&vec);
    close_bank();
    assert(decode(&image) == CODEC_INVALID);

    /* A hidden vector layer doesn't need it. */
    vec.visible = 0;
    begin(6);
    image_block(2, 2, 24, 0, 2);
    open_bank();
    layer(&base);
    layer(&vec);
    close_bank();
    assert(decode(&image) == CODEC_OK);
    expect_pixel(&image, 0, 0, 1, 2, 3, 255);
    psp_free(&image);

    /* Opaque layers: the stored composite wins. */
    begin(6);
    image_block(2, 2, 24, 0, 1);
    composite_bank(2, 2, stored_rgb, NULL, 0, 2);
    open_bank();
    layer(&base);
    close_bank();
    assert(decode(&image) == CODEC_OK);
    expect_pixel(&image, 0, 0, 9, 99, 199, 255);
    psp_free(&image);

    /* Transparent layers and a composite without alpha: the layers win. */
    clear = raster(2, 2, solid(2, 2, 5, 6, 7));
    clear.trans = alpha;
    begin(6);
    image_block(2, 2, 24, 0, 1);
    composite_bank(2, 2, stored_rgb, NULL, 0, 2);
    open_bank();
    layer(&clear);
    close_bank();
    assert(decode(&image) == CODEC_OK);
    expect_pixel(&image, 0, 0, 5, 6, 7, 255);
    expect_pixel(&image, 1, 0, 0, 0, 0, 0);
    psp_free(&image);

    /* A damaged composite is ignored when the layers suffice... */
    begin(6);
    image_block(2, 2, 24, 0, 1);
    {
        size_t at = open_block(16);
        put32(8); put32(1);
        put("~BX", 4);
        close_block(at);
    }
    open_bank();
    layer(&base);
    close_bank();
    assert(decode(&image) == CODEC_OK);
    expect_pixel(&image, 0, 0, 1, 2, 3, 255);
    psp_free(&image);
    /* ...but not when they don't. */
    vec.visible = 1;
    begin(6);
    image_block(2, 2, 24, 0, 2);
    {
        size_t at = open_block(16);
        put32(8); put32(1);
        put("~BX", 4);
        close_block(at);
    }
    open_bank();
    layer(&base);
    layer(&vec);
    close_bank();
    assert(decode(&image) == CODEC_INVALID);
}

/* Greyscale, 16-bit, 48-bit and paletted images. */
static void test_depths(void)
{
    static const uint8_t pal[4][3] = { { 0, 0, 0 }, { 255, 0, 0 },
                                       { 0, 255, 0 }, { 10, 20, 30 } };
    uint8_t grey[4] = { 0, 100, 200, 255 }, deep[3 * 4 * 2], idx[4] = { 0, 1, 2, 3 };
    uint8_t bits[2 * 4], nibbles[1 * 4];
    struct tlayer l;
    struct psp_image image;
    unsigned i, v;

    for (v = 3; v <= 6; v += 3) {
        /* 8-bit grey */
        begin(v);
        image_block(4, 1, 8, 1, 1);
        open_bank();
        l = raster(4, 1, grey);
        l.planes = 1;
        layer(&l);
        close_bank();
        assert(decode(&image) == CODEC_OK);
        expect_pixel(&image, 1, 0, 100, 100, 100, 255);
        expect_pixel(&image, 3, 0, 255, 255, 255, 255);
        psp_free(&image);

        /* 8-bit paletted */
        begin(v);
        image_block(4, 1, 8, 0, 1);
        palette_block(pal, 4);
        open_bank();
        l = raster(4, 1, idx);
        l.planes = 1;
        layer(&l);
        close_bank();
        assert(decode(&image) == CODEC_OK);
        expect_pixel(&image, 1, 0, 255, 0, 0, 255);
        expect_pixel(&image, 3, 0, 10, 20, 30, 255);
        psp_free(&image);
    }

    /* 48-bit: 16-bit samples, little-endian, rounded to 8 bits. */
    for (i = 0; i < 12; i++) {
        unsigned value = i == 0 ? 0 : i == 1 ? 65535 : i == 2 ? 32896 : 257u * i;
        deep[i * 2] = (uint8_t)value;
        deep[i * 2 + 1] = (uint8_t)(value >> 8);
    }
    begin(6);
    image_block(4, 1, 48, 0, 1);
    open_bank();
    l = raster(4, 1, deep);
    l.plane_bytes = 8;
    layer(&l);
    close_bank();
    assert(decode(&image) == CODEC_OK);
    expect_pixel(&image, 0, 0, 0, 4, 8, 255);
    expect_pixel(&image, 1, 0, 255, 5, 9, 255);
    expect_pixel(&image, 2, 0, 128, 6, 10, 255);
    psp_free(&image);

    /* 16-bit grey */
    begin(6);
    image_block(4, 1, 16, 1, 1);
    open_bank();
    l = raster(4, 1, deep);
    l.planes = 1;
    l.plane_bytes = 8;
    layer(&l);
    close_bank();
    assert(decode(&image) == CODEC_OK);
    expect_pixel(&image, 1, 0, 255, 255, 255, 255);
    expect_pixel(&image, 3, 0, 3, 3, 3, 255);
    psp_free(&image);

    /* 1-bit, rows padded to 4 bytes as the spec says, and unpadded. */
    for (v = 0; v < 2; v++) {
        memset(bits, 0, sizeof bits);
        bits[0] = 0xa0;                        /* 1 0 1 0 */
        bits[v ? 1 : 4] = 0x50;                /* 0 1 0 1 */
        begin(6);
        image_block(4, 2, 1, 0, 1);
        palette_block(pal, 2);
        open_bank();
        l = raster(4, 2, bits);
        l.planes = 1;
        l.plane_bytes = v ? 2 : 8;
        layer(&l);
        close_bank();
        assert(decode(&image) == CODEC_OK);
        expect_pixel(&image, 0, 0, 255, 0, 0, 255);
        expect_pixel(&image, 1, 0, 0, 0, 0, 255);
        expect_pixel(&image, 1, 1, 255, 0, 0, 255);
        expect_pixel(&image, 2, 1, 0, 0, 0, 255);
        psp_free(&image);
    }

    /* 4-bit, and an index past the palette's end shows black. */
    nibbles[0] = 0x31;
    nibbles[1] = 0x2f;
    nibbles[2] = nibbles[3] = 0;
    begin(6);
    image_block(4, 1, 4, 0, 1);
    palette_block(pal, 4);
    open_bank();
    l = raster(4, 1, nibbles);
    l.planes = 1;
    l.plane_bytes = 4;
    layer(&l);
    close_bank();
    assert(decode(&image) == CODEC_OK);
    expect_pixel(&image, 0, 0, 10, 20, 30, 255);
    expect_pixel(&image, 1, 0, 255, 0, 0, 255);
    expect_pixel(&image, 2, 0, 0, 255, 0, 255);
    expect_pixel(&image, 3, 0, 0, 0, 0, 255);
    psp_free(&image);

    /* Paletted without a palette, and bad depth combinations. */
    begin(6);
    image_block(4, 1, 8, 0, 1);
    open_bank();
    l = raster(4, 1, idx);
    l.planes = 1;
    layer(&l);
    close_bank();
    assert(decode(&image) == CODEC_INVALID);
    begin(6);
    image_block(4, 1, 16, 0, 0);
    open_bank();
    close_bank();
    assert(decode(&image) == CODEC_INVALID);
    begin(6);
    image_block(4, 1, 12, 0, 0);
    open_bank();
    close_bank();
    assert(decode(&image) == CODEC_INVALID);
    {
        uint8_t big[300][3];
        memset(big, 0, sizeof big);
        begin(6);
        image_block(4, 1, 8, 0, 0);
        palette_block((const uint8_t (*)[3])big, 300);
        open_bank();
        close_bank();
        assert(decode(&image) == CODEC_INVALID);
    }
}

/* RLE runs that overrun the plane are cut short. */
static void test_rle_overrun(void)
{
    struct tlayer l = raster(2, 2, NULL);
    struct psp_image image;
    size_t at;
    unsigned c;

    begin(6);
    compression = 1;
    image_block(2, 2, 24, 0, 1);
    open_bank();
    at = open_block(4);
    {
        size_t start = size;
        put32(0); put16(1); put("L", 1); put8(1);
        rect(0, 0, 2, 2); rect(0, 0, 2, 2);
        put8(255); put8(0); put8(1); put8(0); put8(0);
        zeros(32); put8(0); put8(0); put8(0); put16(0);
        for (c = 0; c < 10; c++) put("\0\0\xff\xff", 4);
        put8(0); put32(0);
        patch32(start, (uint32_t)(size - start));
    }
    put32(8); put16(1); put16(3);
    for (c = 0; c < 3; c++) {
        static const uint8_t run[] = { 128 + 100, 77 };
        size_t ch = open_block(5);
        put32(16); put32(2); put32(4); put16(0); put16(c + 1);
        put(run, 2);
        close_block(ch);
    }
    close_block(at);
    close_bank();
    (void)l;
    assert(decode(&image) == CODEC_OK);
    expect_pixel(&image, 1, 1, 77, 77, 77, 255);
    psp_free(&image);
}

/* A channel too short for its plane is truncated data. */
static void test_short_channel(void)
{
    struct tlayer l = raster(16, 8, gradient());
    struct psp_image image;
    unsigned comp;

    for (comp = 0; comp < 3; comp++) {
        begin(6);
        compression = comp;
        image_block(16, 9, 24, 0, 1);
        open_bank();
        l.height = 9;               /* claims a row more than it stores */
        l.plane_bytes = 16 * 8;
        layer(&l);
        close_bank();
        assert(decode(&image) == CODEC_TRUNCATED);
    }
}

/* Every prefix of a file is rejected, as truncated where a block is cut. */
static void test_truncation(void)
{
    uint8_t trans[16 * 8];
    struct tlayer l = raster(16, 8, gradient()), m;
    struct psp_image image;
    size_t full, n;
    unsigned v;
    static uint8_t copy[1 << 16];

    memset(trans, 200, sizeof trans);
    for (v = 3; v <= 6; v += 3) {
        begin(v);
        image_block(16, 8, 24, 0, 2);
        if (v >= 4)
            composite_bank(16, 8, gradient(), trans, 0, 2);
        open_bank();
        l.trans = trans;
        layer(&l);
        m = raster(16, 8, gradient());
        m.mask = trans;
        m.mask_width = 16;
        m.mask_height = 8;
        layer(&m);
        close_bank();
        full = size;
        assert(full <= sizeof copy);
        memcpy(copy, file, full);
        assert(psp_decode(copy, full, &image) == CODEC_OK);
        psp_free(&image);
        for (n = 0; n < full; n++) {
            enum codec_result r = psp_decode(copy, n, &image);
            assert(r == CODEC_TRUNCATED || r == CODEC_INVALID);
            if (n >= 36 + 14)
                assert(r == CODEC_TRUNCATED);
        }
    }
}

static void test_header(void)
{
    struct tlayer l = raster(16, 8, gradient());
    struct psp_image image;

    begin(6);
    image_block(16, 8, 24, 0, 1);
    open_bank();
    layer(&l);
    close_bank();
    assert(decode(&image) == CODEC_OK);
    psp_free(&image);

    file[3] = 'X';
    assert(decode(&image) == CODEC_INVALID);
    file[3] = 'n';
    file[28] = 1;                       /* signature padding */
    assert(decode(&image) == CODEC_INVALID);
    file[28] = 0;
    psp_put16(file + 32, 2);            /* before PSP 5 */
    assert(decode(&image) == CODEC_INVALID);
    psp_put16(file + 32, 6);
    memcpy(file + 36, "~BX", 4);
    assert(decode(&image) == CODEC_INVALID);
    assert(psp_decode(file, 20, &image) == CODEC_TRUNCATED);
    assert(psp_decode((const uint8_t *)"GIF89a....................................", 40, &image) == CODEC_INVALID);

    /* The general image attributes come first. */
    begin(6);
    open_bank();
    close_bank();
    image_block(16, 8, 24, 0, 1);
    assert(decode(&image) == CODEC_INVALID);
    /* A file without a layer bank has been cut short. */
    begin(6);
    image_block(16, 8, 24, 0, 0);
    assert(decode(&image) == CODEC_TRUNCATED);
    /* Layers only. */
    begin(6);
    image_block(16, 8, 24, 0, 1);
    open_bank();
    {
        size_t at = open_block(5);
        zeros(16);
        close_block(at);
    }
    close_bank();
    assert(decode(&image) == CODEC_INVALID);
    /* An empty layer bank is an empty, transparent canvas. */
    begin(6);
    image_block(3, 2, 24, 0, 0);
    open_bank();
    close_bank();
    assert(decode(&image) == CODEC_OK);
    expect_pixel(&image, 2, 1, 0, 0, 0, 0);
    psp_free(&image);
}

static void test_limits(void)
{
    struct psp_image image;
    struct tlayer l = raster(2, 2, solid(2, 2, 1, 1, 1));

    begin(6);
    image_block(65536, 1, 24, 0, 0);
    open_bank();
    close_bank();
    assert(decode(&image) == CODEC_TOO_LARGE);
    begin(6);
    image_block(5000, 5000, 24, 0, 0);
    open_bank();
    close_bank();
    assert(decode(&image) == CODEC_TOO_LARGE);
    begin(6);
    image_block(0, 5, 24, 0, 0);
    open_bank();
    close_bank();
    assert(decode(&image) == CODEC_INVALID);
    begin(6);
    image_block(4096, 4096, 24, 0, 0);
    open_bank();
    close_bank();
    assert(decode(&image) == CODEC_OK);
    psp_free(&image);

    /* Compression 3 (JPEG) is only for composites. */
    begin(6);
    compression = 3;
    image_block(2, 2, 24, 0, 0);
    open_bank();
    close_bank();
    assert(decode(&image) == CODEC_INVALID);

    /* A layer rectangle that is inside out, or far too big. */
    begin(6);
    image_block(2, 2, 24, 0, 1);
    open_bank();
    layer(&l);
    close_bank();
    {
        /* The saved rectangle's right edge, in the only layer. */
        size_t at = 36 + 10 + 46 + 10 + 10 + 4 + 2 + 5 + 1 + 16 + 8;
        assert(psp_le32(file + at) == 5);
        psp_put32(file + at, 1);
        assert(decode(&image) == CODEC_INVALID);
        psp_put32(file + at, 3);
        psp_put32(file + at + 4, 200000);
        assert(decode(&image) == CODEC_INVALID);
        psp_put32(file + at, 7000);
        psp_put32(file + at + 4, 7000);
        assert(decode(&image) == CODEC_TOO_LARGE);
    }

    /* A block length past the end of the file is truncation; past the end
       of its parent block, with more of the file after it, is invalid. */
    begin(6);
    image_block(2, 2, 24, 0, 1);
    open_bank();
    layer(&l);
    close_bank();
    patch32(36 + 10 + 46 + 6, 0x100000);
    assert(decode(&image) == CODEC_TRUNCATED);
    begin(6);
    image_block(2, 2, 24, 0, 1);
    open_bank();
    layer(&l);
    close_bank();
    {
        size_t at = open_block(10);
        zeros(64);
        close_block(at);
    }
    assert(decode(&image) == CODEC_OK);
    psp_free(&image);
    patch32(36 + 10 + 46 + 10 + 6, 0x40);
    assert(decode(&image) == CODEC_INVALID);
}

/* Encoding and decoding again gives the same picture. */
static void test_encode(void)
{
    static uint8_t rgba[37 * 23 * 4];
    struct psp_image image;
    uint8_t *out;
    size_t length, i;
    unsigned pass;

    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < 37 * 23; i++) {
            rgba[i * 4] = (uint8_t)(i * 3);
            rgba[i * 4 + 1] = (uint8_t)(i / 7);
            rgba[i * 4 + 2] = (uint8_t)(255 - i);
            rgba[i * 4 + 3] = pass ? (uint8_t)(i * 5) : 255;
        }
        assert(psp_encode(rgba, 37, 23, &out, &length) == CODEC_OK);
        assert(memcmp(out, "Paint Shop Pro Image File\n\x1a\0\0\0\0\0", 32) == 0);
        assert(psp_le16(out + 32) == 5);
        assert(psp_decode(out, length, &image) == CODEC_OK);
        assert(image.width == 37 && image.height == 23);
        if (pass) {
            /* Pixels with alpha 0 lose their colour. */
            for (i = 0; i < 37 * 23; i++) {
                if (rgba[i * 4 + 3] != 0)
                    assert(memcmp(image.rgba + i * 4, rgba + i * 4, 4) == 0);
                else
                    assert(image.rgba[i * 4 + 3] == 0);
            }
        } else {
            assert(memcmp(image.rgba, rgba, sizeof rgba) == 0);
        }
        psp_free(&image);
        /* The channel count says whether there is a transparency mask. */
        assert(psp_le16(out + 36 + 10 + 46 + 10 + 10 + 129 + 6) == (pass ? 4 : 3));
        free(out);
    }
    assert(psp_encode(rgba, 0, 1, &out, &length) == CODEC_INVALID);
    assert(psp_encode(rgba, 65536, 1, &out, &length) == CODEC_INVALID);
    assert(psp_encode(rgba, 5000, 5000, &out, &length) == CODEC_TOO_LARGE);
}

int main(void)
{
    test_basic();
    test_other_blocks();
    test_transparency();
    test_blend();
    test_user_mask();
    test_groups();
    test_stored();
    test_depths();
    test_rle_overrun();
    test_short_channel();
    test_truncation();
    test_header();
    test_limits();
    test_encode();
    return 0;
}
