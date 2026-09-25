#include "encode.h"
#include "gimp.h"
#include <stdio.h>
#include <string.h>

/* GIMP's default spacing for exported brushes, as a percentage of the size. */
#define GBR_SPACING 10u

void gimp_scan_row(const uint8_t *rgba, unsigned width, unsigned *flags)
{
    unsigned x;
    for (x = 0; x < width; x++, rgba += 4) {
        if (rgba[3] != 255)
            *flags |= GIMP_SCAN_ALPHA;
        if (rgba[0] != rgba[1] || rgba[1] != rgba[2])
            *flags |= GIMP_SCAN_COLOUR;
    }
}

unsigned gbr_bytes(unsigned flags)
{
    return flags == 0 ? 1u : 4u;
}

unsigned pat_bytes(unsigned flags)
{
    return (flags & GIMP_SCAN_COLOUR ? 3u : 1u) + (flags & GIMP_SCAN_ALPHA ? 1u : 0u);
}

/* The name as stored: at most GIMP_MAX_NAME bytes, one line, never empty. */
static size_t clean_name(const char *name, char *out)
{
    size_t n = 0;
    if (name != NULL)
        for (; name[n] != '\0' && n < GIMP_MAX_NAME; n++)
            out[n] = name[n] == '\n' || name[n] == '\r' ? ' ' : name[n];
    if (n == 0) {
        memcpy(out, "Untitled", 8);
        n = 8;
    }
    out[n] = '\0';
    return n;
}

static int size_ok(unsigned width, unsigned height)
{
    return width != 0 && height != 0 && width <= GIMP_MAX_SIDE &&
           height <= GIMP_MAX_SIDE;
}

size_t gbr_make_header(unsigned width, unsigned height, unsigned bytes,
                       const char *name, uint8_t *out)
{
    char text[GIMP_MAX_NAME + 1];
    size_t n = clean_name(name, text) + 1;

    if (!size_ok(width, height))
        return 0;
    gimp_put32(out, (uint32_t)(28u + n));
    gimp_put32(out + 4, 2);
    gimp_put32(out + 8, width);
    gimp_put32(out + 12, height);
    gimp_put32(out + 16, bytes);
    memcpy(out + 20, "GIMP", 4);
    gimp_put32(out + 24, GBR_SPACING);
    memcpy(out + 28, text, n);
    return 28u + n;
}

size_t gih_make_header(unsigned width, unsigned height, unsigned bytes,
                       const char *name, uint8_t *out)
{
    char text[GIMP_MAX_NAME + 1];
    size_t n;
    int line;

    if (!size_ok(width, height))
        return 0;
    clean_name(name, text);
    line = sprintf((char *)out,
                   "%s\n1 ncells:1 cellwidth:%u cellheight:%u step:100 dim:1 "
                   "cols:1 rows:1 placement:constant rank0:1 sel0:random\n",
                   text, width, height);
    n = gbr_make_header(width, height, bytes, name, out + line);
    return n == 0 ? 0 : (size_t)line + n;
}

size_t pat_make_header(unsigned width, unsigned height, unsigned bytes,
                       const char *name, uint8_t *out)
{
    char text[GIMP_MAX_NAME + 1];
    size_t n = clean_name(name, text) + 1;

    if (!size_ok(width, height))
        return 0;
    gimp_put32(out, (uint32_t)(24u + n));
    gimp_put32(out + 4, 1);
    gimp_put32(out + 8, width);
    gimp_put32(out + 12, height);
    gimp_put32(out + 16, bytes);
    memcpy(out + 20, "GPAT", 4);
    memcpy(out + 24, text, n);
    return 24u + n;
}

void gbr_encode_row(const uint8_t *rgba, unsigned width, unsigned bytes,
                    uint8_t *out)
{
    unsigned x;
    if (bytes == 4) {
        memcpy(out, rgba, (size_t)width * 4u);
        return;
    }
    for (x = 0; x < width; x++)
        out[x] = (uint8_t)(255u - rgba[x * 4u]);
}

void pat_encode_row(const uint8_t *rgba, unsigned width, unsigned bytes,
                    uint8_t *out)
{
    unsigned x;
    for (x = 0; x < width; x++, rgba += 4) {
        switch (bytes) {
        case 4:
            out[3] = rgba[3];
            /* fall through */
        case 3:
            out[0] = rgba[0];
            out[1] = rgba[1];
            out[2] = rgba[2];
            break;
        case 2:
            out[1] = rgba[3];
            /* fall through */
        default:
            out[0] = rgba[0];
            break;
        }
        out += bytes;
    }
}

#define XCF_LAYER_NAME "Background"

static size_t xcf_tile_count(unsigned width, unsigned height)
{
    return (size_t)((width + 63u) / 64u) * ((height + 63u) / 64u);
}

/* Up to the layer: magic 14, image 12, compression property 9, end 8, layer
   list 8, channel list 4. The layer: size and type 12, name 4 + 11,
   properties 48, pointers 8. Hierarchy 12 + level list 8, level size 8.
   Then the tile table. */
#define XCF_TO_LAYER (14u + 12u + 9u + 8u + 8u + 4u)
#define XCF_FIXED (XCF_TO_LAYER + 12u + 4u + sizeof XCF_LAYER_NAME + 48u + 8u + 20u + 8u)

