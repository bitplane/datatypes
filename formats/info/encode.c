#include "encode.h"

#include <string.h>

#include "common/zlib.h"

#define DISKOBJECT_SIZE 78u
#define IMAGE_SIZE 20u
#define MAX_COLOURS 256u

/* The OS 2 Workbench pens the old image is drawn in. */
static const uint8_t pens[4][3] = {
    {0xaa, 0xaa, 0xaa}, {0x00, 0x00, 0x00}, {0xff, 0xff, 0xff}, {0x66, 0x88, 0xbb}
};

static uint8_t *put16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
    return p + 2;
}

static uint8_t *put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
    return p + 4;
}

static size_t planar_size(unsigned width, unsigned height)
{
    return (size_t)((width + 15) >> 4) * 2u * height * 2u;
}

/* The icon at most, then pixels * 4 of scratch space for indexes or ARGB. */
static size_t output_size(unsigned width, unsigned height)
{
    size_t pixels = (size_t)width * height;
    size_t imag = 8 + 10 + pixels + MAX_COLOURS * 3u + 1;
    size_t argb = 8 + 10 + zlib_deflate_bound(pixels * 4u) + 1;

    return DISKOBJECT_SIZE + IMAGE_SIZE + planar_size(width, height) +
           12 + 8 + 6 + (imag > argb ? imag : argb);
}

size_t info_encode_capacity(unsigned width, unsigned height)
{
    if (width == 0 || height == 0 || width > INFO_ENCODE_MAX_SIDE ||
        height > INFO_ENCODE_MAX_SIDE)
        return 0;
    return output_size(width, height) + (size_t)width * height * 4u;
}

/* A Workbench 2 project icon with one image and no tool or tooltypes. */
static uint8_t *put_diskobject(uint8_t *p, unsigned width, unsigned height)
{
    memset(p, 0, DISKOBJECT_SIZE);
    put16(p, 0xe310);               /* WB_DISKMAGIC */
    put16(p + 2, 1);                /* WB_DISKVERSION */
    put16(p + 12, width);
    put16(p + 14, height);
    put16(p + 16, 0x0004);          /* GFLG_GADGIMAGE, complement when selected */
    put16(p + 18, 0x0003);          /* GACT_RELVERIFY | GACT_IMMEDIATE */
    put16(p + 20, 0x0001);          /* GTYP_BOOLGADGET */
    put32(p + 22, 1);               /* GadgetRender: an image follows */
    put32(p + 44, 1);               /* UserData: WB_DISKREVISION */
    p[48] = 4;                      /* WBPROJECT */
    put32(p + 58, 0x80000000u);     /* NO_ICON_POSITION */
    put32(p + 62, 0x80000000u);
    return p + DISKOBJECT_SIZE;
}

/* The picture over the grey background, in the nearest of the four pens. */
static uint8_t *put_planar(uint8_t *p, const uint8_t *rgba, unsigned width, unsigned height)
{
    size_t row = (size_t)((width + 15) >> 4) * 2u, plane = row * height;
    unsigned x, y, c, i;

    memset(p, 0, IMAGE_SIZE);
    put16(p + 4, width);
    put16(p + 6, height);
    put16(p + 8, 2);
    put32(p + 10, 1);               /* ImageData */
    p[14] = 3;                      /* PlanePick */
    p += IMAGE_SIZE;
    memset(p, 0, plane * 2u);
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            const uint8_t *s = rgba + ((size_t)y * width + x) * 4u;
            unsigned best = 0, best_distance = ~0u;
            for (i = 0; i < 4; i++) {
                unsigned distance = 0;
                for (c = 0; c < 3; c++) {
                    int v = (s[c] * s[3] + pens[0][c] * (255 - s[3]) + 127) / 255 - pens[i][c];
                    distance += (unsigned)(v * v);
                }
                if (distance < best_distance) {
                    best = i;
                    best_distance = distance;
                }
            }
            for (i = 0; i < 2; i++)
                if (best & 1u << i)
                    p[i * plane + y * row + x / 8] |= (uint8_t)(0x80u >> (x & 7));
        }
    }
    return p + plane * 2u;
}

struct palette {
    unsigned count, transparent;    /* transparent: index, or count if none */
    uint8_t rgb[MAX_COLOURS][3];
};

/* Index every pixel, or return 0 if the picture needs more than 256 colours
   or has partial transparency. Fully transparent pixels share index 0. */
static int make_palette(const uint8_t *rgba, size_t pixels, struct palette *palette,
                        uint8_t *indexes)
{
    size_t i;
    unsigned j;

    palette->count = 0;
    palette->transparent = MAX_COLOURS + 1;
    for (i = 0; i < pixels; i++)
        if (rgba[i * 4u + 3] == 0) {
            palette->transparent = 0;
            memset(palette->rgb[0], 0, 3);
            palette->count = 1;
            break;
        }
    for (i = 0; i < pixels; i++) {
        const uint8_t *s = rgba + i * 4u;
        if (s[3] == 0) {
            indexes[i] = 0;
            continue;
        }
        if (s[3] != 255)
            return 0;
        for (j = palette->transparent == 0 ? 1 : 0; j < palette->count; j++)
            if (memcmp(palette->rgb[j], s, 3) == 0)
                break;
        if (j == palette->count) {
            if (j == MAX_COLOURS)
                return 0;
            memcpy(palette->rgb[j], s, 3);
            palette->count++;
        }
        indexes[i] = (uint8_t)j;
    }
    if (palette->transparent != 0)
        palette->transparent = palette->count;
    return 1;
}

