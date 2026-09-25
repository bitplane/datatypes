#include "xcf.h"
#include "gimp.h"
#include "common/zlib.h"
#include <stdlib.h>
#include <string.h>

/* The newest version GIMP 3.2 writes and reads. */
#define XCF_MAX_VERSION 25
/* GIMP's largest image or layer side. */
#define XCF_MAX_LAYER_SIDE 524288u
#define XCF_MAX_LAYERS 65536L
/* Compressed tiles may be up to 1.5 times their raw size. */
#define XCF_MAX_TILE_DATA(bpp) (XCF_TILE * XCF_TILE * (bpp) * 3u / 2u)

enum prop {
    PROP_END = 0,
    PROP_COLORMAP = 1,
    PROP_FLOATING_SELECTION = 5,
    PROP_OPACITY = 6,
    PROP_MODE = 7,
    PROP_VISIBLE = 8,
    PROP_APPLY_MASK = 11,
    PROP_SHOW_MASK = 13,
    PROP_OFFSETS = 15,
    PROP_COMPRESSION = 17,
    PROP_GROUP_ITEM = 29,
    PROP_ITEM_PATH = 30,
    PROP_FLOAT_OPACITY = 33,
    PROP_COMPOSITE_MODE = 35,
    PROP_COMPOSITE_SPACE = 36,
    PROP_BLEND_SPACE = 37
};

/* Layer modes GIMP renames on load. */
#define MODE_OVERLAY_LEGACY 5
#define MODE_SOFTLIGHT_LEGACY 19

struct reader {
    const uint8_t *data;
    size_t length, pos;
    enum codec_result error;
};

static void fail(struct reader *r, enum codec_result error)
{
    if (r->error == CODEC_OK)
        r->error = error;
}

static int have(struct reader *r, size_t n)
{
    if (r->error != CODEC_OK)
        return 0;
    if (r->pos > r->length || r->length - r->pos < n) {
        fail(r, CODEC_TRUNCATED);
        return 0;
    }
    return 1;
}

static uint32_t read32(struct reader *r)
{
    uint32_t v;
    if (!have(r, 4))
        return 0;
    v = gimp_be32(r->data + r->pos);
    r->pos += 4;
    return v;
}

/* A file offset; one past the end of the file stands for anything larger. */
static size_t read_offset(struct reader *r, unsigned size)
{
    uint64_t v;
    if (size == 4)
        return read32(r);
    v = ((uint64_t)read32(r) << 32);
    v |= read32(r);
    return v > r->length ? r->length + 1 : (size_t)v;
}

static void skip(struct reader *r, size_t n)
{
    if (have(r, n))
        r->pos += n;
}

static void skip_string(struct reader *r)
{
    skip(r, read32(r));
}

/* Move to a structure an offset points at. It must lie after the pointer,
   as GIMP requires, which also rules out loops. */
static void seek(struct reader *r, size_t offset, size_t pointer)
{
    if (r->error != CODEC_OK)
        return;
    if (offset < pointer)
        fail(r, CODEC_INVALID);
    else if (offset >= r->length)
        fail(r, CODEC_TRUNCATED);
    else
        r->pos = offset;
}

static unsigned tile_count(unsigned width, unsigned height)
{
    return ((width + XCF_TILE - 1) / XCF_TILE) * ((height + XCF_TILE - 1) / XCF_TILE);
}

/* Read a hierarchy and its top level, checking its tile table. */
static void read_buffer(struct reader *r, struct xcf_file *xcf,
                        struct xcf_buffer *b)
{
    unsigned bpp = b->channels * xcf->bpc, i, n;
    size_t pointer, level, offset, next;

