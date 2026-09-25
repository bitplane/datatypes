#include "decode.h"
#include "dptable.h"
#include "qm.h"
#include <stdlib.h>
#include <string.h>

#define MAX_PIXELS (16UL * 1024UL * 1024UL)
#define MAX_SIDE 65535UL
#define MAX_PLANES 16u

enum { HITOLO = 0x08, SEQ = 0x04, ILEAVE = 0x02, SMID = 0x01 };
enum { LRLTWO = 0x40, TPDON = 0x10, TPBON = 0x08, DPON = 0x04, DPPRIV = 0x02, DPLAST = 0x01 };
enum { ESC = 0xff, STUFF = 0x00, SDNORM = 0x02, SDRST = 0x03, ABORT = 0x04,
       NEWLEN = 0x05, ATMOVE = 0x06, COMMENT = 0x07 };

/* Contexts coding the typical prediction pseudo-pixels (Figures 8, 11 and
   12), in the bit layouts of the templates below. */
#define TP_CONTEXT_THREE 0x0e5u
#define TP_CONTEXT_TWO 0x195u
#define TP_CONTEXT_DIFFERENTIAL 0xff0u

/* Where each spatial phase's entries start in a DP table. */
static const unsigned dp_offset[4] = { 0, 256, 768, 2816 };

struct header {
    unsigned d, planes, order, options;
    uint32_t xd, yd, l0;
    const uint8_t *dp;
};

/* An adaptive template move, taking effect on a line of the next stripe. */
struct move { uint32_t line; int tx, ty; };

/* A stripe data entity: where its protected stripe coded data lies, whether
   it ends in SDRST, and the AT moves before it. */
struct entity { size_t offset, length, first_move; unsigned moves; int reset; };

struct scan {
    struct entity *entities; /* NULL to only count */
    struct move *moves;
    size_t limit, count, move_count, pending;
    int have_newlen;
    uint32_t newlen;
};

struct layer { uint32_t width, height; size_t stride; uint8_t *bits; };

/* The coding state of one plane in one resolution layer. */
struct coder {
    const struct header *header;
    uint8_t contexts[4096];
    int tx, ty;    /* AT offsets, 0 and 0 for the default place */
    int ltp;       /* LNTP of the line above, lowest layer only */
    uint32_t top;  /* first line a stripe may see: 0, or where a reset left it */
};

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static int px(const uint8_t *row, long x, uint32_t width)
{
    if (row == NULL || x < 0 || (unsigned long)x >= width)
        return 0;
    return row[x >> 3] >> (7 - (x & 7)) & 1;
}

static void set_px(uint8_t *row, long x)
{
    row[x >> 3] |= (uint8_t)(0x80u >> (x & 7));
}

static uint8_t *row_of(const struct layer *layer, uint32_t y)
{
    return layer->bits + (size_t)y * layer->stride;
}

static void note_newlen(struct scan *scan, const uint8_t *p)
{
    if (!scan->have_newlen) {
        scan->have_newlen = 1;
        scan->newlen = be32(p);
    }
}

static enum codec_result note_move(struct scan *scan, const uint8_t *p)
{
    int tx = p[4] < 128 ? p[4] : p[4] - 256, ty = p[5];
    /* A pixel to the right on the same line isn't decoded yet. */
    if (ty == 0 && tx < 0)
        return CODEC_INVALID;
    if (scan->moves != NULL) {
        scan->moves[scan->move_count].line = be32(p);
        scan->moves[scan->move_count].tx = tx;
        scan->moves[scan->move_count].ty = ty;
    }
    scan->move_count++;
    scan->pending++;
    return CODEC_OK;
}

/* Walk the bi-level image data: stripe data entities, and the floating
   marker segments between them. An entity cut off by the end of the data
   isn't counted. */
