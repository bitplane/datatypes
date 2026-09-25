#include <stdlib.h>
#include <string.h>

#include "common/zlib.h"
#include "psp.h"

/* Block identifiers used here. */
enum {
    B_IMAGE = 0, B_COLOR = 2, B_LAYER_BANK = 3, B_LAYER = 4, B_CHANNEL = 5,
    B_COMPOSITE = 9, B_COMP_BANK = 16, B_COMP_ATTR = 17, B_JPEG = 18,
    B_GROUP_EXT = 25
};

/* Bitmap types of a channel. */
enum { DIB_IMAGE = 0, DIB_TRANS = 1, DIB_USER_MASK = 2, DIB_COMPOSITE = 8,
       DIB_COMPOSITE_TRANS = 9 };

/* Layer types, file format 4.0 and later. Format 3.0 has 0 (normal) and
   1 (floating selection), both raster. */
enum { L_UNDEFINED = 0, L_RASTER, L_FLOATING, L_VECTOR, L_ADJUSTMENT,
       L_GROUP, L_MASK, L_ART_MEDIA };

enum { COMP_NONE = 0, COMP_RLE, COMP_LZ77, COMP_JPEG };

static const uint8_t signature[32] = "Paint Shop Pro Image File\n\x1a";

struct file {
    const uint8_t *data;
    size_t length;
    unsigned major;
};

struct block {
    unsigned id;
    size_t body, length;   /* the block's contents */
    size_t init;           /* format 3.0: the initial chunk's length */
};

/* A channel's compressed data. */
struct channel { const uint8_t *data; size_t length; int present; };

struct attrs {
    unsigned width, height, depth, compression, grey;
    uint8_t palette[256][3];
    unsigned colours;
};

struct layer {
    unsigned type, opacity, blend, visible;
    int64_t x, y;              /* canvas position of the saved rectangle */
    unsigned width, height;    /* the saved rectangle */
    int64_t mask_x, mask_y;
    unsigned mask_width, mask_height, mask_disabled, mask_invert;
    unsigned long children;    /* group layers */
    struct channel colour[4];  /* by channel type: 0 single, 1-3 RGB */
    struct channel trans, mask;
};

struct canvas { unsigned width, height; uint8_t *rgba; };

/* Read the block header at offset, which must lie within end. */
static enum codec_result read_block(const struct file *f, size_t offset,
                                    size_t end, struct block *b)
{
    size_t header = f->major < 4 ? 14u : 10u;
    uint32_t length;

    if (end - offset < header)
        return end < f->length ? CODEC_INVALID : CODEC_TRUNCATED;
    if (memcmp(f->data + offset, "~BK", 4) != 0)
        return CODEC_INVALID;
    b->id = psp_le16(f->data + offset + 4);
    if (f->major < 4) {
        b->init = psp_le32(f->data + offset + 6);
        length = psp_le32(f->data + offset + 10);
    } else {
        b->init = 0;
        length = psp_le32(f->data + offset + 6);
    }
    b->body = offset + header;
    if (length > end - b->body)
        return end < f->length || b->body + length <= f->length ?
               CODEC_INVALID : CODEC_TRUNCATED;
    b->length = length;
    if (b->init > length)
        return CODEC_INVALID;
    return CODEC_OK;
}

/* ---- the general image attributes and palette ---- */

static enum codec_result read_attrs(const struct file *f, const struct block *b,
                                    struct attrs *a)
{
    const uint8_t *p = f->data + b->body;
    size_t length = b->length;

    if (f->major >= 4) {
        uint32_t chunk;
        if (length < 4)
            return CODEC_INVALID;
        chunk = psp_le32(p);
        if (chunk < 42 || chunk > length)
            return CODEC_INVALID;
        p += 4;
        length = chunk - 4;
    }
    if (length < 38)
        return CODEC_INVALID;
    if (psp_le32(p) > PSP_MAX_SIDE || psp_le32(p + 4) > PSP_MAX_SIDE)
        return CODEC_TOO_LARGE;
    a->width = psp_le32(p);
    a->height = psp_le32(p + 4);
    if (a->width == 0 || a->height == 0)
        return CODEC_INVALID;
    if ((uint64_t)a->width * a->height > PSP_MAX_PIXELS)
        return CODEC_TOO_LARGE;
    a->compression = psp_le16(p + 17);
    a->depth = psp_le16(p + 19);
    a->grey = p[27] != 0;
    if (a->compression > COMP_LZ77)
        return CODEC_INVALID;
    switch (a->depth) {
    case 1: case 4:
        a->grey = 0;
        break;
    case 8: case 24: case 48:
        break;
    case 16:
        if (!a->grey)
            return CODEC_INVALID;
        break;
    default:
        return CODEC_INVALID;
    }
    if (a->depth >= 24)
        a->grey = 0;
    return CODEC_OK;
}

/* A colour palette block or sub-block. Entries are stored blue first. */
static enum codec_result read_palette(const struct file *f, const struct block *b,
                                      uint8_t palette[256][3], unsigned *colours)
{
    const uint8_t *p = f->data + b->body;
    size_t start = 4;
    uint32_t count, i;

    if (b->length < 4)
        return CODEC_INVALID;
    if (f->major >= 4) {
        if (b->length < 8)
            return CODEC_INVALID;
        start = psp_le32(p);
        if (start < 8 || start > b->length)
            return CODEC_INVALID;
        count = psp_le32(p + 4);
    } else {
        count = psp_le32(p);
    }
    if (count > 256)
        return CODEC_INVALID;
    if ((b->length - start) / 4 < count)
        return CODEC_INVALID;
    for (i = 0; i < count; i++) {
        const uint8_t *e = p + start + i * 4u;
        palette[i][0] = e[2];
        palette[i][1] = e[1];
        palette[i][2] = e[0];
    }
    *colours = count;
    return CODEC_OK;
}