    if (read32(r) != b->width || read32(r) != b->height || read32(r) != bpp) {
        fail(r, CODEC_INVALID);
        return;
    }
    pointer = r->pos;
    level = read_offset(r, xcf->offset_size);
    /* Lower levels are unused. */
    seek(r, level, pointer);
    if (read32(r) != b->width || read32(r) != b->height) {
        fail(r, CODEC_INVALID);
        return;
    }
    b->tiles = r->pos;
    offset = read_offset(r, xcf->offset_size);
    if (r->error != CODEC_OK)
        return;
    if (offset == 0) {
        b->tiles = 0;
        return;
    }
    n = tile_count(b->width, b->height);
    /* Each entry takes its own bytes in the file, unless structures are
       shared to make a small file costly to read. */
    if (n > r->length / xcf->offset_size - xcf->entries) {
        fail(r, n > r->length / xcf->offset_size ? CODEC_TRUNCATED : CODEC_INVALID);
        return;
    }
    xcf->entries += n;
    for (i = 0; i < n; i++) {
        if (offset == 0) {
            fail(r, CODEC_INVALID);
            return;
        }
        if (offset > r->length) {
            fail(r, CODEC_TRUNCATED);
            return;
        }
        next = read_offset(r, xcf->offset_size);
        if (r->error != CODEC_OK)
            return;
        if (next != 0 && (next < offset || next - offset > XCF_MAX_TILE_DATA(bpp))) {
            fail(r, CODEC_INVALID);
            return;
        }
        offset = next;
    }
    /* The table ends after the last tile. */
    if (offset != 0)
        fail(r, CODEC_INVALID);
}

/* Payload of a property: at least need bytes, or the file is invalid. */
static const uint8_t *payload(struct reader *r, uint32_t size, uint32_t need)
{
    const uint8_t *p = r->data + r->pos;
    if (!have(r, size))
        return NULL;
    if (size < need) {
        fail(r, CODEC_INVALID);
        return NULL;
    }
    r->pos += size;
    return p;
}