static enum codec_result scan_data(const uint8_t *data, size_t length, size_t pos,
                                   struct scan *scan)
{
    while (pos < length) {
        size_t start = pos, end = 0;
        int ended = 0, marker;

        if (scan->entities != NULL && scan->count == scan->limit)
            return CODEC_OK;
        if (data[pos] == ESC) {
            if (length - pos < 2)
                return CODEC_OK;
            marker = data[pos + 1];
            if (marker == ABORT)
                return CODEC_OK;
            if (marker == NEWLEN) {
                if (length - pos < 6)
                    return CODEC_OK;
                note_newlen(scan, data + pos + 2);
                pos += 6;
                continue;
            }
            if (marker == ATMOVE) {
                if (length - pos < 8)
                    return CODEC_OK;
                if (note_move(scan, data + pos + 2) != CODEC_OK)
                    return CODEC_INVALID;
                pos += 8;
                continue;
            }
            if (marker == COMMENT) {
                if (length - pos < 6 || be32(data + pos + 2) > length - pos - 6)
                    return CODEC_OK;
                pos += 6 + be32(data + pos + 2);
                continue;
            }
            if (marker != STUFF && marker != SDNORM && marker != SDRST)
                return CODEC_INVALID;
        }
        /* The entity runs to ESC SDNORM or ESC SDRST. Some encoders put a
           NEWLEN marker segment just before that; it ends the coded data. */
        for (;;) {
            if (pos >= length || (data[pos] == ESC && length - pos < 2))
                return CODEC_OK;
            if (data[pos] != ESC) {
                if (ended)
                    return CODEC_INVALID;
                pos++;
                continue;
            }
            marker = data[pos + 1];
            if (marker == STUFF && !ended) {
                pos += 2;
            } else if (marker == SDNORM || marker == SDRST) {
                break;
            } else if (marker == NEWLEN) {
                if (length - pos < 6)
                    return CODEC_OK;
                if (!ended)
                    end = pos;
                ended = 1;
                note_newlen(scan, data + pos + 2);
                pos += 6;
            } else {
                return CODEC_INVALID;
            }
        }
        if (!ended)
            end = pos;
        if (scan->entities != NULL) {
            struct entity *entity = &scan->entities[scan->count];
            entity->offset = start;
            entity->length = end - start;
            entity->reset = data[pos + 1] == SDRST;
            entity->first_move = scan->move_count - scan->pending;
            entity->moves = (unsigned)scan->pending;
        }
        scan->pending = 0;
        scan->count++;
        pos += 2;
    }
    return CODEC_OK;
}

/* Where stripe s of layer d (counted from the lowest) and plane k (counted
   from the most significant, as sent) comes among the entities (Table 11). */
static size_t entity_index(const struct header *h, size_t stripes, size_t s, unsigned d, unsigned k)
{
    size_t layers = h->d + 1u, planes = h->planes;
    size_t l = h->order & HITOLO ? h->d - d : d;
    switch (h->order & (SEQ | ILEAVE | SMID)) {
    case 0: return ((size_t)k * layers + l) * stripes + s;
    case ILEAVE: return (l * planes + k) * stripes + s;
    case ILEAVE | SMID: return (l * stripes + s) * planes + k;
    case SEQ: return (s * planes + k) * layers + l;
    case SEQ | SMID: return ((size_t)k * stripes + s) * layers + l;
    default: return (s * layers + l) * planes + k; /* SEQ | ILEAVE */
    }
}

static void apply_moves(struct coder *c, const struct entity *e, const struct move *moves,
                        uint32_t line)
{
    unsigned i;
    for (i = 0; i < e->moves; i++)
        if (moves[e->first_move + i].line == line) {
            c->tx = moves[e->first_move + i].tx;
            c->ty = moves[e->first_move + i].ty;
        }
}