/* ---- channels ---- */

/* Parse the channel sub-block b into its type, bitmap type and data. */
static enum codec_result read_channel(const struct file *f, const struct block *b,
                                      unsigned *bitmap, unsigned *type,
                                      struct channel *c)
{
    const uint8_t *p = f->data + b->body;
    size_t start;
    uint32_t compressed;

    if (f->major >= 4) {
        if (b->length < 16)
            return CODEC_INVALID;
        start = psp_le32(p);
        if (start < 16 || start > b->length)
            return CODEC_INVALID;
        p += 4;
    } else {
        start = b->init;
        if (start < 12)
            return CODEC_INVALID;
    }
    compressed = psp_le32(p);
    *bitmap = psp_le16(p + 8);
    *type = psp_le16(p + 10);
    if (compressed > b->length - start)
        return b->body + b->length == f->length ? CODEC_TRUNCATED : CODEC_INVALID;
    c->data = f->data + b->body + start;
    c->length = compressed;
    c->present = 1;
    return CODEC_OK;
}

/* Decode a PSP RLE stream into out; returns the bytes produced. A run that
   overruns out is cut short. */
static size_t unrle(const uint8_t *src, size_t length, uint8_t *out, size_t size)
{
    size_t i = 0, o = 0;

    while (i < length && o < size) {
        unsigned count = src[i++];
        if (count > 128) {
            count -= 128;
            if (i >= length)
                break;
            if (count > size - o)
                count = (unsigned)(size - o);
            memset(out + o, src[i++], count);
        } else {
            if (count > length - i)
                count = (unsigned)(length - i);
            if (count > size - o)
                count = (unsigned)(size - o);
            memcpy(out + o, src + i, count);
            i += count;
        }
        o += count;
    }
    return o;
}

/* Decompress a channel of width by height samples of the given bit depth
   (1, 4, 8 or 16) into one byte per sample: indexes for 1 and 4 bits, and
   16-bit samples rounded to 8. The spec pads rows to 4 bytes, but files
   store 8-bit channels unpadded; the decoded length says which. */
static enum codec_result decode_plane(const struct channel *c, unsigned compression,
                                      unsigned width, unsigned height,
                                      unsigned bits, uint8_t *out)
{
    size_t row = ((size_t)width * bits + 7u) / 8u;
    size_t padded = (row + 3u) & ~(size_t)3u;
    size_t size = padded * height, got = 0, stride, x, y;
    const uint8_t *src;
    uint8_t *buffer = NULL;
    enum codec_result r;

    if (!c->present)
        return CODEC_INVALID;
    if (compression == COMP_NONE) {
        src = c->data;
        got = c->length;
    } else {
        buffer = malloc(size ? size : 1);
        if (buffer == NULL)
            return CODEC_NO_MEMORY;
        if (compression == COMP_RLE) {
            got = unrle(c->data, c->length, buffer, size);
        } else {
            r = zlib_inflate(c->data, c->length, buffer, size, &got);
            if (r == CODEC_TOO_LARGE)
                r = CODEC_OK;
            if (r != CODEC_OK) {
                free(buffer);
                return r;
            }
        }
        src = buffer;
    }
    if (padded != row && got >= size)
        stride = padded;
    else if (got >= row * height)
        stride = row;
    else {
        free(buffer);
        return CODEC_TRUNCATED;
    }
    for (y = 0; y < height; y++) {
        const uint8_t *s = src + y * stride;
        uint8_t *o = out + y * (size_t)width;
        switch (bits) {
        case 1:
            for (x = 0; x < width; x++)
                o[x] = (uint8_t)((s[x >> 3] >> (7u - (x & 7u))) & 1u);
            break;
        case 4:
            for (x = 0; x < width; x++)
                o[x] = (uint8_t)((s[x >> 1] >> (x & 1u ? 0 : 4)) & 15u);
            break;
        case 8:
            memcpy(o, s, width);
            break;
        default:
            for (x = 0; x < width; x++)
                o[x] = (uint8_t)(((uint32_t)psp_le16(s + 2 * x) * 255u + 32767u) / 65535u);
            break;
        }
    }
    free(buffer);
    return CODEC_OK;
}

/* Decode a colour bitmap and optional transparency mask into RGBA. colour
   is indexed by channel type: 0 for greyscale and paletted bitmaps, 1 to 3
   for red, green and blue. */
