#include "decode.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define SECTOR 2048u
/* The image pack information, with the attribute byte at 0xe02: rotation in
   bits 0-1, the highest resolution in bits 2-3. */
#define IPI 0x800u
#define ATTRIBUTES 0xe02u
/* Base/16, Base/4 and Base are stored plainly from these sectors. */
static const size_t plain_sector[3] = { 4, 23, 96 };
/* 4Base's Huffman table; its residuals follow. */
#define SECTOR_4BASE 388u
/* 16Base starts this many sectors after the last one 4Base's decoder read,
   which is where ImageMagick looks for it. */
#define GAP_16BASE 12u
/* Overview thumbnails are Base/16 pictures stored back to back. */
#define SECTOR_OVERVIEW 5u
#define THUMBNAIL (192u * 128u * 3u / 2u)

/* 23 ones and a zero start every row of residuals. */
#define SYNC 0xfffffeu
#define SYNC_BITS 24u
/* After the sync: plane in 2 bits, row in 13, one spare bit. */
#define HEADER_BITS 16u

/* Luma and two chroma planes; chroma is subsampled 2x2. */
struct planes { unsigned w, h; uint8_t *y, *c1, *c2; };

struct bits { const uint8_t *d; size_t len, end, pos; };

/* A lookup of every 16-bit prefix: code length << 8 | delta, 0 if none. */
typedef uint16_t table[65536];

static void free_planes(struct planes *p)
{
    free(p->y);
    free(p->c1);
    free(p->c2);
    p->y = p->c1 = p->c2 = NULL;
}

static enum codec_result alloc_planes(struct planes *p, unsigned w, unsigned h)
{
    size_t n = (size_t)w * h;

    p->w = w;
    p->h = h;
    p->y = malloc(n);
    p->c1 = malloc(n / 4u);
    p->c2 = malloc(n / 4u);
    if (p->y == NULL || p->c1 == NULL || p->c2 == NULL) {
        free_planes(p);
        return CODEC_NO_MEMORY;
    }
    return CODEC_OK;
}

/* Rows come in pairs: two luma rows, then a row of each chroma. */
static enum codec_result read_plain(const uint8_t *data, size_t length, size_t offset,
                                    unsigned w, unsigned h, struct planes *p)
{
    enum codec_result r;
    const uint8_t *s;
    unsigned y;

    if (offset > length || length - offset < (size_t)w * h / 2u * 3u)
        return CODEC_TRUNCATED;
    r = alloc_planes(p, w, h);
    if (r != CODEC_OK)
        return r;
    s = data + offset;
    for (y = 0; y < h; y += 2) {
        memcpy(p->y + (size_t)y * w, s, 2u * w);
        memcpy(p->c1 + (size_t)y / 2u * (w / 2u), s + 2u * w, w / 2u);
        memcpy(p->c2 + (size_t)y / 2u * (w / 2u), s + 2u * w + w / 2u, w / 2u);
        s += 3u * w;
    }
    return CODEC_OK;
}

/* Output row 2y or 2y+1 of a plane doubled in size. Even rows interpolate
   along row a; odd rows sit between a and the next row b. The last column
   and row repeat. This is Photo CD's predictor for residuals, so it has to
   match the encoder exactly. */
static void upsample_row(const uint8_t *a, const uint8_t *b, unsigned w, int odd,
                         uint8_t *out)
{
    unsigned x;

    for (x = 0; x + 1 < w; x++) {
        if (odd) {
            out[2 * x] = (uint8_t)((a[x] + b[x] + 1) >> 1);
            out[2 * x + 1] = (uint8_t)((a[x] + a[x + 1] + b[x] + b[x + 1] + 2) >> 2);
        } else {
            out[2 * x] = a[x];
            out[2 * x + 1] = (uint8_t)((a[x] + a[x + 1] + 1) >> 1);
        }
    }
    out[2 * x] = out[2 * x + 1] = odd ? (uint8_t)((a[x] + b[x] + 1) >> 1) : a[x];
}