/* The line an AT pixel sits on, and its offset from the target pixel. */
static const uint8_t *at_row(const struct coder *c, const struct layer *layer, uint32_t y,
                             long *dx, long default_dx)
{
    uint32_t ty = c->tx == 0 && c->ty == 0 ? 1u : (uint32_t)c->ty;
    *dx = c->tx == 0 && c->ty == 0 ? default_dx : -(long)c->tx;
    return y >= c->top + ty ? row_of(layer, y - ty) : NULL;
}

/* Lines y0 to y1 - 1 of the lowest resolution layer (Figures 14 and 15). */
static void decode_lowest(struct coder *c, struct layer *cur, const uint8_t *pscd,
                          const struct entity *e, const struct move *moves,
                          uint32_t y0, uint32_t y1)
{
    const struct header *h = c->header;
    int two = (h->options & LRLTWO) != 0;
    uint32_t w = cur->width, y;
    struct qm_decoder q;

    qm_decode_init(&q, pscd, e->length);
    for (y = y0; y < y1; y++) {
        uint8_t *r0 = row_of(cur, y);
        const uint8_t *r1 = y >= c->top + 1 ? row_of(cur, y - 1) : NULL;
        const uint8_t *r2 = y >= c->top + 2 ? row_of(cur, y - 2) : NULL;
        const uint8_t *ra;
        long x, adx;

        apply_moves(c, e, moves, y - y0);
        ra = at_row(c, cur, y, &adx, 2);
        if (h->options & TPBON) {
            int sltp = qm_decode(&q, &c->contexts[two ? TP_CONTEXT_TWO : TP_CONTEXT_THREE]);
            c->ltp = !(sltp ^ c->ltp);
            if (!c->ltp) {
                if (r1 != NULL)
                    memcpy(r0, r1, cur->stride);
                continue;
            }
        }
        for (x = 0; x < (long)w; x++) {
            unsigned cx;
            if (two)
                cx = (unsigned)(px(r1, x - 3, w) << 9 | px(r1, x - 2, w) << 8 |
                                px(r1, x - 1, w) << 7 | px(r1, x, w) << 6 |
                                px(r1, x + 1, w) << 5 | px(ra, x + adx, w) << 4 |
                                px(r0, x - 4, w) << 3 | px(r0, x - 3, w) << 2 |
                                px(r0, x - 2, w) << 1 | px(r0, x - 1, w));
            else
                cx = (unsigned)(px(r2, x - 1, w) << 9 | px(r2, x, w) << 8 |
                                px(r2, x + 1, w) << 7 | px(r1, x - 2, w) << 6 |
                                px(r1, x - 1, w) << 5 | px(r1, x, w) << 4 |
                                px(r1, x + 1, w) << 3 | px(ra, x + adx, w) << 2 |
                                px(r0, x - 2, w) << 1 | px(r0, x - 1, w));
            if (qm_decode(&q, &c->contexts[cx]))
                set_px(r0, x);
        }
    }
}

/* TPVALUE for the pixels over low-resolution pixel X (Figure 9), given
   LNTP is 0: its colour if its 8-neighbourhood matches it, otherwise 2. */
static int tp_value(const uint8_t *above, const uint8_t *line, const uint8_t *below,
                    long X, uint32_t w)
{
    int colour = px(line, X, w);
    long i;
    for (i = X - 1; i <= X + 1; i++)
        if (px(above, i, w) != colour || px(line, i, w) != colour ||
            px(below, i, w) != colour)
            return 2;
    return colour;
}

/* Lines y0 to y1 - 1 of a differential layer (Figure 16), over the layer
   below, whose current stripe ends at line last. */