static enum codec_result decode_bitmap(const struct attrs *a, unsigned depth,
                                       unsigned grey, const uint8_t palette[256][3],
                                       unsigned colours, unsigned compression,
                                       const struct channel colour[4],
                                       const struct channel *trans,
                                       unsigned width, unsigned height,
                                       uint8_t *rgba)
{
    size_t n = (size_t)width * height, i;
    uint8_t *plane = malloc(n ? n : 1);
    enum codec_result r = CODEC_OK;
    unsigned c;

    (void)a;
    if (plane == NULL)
        return CODEC_NO_MEMORY;
    if (depth >= 24) {
        for (c = 0; c < 3 && r == CODEC_OK; c++) {
            r = decode_plane(&colour[c + 1], compression, width, height,
                             depth / 3u, plane);
            for (i = 0; r == CODEC_OK && i < n; i++)
                rgba[i * 4 + c] = plane[i];
        }
    } else {
        r = decode_plane(&colour[0], compression, width, height, depth, plane);
        for (i = 0; r == CODEC_OK && i < n; i++) {
            uint8_t *o = rgba + i * 4;
            if (grey) {
                o[0] = o[1] = o[2] = plane[i];
            } else if (plane[i] < colours) {
                o[0] = palette[plane[i]][0];
                o[1] = palette[plane[i]][1];
                o[2] = palette[plane[i]][2];
            } else {
                o[0] = o[1] = o[2] = 0;
            }
        }
    }
    if (r == CODEC_OK && trans != NULL && trans->present) {
        r = decode_plane(trans, compression, width, height, 8, plane);
        for (i = 0; r == CODEC_OK && i < n; i++)
            rgba[i * 4 + 3] = plane[i];
    } else {
        for (i = 0; i < n; i++)
            rgba[i * 4 + 3] = 255;
    }
    free(plane);
    return r;
}

/* ---- the stored composite ---- */

struct composite_attrs {
    unsigned width, height, depth, compression, type;
};

/* A full-size composite that isn't JPEG, as RGBA. *found is 0 if the file
   has none. *alpha says whether it has a transparency mask. */
static enum codec_result read_composite(const struct file *f, const struct block *bank,
                                        const struct attrs *a, int *found,
                                        int *alpha, uint8_t **rgba)
{
    struct composite_attrs list[8];
    unsigned attrs = 0, images = 0;
    size_t offset, end = bank->body + bank->length;
    enum codec_result r;
    struct block b;

    *found = 0;
    if (bank->length < 4 || psp_le32(f->data + bank->body) > bank->length ||
        psp_le32(f->data + bank->body) < 4)
        return CODEC_INVALID;
    offset = bank->body + psp_le32(f->data + bank->body);
    while (offset < end) {
        r = read_block(f, offset, end, &b);
        if (r != CODEC_OK)
            return r;
        offset = b.body + b.length;
        if (b.id == B_COMP_ATTR) {
            const uint8_t *p = f->data + b.body;
            if (b.length < 24 || psp_le32(p) < 24 || psp_le32(p) > b.length)
                return CODEC_INVALID;
            if (attrs < 8) {
                list[attrs].width = psp_le32(p + 4);
                list[attrs].height = psp_le32(p + 8);
                list[attrs].depth = psp_le16(p + 12);
                list[attrs].compression = psp_le16(p + 14);
                list[attrs].type = psp_le16(p + 22);
            }
            attrs++;
        } else if (b.id == B_COMPOSITE || b.id == B_JPEG) {
            /* Images follow their attributes in the same order. */
            unsigned k = images++;
            const struct composite_attrs *c;
            struct channel colour[4], trans;
            uint8_t palette[256][3];
            unsigned colours = 0, grey = 0;
            size_t sub, sub_end = b.body + b.length;

            if (k >= attrs || k >= 8)
                return CODEC_INVALID;
            c = &list[k];
            if (b.id == B_JPEG || c->type != 0 || c->compression > COMP_LZ77 ||
                c->width != a->width || c->height != a->height)
                continue;
            if (c->depth != 1 && c->depth != 4 && c->depth != 8 &&
                c->depth != 24 && c->depth != 48)
                return CODEC_INVALID;
            if (b.length < 4 || psp_le32(f->data + b.body) < 4 ||
                psp_le32(f->data + b.body) > b.length)
                return CODEC_INVALID;
            memset(colour, 0, sizeof colour);
            memset(&trans, 0, sizeof trans);
            for (sub = b.body + psp_le32(f->data + b.body); sub < sub_end;) {
                struct block s;
                r = read_block(f, sub, sub_end, &s);
                if (r != CODEC_OK)
                    return r;
                sub = s.body + s.length;
                if (s.id == B_COLOR) {
                    r = read_palette(f, &s, palette, &colours);
                    if (r != CODEC_OK)
                        return r;
                } else if (s.id == B_CHANNEL) {
                    unsigned bitmap, type;
                    struct channel ch;
                    r = read_channel(f, &s, &bitmap, &type, &ch);
                    if (r != CODEC_OK)
                        return r;
                    if (bitmap == DIB_COMPOSITE && type < 4)
                        colour[type] = ch;
                    else if (bitmap == DIB_COMPOSITE_TRANS)
                        trans = ch;
                }
            }
            if (c->depth <= 8 && colours == 0) {
                if (c->depth != 8 || !a->grey)
                    return CODEC_INVALID;
                grey = 1;
            }
            *rgba = malloc((size_t)a->width * a->height * 4u);
            if (*rgba == NULL)
                return CODEC_NO_MEMORY;
            r = decode_bitmap(a, c->depth, grey, (const uint8_t (*)[3])palette,
                              colours, c->compression, colour, &trans,
                              a->width, a->height, *rgba);
            if (r != CODEC_OK) {
                free(*rgba);
                *rgba = NULL;
                return r;
            }
            *found = 1;
            *alpha = trans.present;
            return CODEC_OK;
        }
    }
    return CODEC_OK;
}

/* ---- layers ---- */

static int64_t le32s(const uint8_t *p)
{
    uint32_t v = psp_le32(p);
    return v & 0x80000000u ? -(int64_t)(0xffffffffu - v) - 1 : (int64_t)v;
}

/* Rectangles are left, top, right, bottom. The saved rectangle lies within
   the image (or mask) rectangle and is relative to its corner. */