struct bits { uint8_t *p, *end; uint32_t buffer; unsigned have; };

static int emit(struct bits *b, unsigned value, unsigned n)
{
    b->buffer = b->buffer << n | (value & ((1u << n) - 1));
    b->have += n;
    while (b->have >= 8) {
        if (b->p == b->end)
            return 0;
        b->have -= 8;
        *b->p++ = (uint8_t)(b->buffer >> b->have);
    }
    return 1;
}

/* OS 3.5 run-length packing; 0 if it doesn't fit in limit bytes. */
static size_t pack35(const uint8_t *src, size_t count, unsigned depth, uint8_t *out, size_t limit)
{
    struct bits b;
    size_t i = 0, k;

    b.p = out;
    b.end = out + limit;
    b.buffer = 0;
    b.have = 0;
    while (i < count) {
        size_t run = 1, literal;
        while (i + run < count && run < 128 && src[i + run] == src[i])
            run++;
        /* A repeat costs a control byte; worth it once it saves more. */
        if ((run - 1) * depth > 8) {
            if (!emit(&b, 257 - run, 8) || !emit(&b, src[i], depth))
                return 0;
            i += run;
            continue;
        }
        for (literal = 0; i + literal < count && literal < 128; literal++) {
            size_t same = 1;
            while (i + literal + same < count && same < 128 &&
                   src[i + literal + same] == src[i + literal])
                same++;
            if (literal != 0 && (same - 1) * depth > 8)
                break;
        }
        if (!emit(&b, (unsigned)(literal - 1), 8))
            return 0;
        for (k = 0; k < literal; k++)
            if (!emit(&b, src[i + k], depth))
                return 0;
        i += literal;
    }
    if (b.have != 0 && !emit(&b, 0, 8 - b.have))
        return 0;
    return (size_t)(b.p - out);
}

static uint8_t *put_chunk_header(uint8_t *p, const char *id, size_t size)
{
    memcpy(p, id, 4);
    return put32(p + 4, (uint32_t)size);
}

static uint8_t *put_imag(uint8_t *p, const struct palette *palette, const uint8_t *indexes,
                         size_t pixels)
{
    uint8_t *h = p + 8, *data = h + 10;
    unsigned depth = 1, flags = 0x02;
    size_t size;

    while ((1u << depth) < palette->count)
        depth++;
    size = pack35(indexes, pixels, depth, data, pixels);
    if (size == 0) {
        memcpy(data, indexes, pixels);
        size = pixels;
        h[3] = 0;
    } else {
        h[3] = 1;
    }
    if (palette->transparent < palette->count)
        flags |= 0x01;
    h[0] = (uint8_t)(flags & 0x01 ? palette->transparent : 0);
    h[1] = (uint8_t)(palette->count - 1);
    h[2] = (uint8_t)flags;
    h[4] = 0;
    h[5] = (uint8_t)depth;
    put16(h + 6, (unsigned)(size - 1));
    put16(h + 8, palette->count * 3u - 1);
    memcpy(data + size, palette->rgb, palette->count * 3u);
    size += 10 + palette->count * 3u;
    put_chunk_header(p, "IMAG", size);
    p += 8 + size;
    if (size & 1)
        *p++ = 0;
    return p;
}

static uint8_t *put_argb(uint8_t *p, const uint8_t *rgba, size_t pixels, uint8_t *scratch)
{
    size_t i, packed;

    for (i = 0; i < pixels; i++) {
        scratch[i * 4u] = rgba[i * 4u + 3];
        memcpy(scratch + i * 4u + 1, rgba + i * 4u, 3);
    }
    if (zlib_deflate(scratch, pixels * 4u, p + 18, zlib_deflate_bound(pixels * 4u), 9,
                     &packed) != CODEC_OK)
        return NULL;
    put_chunk_header(p, "ARGB", 10 + packed);
    put32(p + 8, 1);                /* zlib */
    put32(p + 12, (uint32_t)packed);
    put16(p + 16, 0);
    p += 18 + packed;
    if (packed & 1)
        *p++ = 0;
    return p;
}

size_t info_encode(const uint8_t *rgba, unsigned width, unsigned height,
                   uint8_t *out, size_t capacity)
{
    struct palette palette;
    size_t pixels = (size_t)width * height, need = info_encode_capacity(width, height);
    uint8_t *p, *form, *face, *scratch;

    if (need == 0 || capacity < need)
        return 0;
    scratch = out + output_size(width, height);
    p = put_diskobject(out, width, height);
    p = put_planar(p, rgba, width, height);
    form = p;
    memcpy(form, "FORM\0\0\0\0ICON", 12);
    face = put_chunk_header(form + 12, "FACE", 6);
    face[0] = (uint8_t)(width - 1);
    face[1] = (uint8_t)(height - 1);
    face[2] = 0;
    face[3] = 0x11;                 /* square pixels */
    p = face + 6;
    if (make_palette(rgba, pixels, &palette, scratch)) {
        put16(face + 4, palette.count * 3u - 1);
        p = put_imag(p, &palette, scratch, pixels);
    } else {
        put16(face + 4, 0);
        p = put_argb(p, rgba, pixels, scratch);
        if (p == NULL)
            return 0;
    }
    put32(form + 4, (uint32_t)(p - form - 8));
    return (size_t)(p - out);
}