static void decode_differential(struct coder *c, struct layer *cur, const struct layer *low,
                                const uint8_t *pscd, const struct entity *e,
                                const struct move *moves, uint32_t y0, uint32_t y1,
                                uint32_t last)
{
    const struct header *h = c->header;
    uint32_t w = cur->width, lw = low->width, y;
    uint32_t low_top = c->top / 2u;
    int lntp = 1;
    struct qm_decoder q;

    qm_decode_init(&q, pscd, e->length);
    for (y = y0; y < y1; y++) {
        uint32_t Y = y / 2u;
        uint8_t *r0 = row_of(cur, y);
        const uint8_t *r1 = y >= c->top + 1 ? row_of(cur, y - 1) : NULL;
        const uint8_t *r2 = y >= c->top + 2 ? row_of(cur, y - 2) : NULL;
        const uint8_t *lm = Y >= low_top + 1 ? row_of(low, Y - 1) : NULL;
        const uint8_t *l0 = row_of(low, Y);
        const uint8_t *lp = Y + 1 <= last ? row_of(low, Y + 1) : l0;
        /* DP's high-resolution lines: above the pair, and its two lines. */
        const uint8_t *da = y & 1u ? r2 : r1, *db = y & 1u ? r1 : r0, *dc = y & 1u ? r0 : NULL;
        const uint8_t *ra;
        long x, adx;
        int tpv = 2;

        apply_moves(c, e, moves, y - y0);
        ra = at_row(c, cur, y, &adx, -1);
        if ((h->options & TPDON) && !(y & 1u))
            lntp = qm_decode(&q, &c->contexts[TP_CONTEXT_DIFFERENTIAL]);
        for (x = 0; x < (long)w; x++) {
            long X = x / 2, lx = x & 1 ? X : X - 1, x0 = x & ~1L;
            unsigned phase = (unsigned)((y & 1u) << 1 | (x & 1));
            unsigned cx;

            if ((h->options & TPDON) && !lntp) {
                if (!(x & 1))
                    tpv = tp_value(lm, l0, lp, X, lw);
                if (tpv != 2) {
                    if (tpv)
                        set_px(r0, x);
                    continue;
                }
            }
            if (h->options & DPON) {
                unsigned index = (unsigned)(px(lm, X - 1, lw) | px(lm, X, lw) << 1 |
                                            px(l0, X - 1, lw) << 2 | px(l0, X, lw) << 3 |
                                            px(da, x0 - 1, w) << 4 | px(da, x0, w) << 5 |
                                            px(da, x0 + 1, w) << 6 | px(db, x0 - 1, w) << 7);
                unsigned entry, value;
                if (phase >= 1)
                    index |= (unsigned)px(db, x0, w) << 8;
                if (phase >= 2)
                    index |= (unsigned)(px(db, x0 + 1, w) << 9 | px(dc, x0 - 1, w) << 10);
                if (phase == 3)
                    index |= (unsigned)px(dc, x0, w) << 11;
                entry = dp_offset[phase] + index;
                value = h->dp[entry >> 2] >> (6 - 2 * (entry & 3)) & 3u;
                if (value < 2) {
                    if (value)
                        set_px(r0, x);
                    continue;
                }
            }
            cx = phase << 10 |
                 (unsigned)(px(r2, x, w) << 9 | px(r1, x, w) << 8 | px(r1, x + 1, w) << 7 |
                            px(ra, x + adx, w) << 6 | px(r0, x - 2, w) << 5 |
                            px(r0, x - 1, w) << 4 | px(l0, lx, lw) << 3 |
                            px(l0, lx + 1, lw) << 2 | px(lp, lx, lw) << 1 | px(lp, lx + 1, lw));
            if (qm_decode(&q, &c->contexts[cx]))
                set_px(r0, x);
        }
    }
}

static int alloc_layer(struct layer *layer, uint32_t width, uint32_t height)
{
    layer->width = width;
    layer->height = height;
    layer->stride = (width + 7u) / 8u;
    layer->bits = calloc(layer->stride * height, 1);
    return layer->bits != NULL;
}

/* Lines of stripe s in a layer whose stripes are rows lines high. */
static void stripe_lines(uint64_t rows, size_t s, uint32_t height, uint32_t *y0, uint32_t *y1)
{
    uint64_t start = rows * s, end = start + rows;
    *y0 = start < height ? (uint32_t)start : height;
    *y1 = end < height ? (uint32_t)end : height;
}