static uint8_t *upsample(const uint8_t *src, unsigned w, unsigned h)
{
    uint8_t *dst = malloc((size_t)w * h * 4u);
    unsigned y;

    if (dst == NULL)
        return NULL;
    for (y = 0; y < 2u * h; y++) {
        const uint8_t *a = src + (size_t)(y / 2u) * w;
        const uint8_t *b = y / 2u + 1u < h ? a + w : a;
        upsample_row(a, b, w, (int)(y & 1u), dst + (size_t)y * 2u * w);
    }
    return dst;
}

/* Double the planes, the starting point for the next resolution. */
static enum codec_result grow(struct planes *p)
{
    uint8_t *y = upsample(p->y, p->w, p->h);
    uint8_t *c1 = upsample(p->c1, p->w / 2u, p->h / 2u);
    uint8_t *c2 = upsample(p->c2, p->w / 2u, p->h / 2u);

    free_planes(p);
    p->y = y;
    p->c1 = c1;
    p->c2 = c2;
    p->w *= 2u;
    p->h *= 2u;
    if (y == NULL || c1 == NULL || c2 == NULL) {
        free_planes(p);
        return CODEC_NO_MEMORY;
    }
    return CODEC_OK;
}

/* The next n bits (at most 24), or 0 if the file ends first. */
static int peek(const struct bits *b, unsigned n, uint32_t *value)
{
    size_t i = b->pos >> 3;
    uint32_t w = 0;
    unsigned k;

    if (b->pos > b->end || b->end - b->pos < n)
        return 0;
    for (k = 0; k < 4; k++)
        w = w << 8 | (i + k < b->len ? b->d[i + k] : 0u);
    *value = (w << (b->pos & 7u)) >> (32u - n);
    return 1;
}

static int at_sync(const struct bits *b)
{
    uint32_t v;
    return peek(b, SYNC_BITS, &v) && v == SYNC;
}

/* Move to the next sync at or after the current bit. */
static enum codec_result find_sync(struct bits *b)
{
    size_t i;

    for (i = b->pos >> 3; i + 1 < b->len; i++) {
        /* A sync starting in byte i fills byte i+1 with ones. */
        if (b->d[i + 1] != 0xff)
            continue;
        for (b->pos = i * 8u > b->pos ? i * 8u : b->pos; b->pos < i * 8u + 8u; b->pos++)
            if (at_sync(b))
                return CODEC_OK;
    }
    b->pos = b->end;
    return CODEC_TRUNCATED;
}

/* A table is a count less one, then per code: length less one, the code
   left-aligned in 16 bits, and a signed delta. The first matching code wins;
   one with bits set past its length never matches. */
static enum codec_result read_table(struct bits *b, uint16_t *lookup)
{
    size_t at = b->pos >> 3, n, i;

    if (at >= b->len)
        return CODEC_TRUNCATED;
    n = (size_t)b->d[at] + 1u;
    if (b->len - at - 1u < n * 4u)
        return CODEC_TRUNCATED;
    memset(lookup, 0, sizeof(table));
    for (i = n; i-- > 0;) {
        const uint8_t *e = b->d + at + 1u + i * 4u;
        unsigned bits = e[0] + 1u, code = (unsigned)e[1] << 8 | e[2];
        unsigned span, k;

        if (bits > 16)
            return CODEC_INVALID;
        span = 1u << (16u - bits);
        if ((code & (span - 1u)) != 0)
            continue;
        for (k = 0; k < span; k++)
            lookup[code + k] = (uint16_t)(bits << 8 | e[3]);
    }
    b->pos = (at + 1u + n * 4u) * 8u;
    return CODEC_OK;
}

/* Add one resolution's residuals to planes already grown to its size.
   Tables start at sector; tables_n is 1 (luma only) or 3. On success *next
   receives the sector after the last one the stream reached. */