size_t xcf_header_size(unsigned width, unsigned height)
{
    return XCF_FIXED + (xcf_tile_count(width, height) + 1u) * 4u;
}

size_t xcf_make_header(unsigned width, unsigned height, unsigned bytes,
                       const uint32_t *sizes, uint8_t *out)
{
    size_t header = xcf_header_size(width, height), hierarchy, i, data;
    size_t tiles = xcf_tile_count(width, height);
    uint8_t *p = out;

    if (!size_ok(width, height) || (uint64_t)width * height > GIMP_MAX_PIXELS)
        return 0;
    memcpy(p, "gimp xcf file", 14);
    gimp_put32(p + 14, width);
    gimp_put32(p + 18, height);
    gimp_put32(p + 22, 0);                  /* RGB */
    gimp_put32(p + 26, 17);                 /* compression: RLE */
    gimp_put32(p + 30, 1);
    p[34] = 1;
    gimp_put32(p + 35, 0);                  /* end */
    gimp_put32(p + 39, 0);
    gimp_put32(p + 43, XCF_TO_LAYER);       /* the one layer */
    gimp_put32(p + 47, 0);
    gimp_put32(p + 51, 0);                  /* no channels */
    p += XCF_TO_LAYER;
    gimp_put32(p, width);
    gimp_put32(p + 4, height);
    gimp_put32(p + 8, bytes == 4 ? 1u : 0u);
    gimp_put32(p + 12, (uint32_t)sizeof XCF_LAYER_NAME);
    memcpy(p + 16, XCF_LAYER_NAME, sizeof XCF_LAYER_NAME);
    p += 16 + sizeof XCF_LAYER_NAME;
    gimp_put32(p, 6);                       /* opacity */
    gimp_put32(p + 4, 4);
    gimp_put32(p + 8, 255);
    gimp_put32(p + 12, 8);                  /* visible */
    gimp_put32(p + 16, 4);
    gimp_put32(p + 20, 1);
    gimp_put32(p + 24, 15);                 /* offsets */
    gimp_put32(p + 28, 8);
    gimp_put32(p + 32, 0);
    gimp_put32(p + 36, 0);
    gimp_put32(p + 40, 0);                  /* end */
    gimp_put32(p + 44, 0);
    p += 48;
    hierarchy = (size_t)(p - out) + 8u;
    gimp_put32(p, (uint32_t)hierarchy);
    gimp_put32(p + 4, 0);                   /* no mask */
    p += 8;
    gimp_put32(p, width);
    gimp_put32(p + 4, height);
    gimp_put32(p + 8, bytes);
    gimp_put32(p + 12, (uint32_t)(hierarchy + 20u));
    gimp_put32(p + 16, 0);                  /* no smaller levels */
    gimp_put32(p + 20, width);
    gimp_put32(p + 24, height);
    p += 28;
    data = header;
    for (i = 0; i < tiles; i++) {
        gimp_put32(p, (uint32_t)data);
        p += 4;
        data += sizes[i];
    }
    gimp_put32(p, 0);
    return header;
}

/* One byte plane of a tile as XCF's RLE: runs of three or more as repeats,
   the rest as literals, 16-bit lengths past 127. */
static size_t rle_plane(const uint8_t *rgba, unsigned width, unsigned x0,
                        unsigned tw, unsigned th, unsigned plane, uint8_t *out)
{
    unsigned n = tw * th, i = 0, k;
    size_t len = 0;
#define AT(j) rgba[((size_t)((j) / tw) * width + x0 + (j) % tw) * 4u + plane]
#define PUT(v) do { if (out != NULL) out[len] = (uint8_t)(v); len++; } while (0)
    while (i < n) {
        unsigned run = 1;
        while (i + run < n && AT(i + run) == AT(i))
            run++;
        if (run >= 3) {
            if (run < 128) {
                PUT(run - 1);
            } else {
                PUT(127);
                PUT(run >> 8);
                PUT(run);
            }
            PUT(AT(i));
            i += run;
            continue;
        }
        /* A literal, up to the next run of three. */
        for (run = 0; i + run < n; run++)
            if (i + run + 2 < n && AT(i + run) == AT(i + run + 1) &&
                AT(i + run) == AT(i + run + 2))
                break;
        if (run < 128) {
            PUT(256u - run);
        } else {
            PUT(128);
            PUT(run >> 8);
            PUT(run);
        }
        for (k = 0; k < run; k++)
            PUT(AT(i + k));
        i += run;
    }
#undef AT
#undef PUT
    return len;
}

size_t xcf_encode_tiles(const uint8_t *rgba, unsigned width, unsigned rows,
                        unsigned bytes, uint8_t *out, uint32_t *sizes)
{
    size_t total = 0;
    unsigned x0, plane;
    for (x0 = 0; x0 < width; x0 += 64u) {
        unsigned tw = width - x0 < 64u ? width - x0 : 64u;
        size_t tile = 0;
        for (plane = 0; plane < bytes; plane++)
            tile += rle_plane(rgba, width, x0, tw, rows, plane,
                              out != NULL ? out + total + tile : NULL);
        if (sizes != NULL)
            sizes[x0 / 64u] = (uint32_t)tile;
        total += tile;
    }
    return total;
}