/* Decode plane k through every layer, leaving the full resolution in *out. */
static enum codec_result decode_plane(const struct header *h, const uint8_t *data,
                                      const struct entity *entities, const struct move *moves,
                                      size_t stripes, const uint32_t *widths,
                                      const uint32_t *heights, unsigned k, struct layer *out)
{
    struct layer low = { 0, 0, 0, NULL }, cur;
    struct coder *c = malloc(sizeof *c);
    uint64_t rows = h->l0, low_rows = 0;
    unsigned d;

    if (c == NULL)
        return CODEC_NO_MEMORY;
    c->header = h;
    for (d = 0; d <= h->d; d++) {
        size_t s;
        int reset = 1;
        if (!alloc_layer(&cur, widths[d], heights[d])) {
            free(low.bits);
            free(c);
            return CODEC_NO_MEMORY;
        }
        for (s = 0; s < stripes; s++) {
            const struct entity *e = &entities[entity_index(h, stripes, s, d, k)];
            uint32_t y0, y1, low0, low1;
            stripe_lines(rows, s, cur.height, &y0, &y1);
            if (reset) {
                memset(c->contexts, 0, sizeof c->contexts);
                c->tx = c->ty = 0;
                c->ltp = 1;
                c->top = y0;
            }
            reset = e->reset;
            if (d == 0) {
                decode_lowest(c, &cur, data + e->offset, e, moves, y0, y1);
            } else if (y0 < y1) {
                stripe_lines(low_rows, s, low.height, &low0, &low1);
                decode_differential(c, &cur, &low, data + e->offset, e, moves, y0, y1,
                                    low1 - 1);
            }
        }
        free(low.bits);
        low = cur;
        low_rows = rows;
        /* Lines per stripe double from layer to layer; beyond the image it
           no longer matters. */
        if (rows <= MAX_SIDE)
            rows *= 2;
    }
    free(c);
    *out = low;
    return CODEC_OK;
}

static enum codec_result read_header(const uint8_t *data, size_t length, struct header *h,
                                     size_t *data_start)
{
    unsigned sequence;
    if (length < JBIG_HEADER_SIZE)
        return CODEC_TRUNCATED;
    h->d = data[1];
    h->planes = data[2];
    h->xd = be32(data + 4);
    h->yd = be32(data + 8);
    h->l0 = be32(data + 12);
    h->order = data[18];
    h->options = data[19];
    sequence = h->order & (SEQ | ILEAVE | SMID);
    /* A BIE that starts above layer 0 continues one we don't have. */
    if (data[0] != 0 || h->planes == 0 || h->xd == 0 || h->yd == 0 || h->l0 == 0 ||
        sequence == SMID || sequence == (SEQ | ILEAVE | SMID))
        return CODEC_INVALID;
    if (h->planes > MAX_PLANES)
        return CODEC_INVALID;
    *data_start = JBIG_HEADER_SIZE;
    h->dp = jbig_default_dp;
    /* DPLAST without a table before it: keep to the default. */
    if ((h->options & (DPON | DPPRIV | DPLAST)) == (DPON | DPPRIV)) {
        if (length - JBIG_HEADER_SIZE < JBIG_DP_TABLE_SIZE)
            return CODEC_TRUNCATED;
        h->dp = data + JBIG_HEADER_SIZE;
        *data_start += JBIG_DP_TABLE_SIZE;
    }
    return CODEC_OK;
}

/* Merge plane k, most significant first, into the pixels. Several planes
   hold a Gray code, as netpbm and JBIG-KIT's tools write them. */