static enum codec_result read_rects(const uint8_t *outer, const uint8_t *saved,
                                    int64_t *x, int64_t *y, unsigned *width,
                                    unsigned *height)
{
    int64_t left = le32s(saved), top = le32s(saved + 4);
    int64_t right = le32s(saved + 8), bottom = le32s(saved + 12);

    if (right < left || bottom < top ||
        right - left > (int64_t)PSP_MAX_SIDE * 2 || bottom - top > (int64_t)PSP_MAX_SIDE * 2)
        return CODEC_INVALID;
    *width = (unsigned)(right - left);
    *height = (unsigned)(bottom - top);
    if ((uint64_t)*width * *height > PSP_MAX_LAYER_PIXELS)
        return CODEC_TOO_LARGE;
    *x = le32s(outer) + left;
    *y = le32s(outer + 4) + top;
    return CODEC_OK;
}

static enum codec_result read_layer(const struct file *f, const struct block *b,
                                    struct layer *l)
{
    const uint8_t *p = f->data + b->body, *q;
    size_t offset, end = b->body + b->length, info;
    enum codec_result r;

    memset(l, 0, sizeof *l);
    if (f->major >= 4) {
        unsigned name;
        if (b->length < 6)
            return CODEC_INVALID;
        info = psp_le32(p);
        name = psp_le16(p + 4);
        if (info > b->length || info < 6u + name + 72u)
            return CODEC_INVALID;
        q = p + 6 + name;
        l->type = q[0];
        l->visible = q[35] & 1u;
        l->mask_invert = info >= 6u + name + 73u ? q[72] != 0 : 0;
    } else {
        info = b->init;
        if (info < 331)
            return CODEC_INVALID;
        q = p + 256;
        /* Normal and floating selection layers are both raster. */
        l->type = L_RASTER;
        l->visible = q[35] != 0;
        l->mask_invert = q[72] != 0;
    }
    l->opacity = q[33];
    l->blend = q[34];
    l->mask_disabled = q[71] != 0;
    r = read_rects(q + 1, q + 17, &l->x, &l->y, &l->width, &l->height);
    if (r == CODEC_OK)
        r = read_rects(q + 38, q + 54, &l->mask_x, &l->mask_y, &l->mask_width,
                       &l->mask_height);
    if (r != CODEC_OK)
        return r;
    /* The information chunk is followed by sub-blocks (extensions and
       channels) and, in format 4.0 and later, the bitmap count chunk. */
    for (offset = b->body + info; offset < end;) {
        struct block s;
        if (end - offset >= 4 && memcmp(f->data + offset, "~BK", 4) == 0) {
            r = read_block(f, offset, end, &s);
            if (r != CODEC_OK)
                return r;
            offset = s.body + s.length;
            if (s.id == B_CHANNEL) {
                unsigned bitmap, type;
                struct channel c;
                r = read_channel(f, &s, &bitmap, &type, &c);
                if (r != CODEC_OK)
                    return r;
                if (bitmap == DIB_IMAGE && type < 4)
                    l->colour[type] = c;
                else if (bitmap == DIB_TRANS)
                    l->trans = c;
                else if (bitmap == DIB_USER_MASK)
                    l->mask = c;
            } else if (s.id == B_GROUP_EXT) {
                if (s.length < 8 || psp_le32(f->data + s.body) < 8 ||
                    psp_le32(f->data + s.body) > s.length)
                    return CODEC_INVALID;
                l->children = psp_le32(f->data + s.body + 4);
            }
        } else if (f->major >= 4 && end - offset >= 4 &&
                   psp_le32(f->data + offset) >= 4 &&
                   psp_le32(f->data + offset) <= end - offset) {
            offset += psp_le32(f->data + offset);
        } else {
            return CODEC_INVALID;
        }
    }
    if (l->type == L_UNDEFINED && (l->colour[0].present || l->colour[1].present))
        l->type = L_RASTER;
    return CODEC_OK;
}

/* ---- blending ----

   Paint Shop Pro's blend arithmetic, as its own composites show it (fitted
   to Paint Shop Pro 8 over every mode, opacity and pair of alphas). s is
   the layer's colour, a its alpha (transparency and masks), o its opacity;
   d the colour below, da its alpha, all 0..1 or 0..255 as noted. With
   ao = a * o the result's alpha is always ao + da * (1 - ao). */

static unsigned mul255(unsigned a, unsigned b)
{
    return (a * b + 127u) / 255u;
}

static float round_f(float v)
{
    return (float)(long)(v + 0.5f);
}

static float clamp255(float v)
{
    return v < 0 ? 0 : v > 255 ? 255 : v;
}

static float sqrt_f(float v)
{
    float r = v > 1 ? v : 1;
    int i;
    if (v <= 0)
        return 0;
    for (i = 0; i < 24; i++)
        r = 0.5f * (r + v / r);
    return r;
}

/* The separable modes on 0..255 values. */
static float separable(unsigned mode, float s, float d)
{
    float sf, df;

    switch (mode) {
    case 1: return s < d ? s : d;                              /* darken */
    case 2: return s > d ? s : d;                              /* lighten */
    case 7: return round_f(s * d / 255);                       /* multiply */
    case 8: return 255 - round_f((255 - s) * (255 - d) / 255); /* screen */
    case 10:                                                   /* overlay */
        return d < 128 ? round_f(2 * s * d / 255) : 255 - round_f(2 * (255 - s) * (255 - d) / 255);
    case 11:                                                   /* hard light */
        return s < 128 ? round_f(2 * s * d / 255) : 255 - round_f(2 * (255 - s) * (255 - d) / 255);
    case 12:                                                   /* soft light */
        sf = s / 255;
        df = d / 255;
        if (sf <= 0.5f)
            return round_f((2 * sf * df + df * df * (1 - 2 * sf)) * 255);
        return round_f((2 * df * (1 - sf) + sqrt_f(df) * (2 * sf - 1)) * 255);
    case 13: return s > d ? s - d : d - s;                     /* difference */
    case 16: return s + d - round_f(2 * s * d / 255);          /* exclusion */
    default: return s;
    }
}