static float be_float(const uint8_t *p)
{
    uint32_t bits = gimp_be32(p);
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

/* Layer properties. path gets the item path's first depth entries. */
static void read_layer_props(struct reader *r, struct xcf_file *xcf,
                             struct xcf_layer *layer, uint32_t *path,
                             unsigned *depth)
{
    for (;;) {
        uint32_t type = read32(r), size = read32(r);
        const uint8_t *p;
        uint32_t i;

        if (r->error != CODEC_OK || type == PROP_END) {
            skip(r, size);
            return;
        }
        switch (type) {
        case PROP_FLOATING_SELECTION:
            p = payload(r, size, xcf->offset_size);
            if (p != NULL) {
                layer->floating = 1;
                layer->float_target = xcf->offset_size == 4 ? gimp_be32(p) :
                    gimp_be32(p) != 0 ? (size_t)-1 : gimp_be32(p + 4);
            }
            break;
        case PROP_OPACITY:
            p = payload(r, size, 4);
            if (p != NULL) {
                uint32_t v = gimp_be32(p);
                layer->opacity = (v > 255 ? 255 : v) / 255.0f;
            }
            break;
        case PROP_FLOAT_OPACITY:
            p = payload(r, size, 4);
            if (p != NULL) {
                float v = be_float(p);
                /* NaN fails both tests and becomes opaque. */
                layer->opacity = v >= 0.0f ? (v <= 1.0f ? v : 1.0f) :
                                 v < 0.0f ? 0.0f : 1.0f;
            }
            break;
        case PROP_MODE:
            p = payload(r, size, 4);
            if (p != NULL) {
                layer->mode = (int)(gimp_be32(p) & 0x7fffffff);
                if (layer->mode == MODE_OVERLAY_LEGACY)
                    layer->mode = MODE_SOFTLIGHT_LEGACY;
            }
            break;
        case PROP_BLEND_SPACE:
        case PROP_COMPOSITE_SPACE:
        case PROP_COMPOSITE_MODE:
            p = payload(r, size, 4);
            if (p != NULL) {
                int v = (int)gimp_be32(p);
                if (v < -1000 || v > 1000)
                    v = 0;
                if (type == PROP_BLEND_SPACE)
                    layer->blend_space = v;
                else if (type == PROP_COMPOSITE_SPACE)
                    layer->composite_space = v;
                else
                    layer->composite_mode = v;
            }
            break;
        case PROP_VISIBLE:
            p = payload(r, size, 4);
            if (p != NULL)
                layer->visible = gimp_be32(p) != 0;
            break;
        case PROP_APPLY_MASK:
            p = payload(r, size, 4);
            if (p != NULL)
                layer->apply_mask = gimp_be32(p) != 0;
            break;
        case PROP_SHOW_MASK:
            p = payload(r, size, 4);
            if (p != NULL)
                layer->show_mask = gimp_be32(p) != 0;
            break;
        case PROP_OFFSETS:
            p = payload(r, size, 8);
            if (p != NULL) {
                long x = (long)(int32_t)gimp_be32(p), y = (long)(int32_t)gimp_be32(p + 4);
                /* GIMP resets offsets beyond its largest image. */
                layer->x = x < -(long)XCF_MAX_LAYER_SIDE || x > (long)XCF_MAX_LAYER_SIDE ? 0 : x;
                layer->y = y < -(long)XCF_MAX_LAYER_SIDE || y > (long)XCF_MAX_LAYER_SIDE ? 0 : y;
            }
            break;
        case PROP_GROUP_ITEM:
            if (payload(r, size, 0) != NULL)
                layer->group = 1;
            break;
        case PROP_ITEM_PATH:
            p = payload(r, size, 0);
            if (p != NULL) {
                *depth = 0;
                for (i = 0; i + 4 <= size && *depth < 64; i += 4)
                    path[(*depth)++] = gimp_be32(p + i);
            }
            break;
        default:
            skip(r, size);
            break;
        }
    }
}

/* Skip a channel's properties. */
static void skip_props(struct reader *r)
{
    for (;;) {
        uint32_t type = read32(r), size = read32(r);
        skip(r, size);
        if (r->error != CODEC_OK || type == PROP_END)
            return;
    }
}

/* Non-destructive filters: note whether any is visible. */
static int read_effect(struct reader *r, const struct xcf_file *xcf)
{
    int visible = 0;
    skip_string(r);
    skip_string(r);
    skip_string(r);
    if (xcf->version >= 22)
        skip_string(r);
    for (;;) {
        uint32_t type = read32(r), size = read32(r);
        const uint8_t *p;
        if (r->error != CODEC_OK || type == PROP_END) {
            skip(r, size);
            break;
        }
        p = payload(r, size, type == PROP_VISIBLE ? 4 : 0);
        if (p != NULL && type == PROP_VISIBLE)
            visible = gimp_be32(p) != 0;
    }
    return visible;
}

static void read_layer(struct reader *r, struct xcf_file *xcf,
                       struct xcf_layer *layer, uint32_t *path, unsigned *depth)
{
    uint32_t width, height, type;
    size_t pointer, hierarchy, mask, effect;

    layer->start = r->pos;
    width = read32(r);
    height = read32(r);
    type = read32(r);
    skip_string(r);
    if (r->error != CODEC_OK)
        return;
    if (type > 5) {
        fail(r, CODEC_INVALID);
        return;
    }
    layer->base = type < 2 ? XCF_RGB : type < 4 ? XCF_GREY : XCF_INDEXED;
    layer->alpha = type & 1;
    /* Grey layers can float over masks in any image; others match it. */
    if (layer->base != XCF_GREY && layer->base != xcf->base) {
        fail(r, CODEC_INVALID);
        return;
    }
    layer->width = width;
    layer->height = height;
    layer->visible = 1;
    layer->opacity = 1.0f;
    layer->mode = 28;  /* normal, when the file has no mode */
    layer->apply_mask = 1;
    read_layer_props(r, xcf, layer, path, depth);
    if (r->error != CODEC_OK)
        return;
    if (!layer->group && (width == 0 || height == 0 || width > XCF_MAX_LAYER_SIDE ||
                          height > XCF_MAX_LAYER_SIDE)) {
        fail(r, CODEC_INVALID);
        return;
    }
    pointer = r->pos;
    hierarchy = read_offset(r, xcf->offset_size);
    mask = read_offset(r, xcf->offset_size);
    effect = xcf->version >= 20 ? read_offset(r, xcf->offset_size) : 0;
    if (r->error != CODEC_OK)
        return;
    /* A group's own pixels are unused; its children make its picture. */
    if (!layer->group) {
        layer->pixels.width = width;
        layer->pixels.height = height;
        layer->pixels.channels = (layer->base == XCF_RGB ? 3u : 1u) + (unsigned)layer->alpha;
        seek(r, hierarchy, pointer);
        read_buffer(r, xcf, &layer->pixels);
    }
    pointer += xcf->offset_size;
    if (mask != 0) {
        uint32_t mw, mh;
        seek(r, mask, pointer);
        layer->mask_start = r->pos;
        mw = read32(r);
        mh = read32(r);
        if (r->error == CODEC_OK &&
            (mw == 0 || mh == 0 || mw > XCF_MAX_LAYER_SIDE || mh > XCF_MAX_LAYER_SIDE))
            fail(r, CODEC_INVALID);
        skip_string(r);
        skip_props(r);
        hierarchy = read_offset(r, xcf->offset_size);
        seek(r, hierarchy, r->pos - xcf->offset_size);
        layer->mask.width = mw;
        layer->mask.height = mh;
        layer->mask.channels = 1;
        read_buffer(r, xcf, &layer->mask);
        layer->has_mask = 1;
    }
    pointer += xcf->offset_size;
    while (effect != 0 && r->error == CODEC_OK) {
        seek(r, effect, pointer);
        if (read_effect(r, xcf))
            layer->filtered = 1;
        r->pos = pointer;
        effect = read_offset(r, xcf->offset_size);
        pointer += xcf->offset_size;
    }
}

/* Where GIMP puts a layer: under the group its item path names, after the
   children already there. An item path that doesn't name a group means the
   top level. */
static long find_parent(const struct xcf_file *xcf, const uint32_t *path,
                        unsigned depth)
{
    long parent = -1, child = xcf->top;
    unsigned level, i;

    for (level = 0; level + 1 < depth; level++) {
        for (i = 0; child >= 0 && i < path[level]; i++)
            child = xcf->layers[child].next_sibling;
        if (child < 0 || !xcf->layers[child].group)
            return -1;
        parent = child;
        child = xcf->layers[child].first_child;
    }
    return parent;
}

static void attach(struct xcf_file *xcf, long index, long parent)
{
    long *link = parent < 0 ? &xcf->top : &xcf->layers[parent].first_child;
    while (*link >= 0)
        link = &xcf->layers[*link].next_sibling;
    *link = index;
    xcf->layers[index].parent = parent;
}

static void read_header(struct reader *r, struct xcf_file *xcf)
{
    const uint8_t *d = r->data;
    uint32_t precision = 150;

    if (!have(r, 14))
        return;
    if (memcmp(d + 9, "file", 5) == 0) {
        xcf->version = 0;
    } else if (d[9] == 'v' && d[13] == '\0' && d[10] >= '0' && d[10] <= '9' &&
               d[11] >= '0' && d[11] <= '9' && d[12] >= '0' && d[12] <= '9') {
        xcf->version = (d[10] - '0') * 100 + (d[11] - '0') * 10 + (d[12] - '0');
    } else {
        fail(r, CODEC_INVALID);
        return;
    }
    if (xcf->version > XCF_MAX_VERSION) {
        fail(r, CODEC_INVALID);
        return;
    }
    r->pos = 14;
    xcf->offset_size = xcf->version >= 11 ? 8 : 4;
    xcf->width = read32(r);
    xcf->height = read32(r);
    xcf->base = (enum xcf_base)read32(r);
    if (xcf->version >= 4)
        precision = read32(r);
    if (r->error != CODEC_OK)
        return;
    if (xcf->version == 4) {
        static const uint32_t old[5] = {150, 250, 300, 500, 600};
        precision = precision < 5 ? old[precision] : 0;
    } else if (xcf->version == 5 || xcf->version == 6) {
        /* These versions numbered the floating point precisions lower. */
        if (precision >= 400 && precision <= 550)
            precision += 100;
    }
    xcf->bpc = 1;
    xcf->linear = 0;
    switch (precision) {
    case 100: xcf->linear = 1; break;
    case 150: case 175: break;
    case 200: xcf->linear = 1; /* fall through */
    case 250: case 275: xcf->bpc = 2; break;
    case 300: xcf->linear = 1; /* fall through */
    case 350: case 375: xcf->bpc = 4; break;
    default:
        /* Floating point images are HDR, and anything else is unknown. */
        fail(r, CODEC_INVALID);
        return;
    }
    if ((unsigned)xcf->base > XCF_INDEXED ||
        (xcf->base == XCF_INDEXED && (xcf->bpc != 1 || xcf->linear))) {
        fail(r, CODEC_INVALID);
        return;
    }
    if (xcf->width == 0 || xcf->height == 0)
        fail(r, CODEC_INVALID);
    else if (xcf->width > GIMP_MAX_SIDE || xcf->height > GIMP_MAX_SIDE ||
             (uint64_t)xcf->width * xcf->height > GIMP_MAX_PIXELS)
        fail(r, CODEC_TOO_LARGE);
}

static void read_image_props(struct reader *r, struct xcf_file *xcf)
{
    for (;;) {
        uint32_t type = read32(r), size = read32(r), n, i;
        const uint8_t *p;

        if (r->error != CODEC_OK || type == PROP_END) {
            skip(r, size);
            return;
        }
        if (type == PROP_COLORMAP) {
            p = payload(r, size, 4);
            if (p == NULL)
                return;
            n = gimp_be32(p);
            /* Version 0 saved the colour map wrongly; GIMP substitutes a
               grey ramp. */
            if (n > 256 || (xcf->version > 0 && size - 4 < n * 3)) {
                fail(r, CODEC_INVALID);
                return;
            }
            xcf->colours = n;
            for (i = 0; i < n * 3; i++)
                xcf->colourmap[i] = xcf->version == 0 ? (uint8_t)(i / 3) : p[4 + i];
        } else if (type == PROP_COMPRESSION) {
            p = payload(r, size, 1);
            if (p == NULL)
                return;
            /* 3 is fractal compression, never implemented. */
            if (p[0] > 2) {
                fail(r, CODEC_INVALID);
                return;
            }
            xcf->compression = p[0];
        } else {
            skip(r, size);
        }
    }
}

enum codec_result xcf_parse(struct xcf_file *xcf)
{
    struct reader r;
    size_t pointer, offset;
    long capacity = 0, i;

    memset(&r, 0, sizeof r);
    r.data = xcf->data;
    r.length = xcf->length;
    read_header(&r, xcf);
    read_image_props(&r, xcf);
    xcf->top = xcf->floating = -1;
    while (r.error == CODEC_OK) {
        struct xcf_layer *layer;
        uint32_t path[64];
        unsigned depth = 0;
        long parent;

        offset = read_offset(&r, xcf->offset_size);
        if (r.error != CODEC_OK || offset == 0)
            break;
        pointer = r.pos;
        if (xcf->count == XCF_MAX_LAYERS) {
            fail(&r, CODEC_TOO_LARGE);
            break;
        }
        if (xcf->count == capacity) {
            long grow = capacity ? capacity * 2 : 16;
            void *more = realloc(xcf->layers, (size_t)grow * sizeof *xcf->layers);
            if (more == NULL) {
                fail(&r, CODEC_NO_MEMORY);
                break;
            }
            xcf->layers = more;
            capacity = grow;
        }
        layer = &xcf->layers[xcf->count];
        memset(layer, 0, sizeof *layer);
        layer->parent = layer->first_child = layer->next_sibling = -1;
        seek(&r, offset, pointer);
        read_layer(&r, xcf, layer, path, &depth);
        if (r.error != CODEC_OK)
            break;
        if (layer->floating) {
            /* The floating selection stays out of the layer tree. */
            xcf->floating = xcf->count++;
        } else {
            /* Top-level indexes in item paths count the floating selection. */
            if (xcf->floating >= 0 && depth > 0 && path[0] > 0)
                path[0]--;
            parent = find_parent(xcf, path, depth);
            attach(xcf, xcf->count++, parent);
        }
        r.pos = pointer;
    }
    if (r.error == CODEC_OK && xcf->count == 0)
        r.error = CODEC_INVALID;
    for (i = 0; r.error == CODEC_OK && i < xcf->count; i++)
        if (xcf->layers[i].filtered && xcf->layers[i].visible)
            r.error = CODEC_INVALID;
    return r.error;
}

static uint16_t component(const uint8_t *p, unsigned bpc, int big_endian)
{
    uint32_t v;
    if (bpc == 1)
        return (uint16_t)(p[0] * 257u);
    if (bpc == 2)
        return big_endian ? (uint16_t)((p[0] << 8) | p[1]) : (uint16_t)((p[1] << 8) | p[0]);
    v = big_endian ? gimp_be32(p) :
        ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
    return v >= 0xffff8000u ? 65535u : (uint16_t)((v + 0x8000u) >> 16);
}

/* RLE: each byte of a pixel is coded as its own plane of runs. */
static enum codec_result unrle(const uint8_t *in, size_t length, int clipped,
                               uint8_t *out, unsigned pixels, unsigned bpp)
{
    enum codec_result short_data = clipped ? CODEC_TRUNCATED : CODEC_INVALID;
    size_t at = 0;
    unsigned plane;

    for (plane = 0; plane < bpp; plane++) {
        uint8_t *o = out + plane;
        unsigned left = pixels;
        while (left > 0) {
            unsigned v, n, j;
            if (at >= length)
                return short_data;
            v = in[at++];
            n = v >= 128 ? 256u - v : v + 1u;
            if (n == 128) {
                if (length - at < 2)
                    return short_data;
                n = ((unsigned)in[at] << 8) | in[at + 1];
                at += 2;
            }
            if (n > left)
                return CODEC_INVALID;
            if (v >= 128) {
                if (length - at < n)
                    return short_data;
                for (j = 0; j < n; j++, o += bpp)
                    *o = in[at++];
            } else {
                if (at >= length)
                    return short_data;
                for (j = 0; j < n; j++, o += bpp)
                    *o = in[at];
                at++;
            }
            left -= n;
        }
    }
    return CODEC_OK;
}

static size_t table_entry(const struct xcf_file *xcf, size_t at)
{
    const uint8_t *p = xcf->data + at;
    uint64_t v;
    if (xcf->offset_size == 4)
        return gimp_be32(p);
    v = ((uint64_t)gimp_be32(p) << 32) | gimp_be32(p + 4);
    return v > xcf->length ? xcf->length + 1 : (size_t)v;
}

enum codec_result xcf_tile(const struct xcf_file *xcf, const struct xcf_buffer *b,
                           unsigned col, unsigned row, uint8_t *scratch,
                           uint16_t *out, unsigned *tw, unsigned *th)
{
    unsigned cols = (b->width + XCF_TILE - 1) / XCF_TILE;
    unsigned bpp = b->channels * xcf->bpc, pixels, i, n;
    size_t index = (size_t)row * cols + col, offset, next, length, need, written;
    int clipped = 0;
    enum codec_result result = CODEC_OK;

    *tw = b->width - col * XCF_TILE < XCF_TILE ? b->width - col * XCF_TILE : XCF_TILE;
    *th = b->height - row * XCF_TILE < XCF_TILE ? b->height - row * XCF_TILE : XCF_TILE;
    pixels = *tw * *th;
    n = pixels * b->channels;
    if (b->tiles == 0) {
        memset(out, 0, n * sizeof *out);
        return CODEC_OK;
    }
    /* read_buffer checked the table, so these offsets are sound. */
    offset = table_entry(xcf, b->tiles + index * xcf->offset_size);
    next = table_entry(xcf, b->tiles + (index + 1) * xcf->offset_size);
    length = next != 0 ? next - offset : XCF_MAX_TILE_DATA(bpp);
    if (length > xcf->length - offset) {
        length = xcf->length - offset;
        clipped = 1;
    }
    need = (size_t)pixels * bpp;
    switch (xcf->compression) {
    case 0:
        if (length < need)
            return clipped ? CODEC_TRUNCATED : CODEC_INVALID;
        memcpy(scratch, xcf->data + offset, need);
        break;
    case 1:
        result = unrle(xcf->data + offset, length, clipped, scratch, pixels, bpp);
        break;
    default:
        result = zlib_inflate(xcf->data + offset, length, scratch, need, &written);
        if (result == CODEC_TRUNCATED && !clipped)
            result = CODEC_INVALID;
        else if (result == CODEC_TOO_LARGE || (result == CODEC_OK && written != need))
            result = CODEC_INVALID;
        break;
    }
    if (result != CODEC_OK)
        return result;
    /* Deep components are big-endian from version 12, native (x86) before. */
    for (i = 0; i < n; i++)
        out[i] = component(scratch + (size_t)i * xcf->bpc, xcf->bpc, xcf->version >= 12);
    return CODEC_OK;
}

enum codec_result xcf_decode(const uint8_t *data, size_t length,
                             struct gimp_image *image)
{
    struct xcf_file xcf;
    enum codec_result result;

    memset(&xcf, 0, sizeof xcf);
    xcf.data = data;
    xcf.length = length;
    result = xcf_parse(&xcf);
    if (result == CODEC_OK) {
        image->rgba = malloc((size_t)xcf.width * xcf.height * 4u);
        if (image->rgba == NULL) {
            result = CODEC_NO_MEMORY;
        } else {
            result = xcf_composite(&xcf, image->rgba);
            if (result == CODEC_OK) {
                image->width = xcf.width;
                image->height = xcf.height;
            } else {
                free(image->rgba);
                image->rgba = NULL;
            }
        }
    }
    free(xcf.layers);
    return result;
}