static void merge_plane(const struct layer *plane, unsigned k, unsigned planes,
                        uint8_t *pixels, uint16_t *values)
{
    uint32_t x, y;
    size_t i = 0;
    for (y = 0; y < plane->height; y++) {
        const uint8_t *row = row_of(plane, y);
        for (x = 0; x < plane->width; x++, i++) {
            unsigned bit = (unsigned)px(row, (long)x, plane->width);
            if (planes == 1)
                pixels[i] = (uint8_t)bit;
            else if (values != NULL)
                values[i] = (uint16_t)(values[i] << 1 | (bit ^ (values[i] & 1u)));
            else if (k == 0)
                pixels[i] = (uint8_t)bit;
            else
                pixels[i] = (uint8_t)(pixels[i] << 1 | (bit ^ (pixels[i] & 1u)));
        }
    }
}

enum codec_result jbig_decode(const uint8_t *data, size_t length, struct jbig_image *image)
{
    struct header h;
    struct scan scan;
    struct entity *entities = NULL;
    struct move *moves = NULL;
    uint32_t widths[256], heights[256];
    size_t start, stripes, needed, pixels, i;
    uint16_t *values = NULL;
    enum codec_result result;
    unsigned d, k;

    memset(image, 0, sizeof *image);
    result = read_header(data, length, &h, &start);
    if (result != CODEC_OK)
        return result;
    memset(&scan, 0, sizeof scan);
    result = scan_data(data, length, start, &scan);
    if (result != CODEC_OK)
        return result;
    if (scan.have_newlen && scan.newlen < h.yd) {
        if (scan.newlen == 0)
            return CODEC_INVALID;
        h.yd = scan.newlen;
    }
    if (h.xd > MAX_SIDE || h.yd > MAX_SIDE || (unsigned long)h.xd * h.yd > MAX_PIXELS)
        return CODEC_TOO_LARGE;
    widths[h.d] = h.xd;
    heights[h.d] = h.yd;
    for (d = h.d; d > 0; d--) {
        widths[d - 1] = (widths[d] + 1u) / 2u;
        heights[d - 1] = (heights[d] + 1u) / 2u;
    }
    /* At most 65536 stripes over all layers, as each layer halves the
       lines, so at most 2^20 entities, each at least 2 bytes of data. */
    stripes = heights[0] / h.l0 + (heights[0] % h.l0 != 0);
    needed = stripes * (h.d + 1u) * h.planes;
    if (scan.count < needed)
        return CODEC_TRUNCATED;

    entities = malloc(needed * sizeof *entities);
    moves = malloc((scan.move_count + 1u) * sizeof *moves);
    pixels = (size_t)h.xd * h.yd;
    image->pixels = malloc(pixels);
    if (h.planes > 8)
        values = calloc(pixels, sizeof *values);
    if (entities == NULL || moves == NULL || image->pixels == NULL ||
        (h.planes > 8 && values == NULL)) {
        result = CODEC_NO_MEMORY;
        goto done;
    }
    memset(&scan, 0, sizeof scan);
    scan.entities = entities;
    scan.moves = moves;
    scan.limit = needed;
    result = scan_data(data, length, start, &scan);
    if (result != CODEC_OK)
        goto done;

    for (k = 0; k < h.planes; k++) {
        struct layer plane;
        result = decode_plane(&h, data, entities, moves, stripes, widths, heights, k, &plane);
        if (result != CODEC_OK)
            goto done;
        merge_plane(&plane, k, h.planes, image->pixels, values);
        free(plane.bits);
    }
    if (h.planes > 1) {
        uint32_t maxval = (1u << h.planes) - 1u;
        for (i = 0; i < pixels; i++) {
            uint32_t v = values != NULL ? values[i] : image->pixels[i];
            image->pixels[i] = (uint8_t)((v * 255u + maxval / 2u) / maxval);
        }
    }
    image->width = h.xd;
    image->height = h.yd;
    image->planes = h.planes;
done:
    free(entities);
    free(moves);
    free(values);
    if (result != CODEC_OK) {
        free(image->pixels);
        memset(image, 0, sizeof *image);
    }
    return result;
}

void jbig_free(struct jbig_image *image)
{
    free(image->pixels);
    memset(image, 0, sizeof *image);
}