/* Luminosity-preserving blends with 0.3/0.59/0.11 weights: Paint Shop Pro's
   legacy hue, saturation, colour and luminance modes. */
static float lum_of(const float c[3])
{
    return c[0] * 0.3f + c[1] * 0.59f + c[2] * 0.11f;
}

static void set_lum(float c[3], float l)
{
    float d = l - lum_of(c), n, x;
    int i;

    for (i = 0; i < 3; i++)
        c[i] += d;
    l = lum_of(c);
    n = x = c[0];
    for (i = 1; i < 3; i++) {
        if (c[i] < n) n = c[i];
        if (c[i] > x) x = c[i];
    }
    for (i = 0; i < 3; i++) {
        if (n < 0 && l - n > 0)
            c[i] = l + (c[i] - l) * l / (l - n);
        if (x > 1 && x - l > 0)
            c[i] = l + (c[i] - l) * (1 - l) / (x - l);
    }
}

static void set_sat(float c[3], float s)
{
    int max = 0, min = 0, mid, i;
    for (i = 1; i < 3; i++) {
        if (c[i] > c[max]) max = i;
        if (c[i] < c[min]) min = i;
    }
    if (c[max] <= c[min]) {
        c[0] = c[1] = c[2] = 0;
        return;
    }
    mid = 3 - max - min;
    c[mid] = (c[mid] - c[min]) * s / (c[max] - c[min]);
    c[max] = s;
    c[min] = 0;
}

static float sat_of(const float c[3])
{
    float n = c[0], x = c[0];
    int i;
    for (i = 1; i < 3; i++) {
        if (c[i] < n) n = c[i];
        if (c[i] > x) x = c[i];
    }
    return x - n;
}

static void luminosity_blend(unsigned kind, const float *s8, const float *d8, float out[3])
{
    float s[3], d[3], t[3];
    int i;

    for (i = 0; i < 3; i++) {
        s[i] = s8[i] / 255;
        d[i] = d8[i] / 255;
    }
    switch (kind) {
    case 3:
        memcpy(t, s, sizeof t);
        set_sat(t, sat_of(d));
        set_lum(t, lum_of(d));
        break;
    case 4:
        memcpy(t, d, sizeof t);
        set_sat(t, sat_of(s));
        set_lum(t, lum_of(d));
        break;
    case 5:
        memcpy(t, s, sizeof t);
        set_lum(t, lum_of(d));
        break;
    default:
        memcpy(t, d, sizeof t);
        set_lum(t, lum_of(s));
        break;
    }
    for (i = 0; i < 3; i++)
        out[i] = round_f(clamp255(t[i] * 255));
}

/* HSL, hue 0..1: Paint Shop Pro 8's hue, saturation, colour and lightness
   modes swap these components. */
static void to_hsl(const float *c8, float *h, float *s, float *l)
{
    float r = c8[0] / 255, g = c8[1] / 255, b = c8[2] / 255;
    float mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    float mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    float d = mx - mn;

    *l = (mx + mn) / 2;
    if (d <= 0) {
        *h = *s = 0;
        return;
    }
    *s = d / (*l < 0.5f ? mx + mn : 2 - mx - mn);
    if (mx == r)
        *h = (g - b) / d + (g < b ? 6 : 0);
    else if (mx == g)
        *h = (b - r) / d + 2;
    else
        *h = (r - g) / d + 4;
    *h /= 6;
}

static float hue_part(float p, float q, float t)
{
    t -= (float)(long)t;
    if (t < 0)
        t += 1;
    if (t < 1.0f / 6)
        return p + (q - p) * 6 * t;
    if (t < 0.5f)
        return q;
    if (t < 2.0f / 3)
        return p + (q - p) * (2.0f / 3 - t) * 6;
    return p;
}

static void from_hsl(float h, float s, float l, float out[3])
{
    float q = l < 0.5f ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
    out[0] = round_f(hue_part(p, q, h + 1.0f / 3) * 255);
    out[1] = round_f(hue_part(p, q, h) * 255);
    out[2] = round_f(hue_part(p, q, h - 1.0f / 3) * 255);
}

static void hsl_blend(unsigned mode, const float *s, const float *d, float out[3])
{
    float sh, ss, sl, dh, ds, dl;
    to_hsl(s, &sh, &ss, &sl);
    to_hsl(d, &dh, &ds, &dl);
    switch (mode) {
    case 17: from_hsl(ss > 0 ? sh : dh, ds, dl, out); break;
    case 18: from_hsl(dh, ss, dl, out); break;
    case 19: from_hsl(sh, ss, dl, out); break;
    default: from_hsl(dh, ds, sl, out); break;
    }
}

static void blend_colour(unsigned mode, const float *s, const float *d, float out[3])
{
    unsigned i;
    if (mode >= 3 && mode <= 6)
        luminosity_blend(mode, s, d, out);
    else if (mode >= 17)
        hsl_blend(mode, s, d, out);
    else
        for (i = 0; i < 3; i++)
            out[i] = separable(mode, s[i], d[i]);
}