static enum codec_result residuals(const uint8_t *data, size_t length, size_t sector,
                                   unsigned tables_n, struct planes *p, size_t *next)
{
    struct bits b;
    uint16_t *lookup[3] = { NULL, NULL, NULL };
    uint8_t *q = NULL, *end = NULL;
    const uint16_t *t = NULL;
    enum codec_result r = CODEC_OK;
    size_t start = sector * SECTOR, seen;
    unsigned i;

    b.d = data;
    b.len = length;
    b.end = length * 8u;
    b.pos = start * 8u;
    if (start >= length)
        return CODEC_TRUNCATED;
    for (i = 0; i < tables_n && r == CODEC_OK; i++) {
        lookup[i] = malloc(sizeof(table));
        r = lookup[i] == NULL ? CODEC_NO_MEMORY : read_table(&b, lookup[i]);
    }
    if (r == CODEC_OK)
        r = find_sync(&b);
    while (r == CODEC_OK) {
        uint32_t v;

        if (at_sync(&b)) {
            unsigned plane, row;

            b.pos += SYNC_BITS;
            if (!peek(&b, HEADER_BITS, &v)) {
                r = CODEC_TRUNCATED;
                break;
            }
            plane = v >> 14;
            row = v >> 1 & 0x1fffu;
            if (row == p->h) {
                /* ImageMagick's reader keeps 25 to 32 bits ahead of the
                   16 it has taken since the sync began. */
                seen = ((b.pos + 16u) >> 3) + 1u - start;
                *next = sector + (seen + SECTOR - 1u) / SECTOR;
                break;
            }
            b.pos += HEADER_BITS;
            t = NULL;
            if (row < p->h && plane == 0) {
                t = lookup[0];
                q = p->y + (size_t)row * p->w;
                end = p->y + (size_t)p->w * p->h;
            } else if (row < p->h && plane >= 2 && tables_n == 3) {
                uint8_t *c = plane == 2 ? p->c1 : p->c2;
                t = lookup[plane - 1u];
                q = c + (size_t)(row / 2u) * (p->w / 2u);
                end = c + (size_t)(p->w / 2u) * (p->h / 2u);
            }
            continue;
        }
        if (t == NULL || q == end) {
            /* A row we can't use, or one that ran over: skip to the next. */
            b.pos++;
            r = find_sync(&b);
            continue;
        }
        if (!peek(&b, 16, &v)) {
            r = CODEC_TRUNCATED;
            break;
        }
        if (t[v] == 0) {
            t = NULL;
            continue;
        }
        {
            int delta = t[v] & 0xffu, s;
            s = *q + (delta < 128 ? delta : delta - 256);
            *q++ = (uint8_t)(s < 0 ? 0 : s > 255 ? 255 : s);
        }
        b.pos += t[v] >> 8u;
    }
    for (i = 0; i < 3; i++)
        free(lookup[i]);
    return r;
}

/* PhotoYCC to RGB with Kodak's matrix, in 12.20 fixed point, clipped. */
static uint8_t clip(int32_t v)
{
    v += 1 << 19;
    if (v < 0)
        return 0;
    v >>= 20;
    return (uint8_t)(v > 255 ? 255 : v);
}

static void ycc_rgb(unsigned l, unsigned c1, unsigned c2, uint8_t *out)
{
    int32_t y = 1424386 * (int32_t)l;
    int32_t b = (int32_t)c1 - 156, r = (int32_t)c2 - 137;

    out[0] = clip(y + 1909981 * r);
    out[1] = clip(y - 451174 * b - 972180 * r);
    out[2] = clip(y + 2325637 * b);
    out[3] = 255;
}

/* Upsample chroma a row at a time, convert, and rotate by rotation x 90
   degrees anticlockwise. */
static enum codec_result to_rgba(const struct planes *p, unsigned rotation,
                                 struct pcd_image *image)
{
    unsigned w = p->w, h = p->h, cw = w / 2u, ch = h / 2u, x, y;
    uint8_t *u = malloc(w), *v = malloc(w);
    size_t dx, dy, o;

    image->width = rotation & 1u ? h : w;
    image->height = rotation & 1u ? w : h;
    image->rgba = malloc((size_t)w * h * 4u);
    if (u == NULL || v == NULL || image->rgba == NULL) {
        free(u);
        free(v);
        pcd_free(image);
        return CODEC_NO_MEMORY;
    }
    /* Step between neighbouring source pixels, in output pixels. */
    switch (rotation) {
    case 1: dx = (size_t)0 - h; dy = 1; o = (size_t)(w - 1u) * h; break;
    case 2: dx = (size_t)0 - 1u; dy = (size_t)0 - w; o = (size_t)w * h - 1u; break;
    case 3: dx = h; dy = (size_t)0 - 1u; o = h - 1u; break;
    default: dx = 1; dy = w; o = 0; break;
    }
    for (y = 0; y < h; y++) {
        size_t c = (size_t)(y / 2u) * cw, next = y / 2u + 1u < ch ? cw : 0;
        size_t at = o + y * dy;

        upsample_row(p->c1 + c, p->c1 + c + next, cw, (int)(y & 1u), u);
        upsample_row(p->c2 + c, p->c2 + c + next, cw, (int)(y & 1u), v);
        for (x = 0; x < w; x++, at += dx)
            ycc_rgb(p->y[(size_t)y * w + x], u[x], v[x], image->rgba + at * 4u);
    }
    free(u);
    free(v);
    return CODEC_OK;
}