static int known_blend(unsigned mode)
{
    return mode <= 20;
}

/* A stable per-pixel threshold for dissolve. Paint Shop Pro uses its own
   fixed noise, which isn't reproduced. */
static unsigned dissolve_noise(unsigned x, unsigned y)
{
    uint32_t h = x * 0x9e3779b1u ^ (y + 0x7f4a7c15u) * 0x85ebca77u;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    return h % 255u;
}

/* Normal mode, in integers: the plain "over" operator. */
static void put_normal(const uint8_t *s, unsigned a, uint8_t *d)
{
    unsigned da = d[3], i;
    uint32_t oa;

    if (a == 0)
        return;
    oa = a * 255u + da * (255u - a);
    for (i = 0; i < 3; i++) {
        uint32_t num = a * 255u * s[i] + (255u - a) * da * d[i];
        d[i] = (uint8_t)((num + oa / 2u) / oa);
    }
    d[3] = (uint8_t)((oa + 127u) / 255u);
}

/* Composite one pixel of colour s, alpha a8 and opacity o8 (0..255) over d. */
static void put_pixel(unsigned mode, const uint8_t *s8, unsigned a8, unsigned o8,
                      uint8_t *d8, unsigned x, unsigned y)
{
    float a = a8 / 255.0f, o = o8 / 255.0f, da = d8[3] / 255.0f;
    float ao = a * o, oa = ao + da * (1 - ao), s[3], d[3], b[3], col[3], w;
    unsigned i;

    if (mode == 0 || mode == 9) {
        unsigned k = mul255(a8, o8);
        if (mode == 9)
            k = dissolve_noise(x, y) < k ? 255u : 0u;
        put_normal(s8, k, d8);
        return;
    }
    if (a8 == 0 || o8 == 0)
        return;
    for (i = 0; i < 3; i++) {
        s[i] = s8[i];
        d[i] = d8[i];
    }
    switch (mode) {
    case 14:
    case 15:
        /* Dodge, and burn as dodge in inverted colours, against the
           premultiplied colour below; opacity and alpha move the layer's
           colour toward the neutral one. */
        for (i = 0; i < 3; i++) {
            float sv = mode == 15 ? 255 - s[i] : s[i], dv = mode == 15 ? 255 - d[i] : d[i];
            float sn = sv * ao, dn = dv * da, v;
            if (sn >= 255)
                v = dn > 0 ? 255 : 0;
            else {
                v = round_f(dn * 255 / (255 - sn));
                if (v > 255) v = 255;
            }
            v = v / oa;
            if (v > 255) v = 255;
            col[i] = mode == 15 ? 255 - v : v;
        }
        break;
    case 13:
        /* Difference: opacity moves the layer's colour toward black. */
        for (i = 0; i < 3; i++) {
            float sn = round_f(s[i] * o), bv = sn > d[i] ? sn - d[i] : d[i] - sn;
            w = 1 - (1 - a) * (1 - ao);
            col[i] = (ao * (1 - da) * s[i] + w * da * bv + (1 - w) * da * d[i]) / oa;
        }
        break;
    case 12:
        /* Soft light blends against the colour below faded toward grey. */
        for (i = 0; i < 3; i++) {
            float dg = round_f(128 + (d[i] - 128) * da);
            col[i] = (ao * separable(12, s[i], dg) + (1 - ao) * da * d[i]) / oa;
        }
        break;
    case 6: {
        /* Legacy luminance: the luminance of the normal composite, given to
           the colours mixed at the lower pixel's full alpha. */
        float n[3], yv[3], k = ao * (1 - da) + da;
        for (i = 0; i < 3; i++) {
            n[i] = round_f((ao * s[i] + (1 - ao) * da * d[i]) / oa);
            yv[i] = round_f((ao * (1 - da) * s[i] + da * d[i]) / k);
        }
        luminosity_blend(6, n, yv, col);
        break;
    }
    default:
        /* The other modes count the layer's transparency twice. */
        blend_colour(mode, s, d, b);
        w = ao * (1 + o * (1 - a));
        for (i = 0; i < 3; i++)
            col[i] = (ao * (1 - da) * s[i] + w * da * b[i] + (1 - w) * da * d[i]) / oa;
        break;
    }
    for (i = 0; i < 3; i++)
        d8[i] = (uint8_t)round_f(clamp255(col[i]));
    d8[3] = (uint8_t)round_f(clamp255(oa * 255));
}

/* ---- compositing ---- */

struct context {
    const struct file *f;
    const struct attrs *a;
    struct layer *layers;
    unsigned long count;
};

/* Scale mask values by the layer's opacity: 255 leaves pixels alone. */
static enum codec_result mask_plane(const struct context *c, const struct layer *l,
                                    uint8_t **mask)
{
    size_t n = (size_t)l->mask_width * l->mask_height, i;
    enum codec_result r;

    *mask = malloc(n ? n : 1);
    if (*mask == NULL)
        return CODEC_NO_MEMORY;
    r = decode_plane(&l->mask, c->a->compression, l->mask_width, l->mask_height,
                     8, *mask);
    if (r != CODEC_OK) {
        free(*mask);
        *mask = NULL;
        return r;
    }
    if (l->mask_invert)
        for (i = 0; i < n; i++)
            (*mask)[i] = (uint8_t)(255u - (*mask)[i]);
    return CODEC_OK;
}

/* The mask value at canvas position x, y: 0 outside the saved rectangle. */
static unsigned mask_at(const struct layer *l, const uint8_t *mask, int64_t x, int64_t y)
{
    if (x < l->mask_x || y < l->mask_y || x - l->mask_x >= (int64_t)l->mask_width ||
        y - l->mask_y >= (int64_t)l->mask_height)
        return 0;
    return mask[(size_t)(y - l->mask_y) * l->mask_width + (size_t)(x - l->mask_x)];
}

static enum codec_result draw_raster(const struct context *c, const struct layer *l,
                                     struct canvas *canvas)
{
    size_t n = (size_t)l->width * l->height;
    uint8_t *rgba, *mask = NULL;
    int64_t x0, y0, x1, y1, x, y;
    unsigned opacity;
    enum codec_result r;

    if (!known_blend(l->blend))
        return CODEC_INVALID;
    x0 = l->x < 0 ? 0 : l->x;
    y0 = l->y < 0 ? 0 : l->y;
    x1 = l->x + l->width;
    y1 = l->y + l->height;
    if (x1 > (long)canvas->width) x1 = canvas->width;
    if (y1 > (long)canvas->height) y1 = canvas->height;
    /* Paint Shop Pro gives a layer with no transparency channel, as it
       writes the background, no opacity in normal mode. */
    opacity = l->blend == 0 && !l->trans.present ? 255u : l->opacity;
    if (n == 0 || opacity == 0)
        return CODEC_OK;
    rgba = malloc(n * 4u);
    if (rgba == NULL)
        return CODEC_NO_MEMORY;
    r = decode_bitmap(c->a, c->a->depth, c->a->grey,
                      (const uint8_t (*)[3])c->a->palette, c->a->colours,
                      c->a->compression, l->colour, &l->trans, l->width,
                      l->height, rgba);
    if (r == CODEC_OK && l->mask.present && !l->mask_disabled)
        r = mask_plane(c, l, &mask);
    for (y = y0; r == CODEC_OK && y < y1; y++) {
        for (x = x0; x < x1; x++) {
            const uint8_t *s = rgba + ((size_t)(y - l->y) * l->width + (size_t)(x - l->x)) * 4u;
            unsigned a = s[3];
            if (mask != NULL)
                a = mul255(a, mask_at(l, mask, x, y));
            put_pixel(l->blend, s, a, opacity,
                      canvas->rgba + ((size_t)y * canvas->width + (size_t)x) * 4u,
                      (unsigned)x, (unsigned)y);
        }
    }
    free(mask);
    free(rgba);
    return r;
}

/* A mask layer hides what lies below it at its own level. */
static enum codec_result draw_mask(const struct context *c, const struct layer *l,
                                   struct canvas *canvas)
{
    uint8_t *mask = NULL;
    unsigned x, y;
    enum codec_result r;

    if (!l->mask.present || l->mask_disabled)
        return CODEC_OK;
    r = mask_plane(c, l, &mask);
    if (r != CODEC_OK)
        return r;
    for (y = 0; y < canvas->height; y++) {
        for (x = 0; x < canvas->width; x++) {
            uint8_t *d = canvas->rgba + ((size_t)y * canvas->width + x) * 4u;
            unsigned m = 255u - mul255(255u - mask_at(l, mask, x, y), l->opacity);
            d[3] = (uint8_t)mul255(d[3], m);
        }
    }
    free(mask);
    return CODEC_OK;
}

/* Apply a composited group to the canvas below it. */
static void draw_group(const struct layer *l, const struct canvas *group,
                       struct canvas *canvas)
{
    size_t i, n = (size_t)canvas->width * canvas->height;
    for (i = 0; i < n; i++) {
        const uint8_t *s = group->rgba + i * 4u;
        put_pixel(l->blend, s, s[3], l->opacity, canvas->rgba + i * 4u,
                  (unsigned)(i % canvas->width), (unsigned)(i / canvas->width));
    }
}

/* Walk count layers from *index, drawing them onto canvas unless it is
   NULL (a hidden group, whose layers are only skipped). *needs_stored is
   set if a visible layer can't be drawn here. */
static enum codec_result walk(const struct context *c, unsigned long *index,
                              unsigned long count, unsigned depth,
                              struct canvas *canvas, int draw, int *needs_stored)
{
    unsigned long k;
    enum codec_result r;

    if (depth > PSP_MAX_DEPTH)
        return CODEC_INVALID;
    for (k = 0; k < count && *index < c->count; k++) {
        const struct layer *l = &c->layers[(*index)++];
        int shown = draw && l->visible;

        switch (l->type) {
        case L_GROUP:
            if (!known_blend(l->blend) && shown)
                return CODEC_INVALID;
            if (shown && canvas != NULL) {
                struct canvas group = { canvas->width, canvas->height, NULL };
                group.rgba = calloc((size_t)canvas->width * canvas->height, 4u);
                if (group.rgba == NULL)
                    return CODEC_NO_MEMORY;
                r = walk(c, index, l->children, depth + 1, &group, 1, needs_stored);
                if (r == CODEC_OK)
                    draw_group(l, &group, canvas);
                free(group.rgba);
            } else {
                r = walk(c, index, l->children, depth + 1, NULL, shown, needs_stored);
            }
            if (r != CODEC_OK)
                return r;
            break;
        case L_RASTER:
        case L_FLOATING:
            if (shown && canvas != NULL) {
                r = draw_raster(c, l, canvas);
                if (r != CODEC_OK)
                    return r;
            }
            break;
        case L_MASK:
            if (shown && canvas != NULL) {
                r = draw_mask(c, l, canvas);
                if (r != CODEC_OK)
                    return r;
            }
            break;
        case L_UNDEFINED:
            break;
        default:
            /* Vector, adjustment and art media layers, and unknown types. */
            if (shown)
                *needs_stored = 1;
            break;
        }
    }
    return CODEC_OK;
}