static int is_overview(const uint8_t *data, size_t length)
{
    return length >= 7 && memcmp(data, "PCD_OPA", 7) == 0;
}

static enum codec_result header(const uint8_t *data, size_t length, int *overview,
                                unsigned long *count)
{
    if (data == NULL)
        return CODEC_INVALID;
    if (length > SIZE_MAX / 8u)
        return CODEC_TOO_LARGE;
    *overview = is_overview(data, length);
    if (*overview) {
        unsigned long n;

        if (length < 12)
            return CODEC_TRUNCATED;
        n = (unsigned long)data[10] << 8 | data[11];
        if (n == 0)
            return CODEC_INVALID;
        if (length < SECTOR_OVERVIEW * SECTOR + THUMBNAIL)
            return CODEC_TRUNCATED;
        if (n > (length - SECTOR_OVERVIEW * SECTOR) / THUMBNAIL)
            n = (length - SECTOR_OVERVIEW * SECTOR) / THUMBNAIL;
        *count = n;
        return CODEC_OK;
    }
    if (length < IPI + 7)
        return CODEC_TRUNCATED;
    if (memcmp(data + IPI, "PCD_IPI", 7) != 0)
        return CODEC_INVALID;
    if (length <= ATTRIBUTES)
        return CODEC_TRUNCATED;
    /* 3 means 64Base, kept in separate files; this one ends at 16Base. */
    *count = 3u + ((data[ATTRIBUTES] >> 2 & 3u) > 2u ? 2u : data[ATTRIBUTES] >> 2 & 3u);
    return CODEC_OK;
}

enum codec_result pcd_count(const uint8_t *data, size_t length, unsigned long *count)
{
    int overview;

    if (count == NULL)
        return CODEC_INVALID;
    *count = 0;
    return header(data, length, &overview, count);
}

void pcd_free(struct pcd_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
    image->width = image->height = 0;
}

enum codec_result pcd_decode(const uint8_t *data, size_t length, unsigned long index,
                             struct pcd_image *image)
{
    struct planes p = { 0, 0, NULL, NULL, NULL };
    enum codec_result r;
    unsigned long count;
    unsigned level;
    int overview;
    size_t next = 0;

    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->rgba = NULL;
    r = header(data, length, &overview, &count);
    if (r != CODEC_OK)
        return r;
    if (index == PCD_DEFAULT)
        index = overview ? 0 : count - 1u;
    if (index >= count)
        return CODEC_INVALID;
    if (overview) {
        r = read_plain(data, length, SECTOR_OVERVIEW * SECTOR + index * THUMBNAIL,
                       192, 128, &p);
        if (r == CODEC_OK)
            r = to_rgba(&p, 0, image);
        free_planes(&p);
        return r;
    }
    level = (unsigned)index;
    r = read_plain(data, length, plain_sector[level < 2 ? level : 2] * SECTOR,
                   192u << (level < 2 ? level : 2), 128u << (level < 2 ? level : 2), &p);
    if (r == CODEC_OK && level >= 3) {
        r = grow(&p);
        if (r == CODEC_OK)
            r = residuals(data, length, SECTOR_4BASE, 1, &p, &next);
    }
    if (r == CODEC_OK && level >= 4) {
        r = grow(&p);
        if (r == CODEC_OK)
            r = residuals(data, length, next + GAP_16BASE, 3, &p, &next);
    }
    if (r == CODEC_OK)
        r = to_rgba(&p, data[ATTRIBUTES] & 3u, image);
    free_planes(&p);
    return r;
}