static int opaque(const uint8_t *rgba, size_t pixels)
{
    size_t i;
    for (i = 0; i < pixels; i++)
        if (rgba[i * 4 + 3] != 255)
            return 0;
    return 1;
}

enum codec_result psp_decode(const uint8_t *data, size_t length,
                             struct psp_image *image)
{
    struct file f;
    struct attrs a;
    struct block b;
    struct context c;
    struct canvas canvas;
    size_t offset, bank_offset = 0, layers_offset = 0;
    unsigned long index;
    int have_attrs = 0, have_bank = 0, have_layers = 0, needs_stored = 0;
    int found = 0, alpha = 0;
    uint8_t *stored = NULL;
    enum codec_result r;

    memset(image, 0, sizeof *image);
    memset(&a, 0, sizeof a);
    if (length < 36)
        return length >= 27 && memcmp(data, signature, 27) != 0 ?
               CODEC_INVALID : CODEC_TRUNCATED;
    if (memcmp(data, signature, 32) != 0)
        return CODEC_INVALID;
    f.data = data;
    f.length = length;
    f.major = psp_le16(data + 32);
    if (f.major < 3)
        return CODEC_INVALID;
    /* The main blocks, the general image attributes first. */
    for (offset = 36; offset < length; offset = b.body + b.length) {
        r = read_block(&f, offset, length, &b);
        if (r != CODEC_OK)
            return r;
        if (!have_attrs) {
            if (b.id != B_IMAGE)
                return CODEC_INVALID;
            r = read_attrs(&f, &b, &a);
            if (r != CODEC_OK)
                return r;
            have_attrs = 1;
        } else if (b.id == B_COLOR) {
            r = read_palette(&f, &b, a.palette, &a.colours);
            if (r != CODEC_OK)
                return r;
        } else if (b.id == B_COMP_BANK && f.major >= 4 && !have_bank) {
            bank_offset = offset;
            have_bank = 1;
        } else if (b.id == B_LAYER_BANK && !have_layers) {
            layers_offset = offset;
            have_layers = 1;
        }
    }
    /* Every file has a layer bank, after the other blocks it needs. */
    if (!have_attrs || !have_layers)
        return CODEC_TRUNCATED;
    if (a.depth <= 8 && !a.grey && a.colours == 0)
        return CODEC_INVALID;

    /* The layers, bottom first. */
    memset(&c, 0, sizeof c);
    c.f = &f;
    c.a = &a;
    r = read_block(&f, layers_offset, length, &b);
    if (r != CODEC_OK)
        return r;
    {
        size_t end = b.body + b.length;
        struct block l;
        unsigned long n = 0;
        for (offset = b.body; offset < end; offset = l.body + l.length) {
            r = read_block(&f, offset, end, &l);
            if (r != CODEC_OK)
                return r;
            n++;
        }
        c.layers = calloc(n ? n : 1, sizeof *c.layers);
        if (c.layers == NULL)
            return CODEC_NO_MEMORY;
        for (offset = b.body; offset < end; offset = l.body + l.length) {
            read_block(&f, offset, end, &l);
            if (l.id != B_LAYER) {
                free(c.layers);
                return CODEC_INVALID;
            }
            r = read_layer(&f, &l, &c.layers[c.count]);
            if (r != CODEC_OK) {
                free(c.layers);
                return r;
            }
            c.count++;
        }
    }

    /* Find out whether every visible layer can be drawn. */
    index = 0;
    while (index < c.count) {
        r = walk(&c, &index, c.count - index, 0, NULL, 1, &needs_stored);
        if (r != CODEC_OK) {
            free(c.layers);
            return r;
        }
    }
    if (have_bank) {
        read_block(&f, bank_offset, length, &b);
        r = read_composite(&f, &b, &a, &found, &alpha, &stored);
        /* The composite is optional; a damaged one only matters if the
           picture can't be made without it. */
        if (r != CODEC_OK) {
            if (needs_stored) {
                free(c.layers);
                return r;
            }
            found = 0;
        }
    }
    if (needs_stored) {
        free(c.layers);
        if (!found)
            return CODEC_INVALID;
        image->width = a.width;
        image->height = a.height;
        image->rgba = stored;
        return CODEC_OK;
    }

    canvas.width = a.width;
    canvas.height = a.height;
    canvas.rgba = calloc((size_t)a.width * a.height, 4u);
    if (canvas.rgba == NULL) {
        free(stored);
        free(c.layers);
        return CODEC_NO_MEMORY;
    }
    index = 0;
    r = CODEC_OK;
    while (r == CODEC_OK && index < c.count)
        r = walk(&c, &index, c.count - index, 0, &canvas, 1, &needs_stored);
    free(c.layers);
    if (r != CODEC_OK) {
        free(stored);
        free(canvas.rgba);
        return r;
    }
    /* Paint Shop Pro's own composite has its exact blend arithmetic, but
       it drops transparency unless it stores a mask for it. */
    if (found && (alpha || opaque(canvas.rgba, (size_t)a.width * a.height))) {
        free(canvas.rgba);
        canvas.rgba = stored;
    } else {
        free(stored);
    }
    image->width = a.width;
    image->height = a.height;
    image->rgba = canvas.rgba;
    return CODEC_OK;
}

void psp_free(struct psp_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
}
