#include "encode.h"
#include <stdlib.h>
#include <string.h>

#define TABLE_SIZE 1024u
#define EMPTY 0xffffffffu
#define BINS 32768u
#define NO_KEY 0xffffffffu

/* Distinct colours, as long as they fit in the registers. */
struct sixel_table {
    uint32_t key[TABLE_SIZE], count[TABLE_SIZE];
    uint8_t reg[TABLE_SIZE];
    unsigned used;
};

struct box { uint8_t lo[3], hi[3]; uint32_t count; };

/* How much splitting the box should help: its pixels at first, so busy
   areas get most colours, then pixels times volume, so rare colours far
   from the rest still get some. */
static uint32_t priority(const struct box *b, int by_volume)
{
    uint32_t volume = 1;
    unsigned i;

    if (b->lo[0] == b->hi[0] && b->lo[1] == b->hi[1] && b->lo[2] == b->hi[2])
        return 0;
    if (!by_volume)
        return b->count;
    for (i = 0; i < 3; i++)
        volume *= (uint32_t)(b->hi[i] - b->lo[i] + 1u);
    /* Pixels are at most 2^24 and volume 2^15; keep the product in range. */
    return (b->count >> 8 | 1u) * volume;
}

/* The colour a pixel is written as, in percent, packed as a key; NO_KEY
   for pixels left undrawn. */
static uint32_t pixel_key(const uint8_t *p)
{
    uint32_t key = 0, c, a = p[3];
    unsigned i;

    if (a < 128)
        return NO_KEY;
    for (i = 0; i < 3; i++) {
        c = (p[i] * a + 255u * (255u - a) + 127u) / 255u;
        key = key * 101u + (c * 100u + 127u) / 255u;
    }
    return key;
}

static void key_rgb(uint32_t key, unsigned *rgb)
{
    rgb[2] = key % 101u;
    rgb[1] = key / 101u % 101u;
    rgb[0] = key / 10201u;
}

static unsigned bin_of(uint32_t key)
{
    unsigned rgb[3], i, bin = 0;

    key_rgb(key, rgb);
    for (i = 0; i < 3; i++)
        bin = bin << 5 | (rgb[i] * 31u + 50u) / 100u;
    return bin;
}

static unsigned slot_of(const struct sixel_table *t, uint32_t key)
{
    unsigned slot = (unsigned)((key * 2654435761u) >> 22) & (TABLE_SIZE - 1u);

    while (t->key[slot] != EMPTY && t->key[slot] != key)
        slot = (slot + 1u) & (TABLE_SIZE - 1u);
    return slot;
}

static void histogram_add(uint32_t *h, uint32_t key, uint32_t count)
{
    unsigned rgb[3];
    uint32_t *bin = h + bin_of(key) * 4u;

    key_rgb(key, rgb);
    bin[0] += count;
    bin[1] += rgb[0] * count;
    bin[2] += rgb[1] * count;
    bin[3] += rgb[2] * count;
}

/* Move from exact colours to a histogram once there are too many. */
static enum codec_result start_histogram(struct sixel_encoder *e)
{
    unsigned slot;

    e->histogram = calloc(BINS * 4u, sizeof *e->histogram);
    if (e->histogram == NULL)
        return CODEC_NO_MEMORY;
    for (slot = 0; slot < TABLE_SIZE; slot++)
        if (e->table->key[slot] != EMPTY)
            histogram_add(e->histogram, e->table->key[slot], e->table->count[slot]);
    e->quantised = 1;
    return CODEC_OK;
}

enum codec_result sixel_encoder_init(struct sixel_encoder *e, unsigned width, unsigned height)
{
    memset(e, 0, sizeof *e);
    if (width == 0 || height == 0 || width > 65535u || height > 65535u)
        return CODEC_INVALID;
    e->width = width;
    e->height = height;
    e->table = malloc(sizeof *e->table);
    e->band = malloc((size_t)width * 6u);
    e->first = malloc(SIXEL_MAX_COLORS * sizeof *e->first);
    e->last = malloc(SIXEL_MAX_COLORS * sizeof *e->last);
    if (e->table == NULL || e->band == NULL || e->first == NULL || e->last == NULL) {
        sixel_encoder_free(e);
        return CODEC_NO_MEMORY;
    }
    memset(e->table->key, 0xff, sizeof e->table->key);
    e->table->used = 0;
    return CODEC_OK;
}

enum codec_result sixel_encoder_scan(struct sixel_encoder *e, const uint8_t *rgba)
{
    struct sixel_table *t = e->table;
    enum codec_result result;
    unsigned x, slot;
    uint32_t key;

    for (x = 0; x < e->width; x++) {
        key = pixel_key(rgba + (size_t)x * 4u);
        if (key == NO_KEY) {
            e->transparent = 1;
            continue;
        }
        if (e->quantised) {
            histogram_add(e->histogram, key, 1);
            continue;
        }
        slot = slot_of(t, key);
        if (t->key[slot] == key) {
            t->count[slot]++;
            continue;
        }
        if (t->used == SIXEL_MAX_COLORS) {
            result = start_histogram(e);
            if (result != CODEC_OK)
                return result;
            histogram_add(e->histogram, key, 1);
            continue;
        }
        t->key[slot] = key;
        t->count[slot] = 1;
        t->used++;
    }
    return CODEC_OK;
}

/* Tighten the box around its occupied bins and count their pixels. */
static void shrink(const uint32_t *h, struct box *b)
{
    unsigned lo[3], hi[3], v[3], i;
    uint32_t count = 0, n;

    for (i = 0; i < 3; i++) {
        lo[i] = b->hi[i];
        hi[i] = b->lo[i];
    }
    for (v[0] = b->lo[0]; v[0] <= b->hi[0]; v[0]++)
        for (v[1] = b->lo[1]; v[1] <= b->hi[1]; v[1]++)
            for (v[2] = b->lo[2]; v[2] <= b->hi[2]; v[2]++) {
                n = h[(v[0] << 10 | v[1] << 5 | v[2]) * 4u];
                if (n == 0)
                    continue;
                count += n;
                for (i = 0; i < 3; i++) {
                    if (v[i] < lo[i])
                        lo[i] = v[i];
                    if (v[i] > hi[i])
                        hi[i] = v[i];
                }
            }
    for (i = 0; i < 3; i++) {
        b->lo[i] = (uint8_t)lo[i];
        b->hi[i] = (uint8_t)hi[i];
    }
    b->count = count;
}

/* Split b along its longest side at the median pixel; the upper part
   goes to c. Both parts are left non-empty. */
static void split(const uint32_t *h, struct box *b, struct box *c)
{
    uint32_t slice[32], sum = 0;
    unsigned axis = 0, i, v[3], cut;

    for (i = 1; i < 3; i++)
        if (b->hi[i] - b->lo[i] > b->hi[axis] - b->lo[axis])
            axis = i;
    memset(slice, 0, sizeof slice);
    for (v[0] = b->lo[0]; v[0] <= b->hi[0]; v[0]++)
        for (v[1] = b->lo[1]; v[1] <= b->hi[1]; v[1]++)
            for (v[2] = b->lo[2]; v[2] <= b->hi[2]; v[2]++)
                slice[v[axis]] += h[(v[0] << 10 | v[1] << 5 | v[2]) * 4u];
    for (cut = b->lo[axis]; cut + 1u < b->hi[axis]; cut++) {
        sum += slice[cut];
        if (sum >= b->count / 2u)
            break;
    }
    *c = *b;
    b->hi[axis] = (uint8_t)cut;
    c->lo[axis] = (uint8_t)(cut + 1u);
    shrink(h, b);
    shrink(h, c);
}

/* Give each occupied bin the register nearest its mean colour. */
static void map_bins(struct sixel_encoder *e, unsigned base)
{
    const uint32_t *hb;
    unsigned bin, reg, i;
    uint32_t mean[3], d, best_d;
    int diff;

    for (bin = 0; bin < BINS; bin++) {
        hb = e->histogram + bin * 4u;
        e->bin_color[bin] = (uint8_t)base;
        if (hb[0] == 0)
            continue;
        for (i = 0; i < 3; i++)
            mean[i] = (hb[1 + i] + hb[0] / 2u) / hb[0];
        best_d = 0xffffffffu;
        for (reg = base; reg < e->colors; reg++) {
            d = 0;
            for (i = 0; i < 3; i++) {
                diff = (int)mean[i] - (int)e->palette[reg][i];
                d += (uint32_t)(diff * diff);
            }
            if (d < best_d) {
                best_d = d;
                e->bin_color[bin] = (uint8_t)reg;
            }
        }
    }
}

static enum codec_result median_cut(struct sixel_encoder *e, unsigned limit, unsigned base)
{
    const uint32_t *h = e->histogram;
    struct box *boxes;
    unsigned count = 1, i, best, reg, v[3];
    uint32_t sum[3];
    uint32_t n;

    boxes = malloc(limit * sizeof *boxes);
    e->bin_color = malloc(BINS);
    if (boxes == NULL || e->bin_color == NULL) {
        free(boxes);
        return CODEC_NO_MEMORY;
    }
    for (i = 0; i < 3; i++) {
        boxes[0].lo[i] = 0;
        boxes[0].hi[i] = 31;
    }
    shrink(h, &boxes[0]);
    while (count < limit) {
        uint32_t top = 0, p;
        best = count;
        for (i = 0; i < count; i++) {
            p = priority(&boxes[i], count >= limit / 2u);
            if (p > top) {
                top = p;
                best = i;
            }
        }
        if (best == count)
            break;
        split(h, &boxes[best], &boxes[count]);
        count++;
    }
    for (i = 0; i < count; i++) {
        struct box *b = &boxes[i];
        reg = base + i;
        sum[0] = sum[1] = sum[2] = 0;
        n = 0;
        for (v[0] = b->lo[0]; v[0] <= b->hi[0]; v[0]++)
            for (v[1] = b->lo[1]; v[1] <= b->hi[1]; v[1]++)
                for (v[2] = b->lo[2]; v[2] <= b->hi[2]; v[2]++) {
                    unsigned bin = v[0] << 10 | v[1] << 5 | v[2];
                    const uint32_t *hb = h + bin * 4u;
                    if (hb[0] == 0)
                        continue;
                    n += hb[0];
                    sum[0] += hb[1];
                    sum[1] += hb[2];
                    sum[2] += hb[3];
                }
        /* The first box may be empty only when every pixel is undrawn. */
        if (n == 0)
            n = 1;
        e->palette[reg][0] = (uint8_t)((sum[0] + n / 2u) / n);
        e->palette[reg][1] = (uint8_t)((sum[1] + n / 2u) / n);
        e->palette[reg][2] = (uint8_t)((sum[2] + n / 2u) / n);
    }
    free(boxes);
    e->colors = base + count;
    map_bins(e, base);
    return CODEC_OK;
}

enum codec_result sixel_encoder_plan(struct sixel_encoder *e)
{
    struct sixel_table *t = e->table;
    unsigned base = e->transparent ? 1u : 0u, slot, rgb[3];
    enum codec_result result;

    /* Register 0 is the background, which ImageMagick shows where pixels
       are undrawn; white matches compositing over white. */
    if (base == 1) {
        e->palette[0][0] = e->palette[0][1] = e->palette[0][2] = 100;
    }
    if (!e->quantised && t->used > SIXEL_MAX_COLORS - base) {
        result = start_histogram(e);
        if (result != CODEC_OK)
            return result;
    }
    if (e->quantised)
        return median_cut(e, SIXEL_MAX_COLORS - base, base);
    e->colors = base;
    for (slot = 0; slot < TABLE_SIZE; slot++) {
        if (t->key[slot] == EMPTY)
            continue;
        key_rgb(t->key[slot], rgb);
        t->reg[slot] = (uint8_t)e->colors;
        e->palette[e->colors][0] = (uint8_t)rgb[0];
        e->palette[e->colors][1] = (uint8_t)rgb[1];
        e->palette[e->colors][2] = (uint8_t)rgb[2];
        e->colors++;
    }
    /* Even an image with no drawn pixels defines one register. */
    if (e->colors == 0) {
        e->palette[0][0] = e->palette[0][1] = e->palette[0][2] = 100;
        e->colors = 1;
    }
    return CODEC_OK;
}

static char *put_uint(char *out, unsigned long n)
{
    char digits[12];
    unsigned count = 0;

    do {
        digits[count++] = (char)('0' + n % 10u);
        n /= 10u;
    } while (n != 0);
    while (count > 0)
        *out++ = digits[--count];
    return out;
}

size_t sixel_encoder_header(struct sixel_encoder *e, char *out, size_t capacity)
{
    char *p = out;
    unsigned reg, i;

    if (capacity < SIXEL_HEADER_MAX)
        return 0;
    /* Aspect 1:1 in the raster attributes, which override P1. */
    memcpy(p, e->transparent ? "\033P0;1;0q\"1;1;" : "\033P0;0;0q\"1;1;", 13);
    p += 13;
    p = put_uint(p, e->width);
    *p++ = ';';
    p = put_uint(p, e->height);
    for (reg = 0; reg < e->colors; reg++) {
        *p++ = '#';
        p = put_uint(p, reg);
        *p++ = ';';
        *p++ = '2';
        for (i = 0; i < 3; i++) {
            *p++ = ';';
            p = put_uint(p, e->palette[reg][i]);
        }
    }
    e->written = (unsigned long)(p - out);
    return (size_t)(p - out);
}

int sixel_encoder_add_row(struct sixel_encoder *e, const uint8_t *rgba)
{
    uint8_t *row;
    unsigned x, reg, slot;
    uint32_t key;

    if (e->band_rows == 0) {
        for (reg = 0; reg < SIXEL_MAX_COLORS; reg++) {
            e->first[reg] = 0xffffu;
            e->last[reg] = 0;
        }
        e->cursor = 0;
        e->lines = 0;
    }
    row = e->band + (size_t)e->band_rows * e->width;
    for (x = 0; x < e->width; x++) {
        key = pixel_key(rgba + (size_t)x * 4u);
        if (key == NO_KEY) {
            /* Only images with undrawn pixels reserve register 0. */
            row[x] = 0;
            continue;
        }
        if (e->quantised) {
            reg = e->bin_color[bin_of(key)];
        } else {
            slot = slot_of(e->table, key);
            reg = e->table->key[slot] == key ? e->table->reg[slot] : 0;
        }
        row[x] = (uint8_t)reg;
        if (x < e->first[reg])
            e->first[reg] = (uint16_t)x;
        if (x > e->last[reg])
            e->last[reg] = (uint16_t)x;
    }
    e->band_rows++;
    e->rows++;
    return e->band_rows == 6 || e->rows == e->height;
}

/* ImageMagick stops reading at a repeat count larger than the whole file,
   so no count exceeds what is already written: out holds the current
   piece, which starts after e->written bytes. Splitting keeps a run no
   longer than its pixel count. */
static char *put_run(const struct sixel_encoder *e, const char *out, char *p,
                     unsigned ch, unsigned long count)
{
    unsigned long n, limit;

    while (count > 0) {
        limit = e->written + (unsigned long)(p - out);
        n = count < limit ? count : limit;
        if (n >= 4) {
            *p++ = '!';
            p = put_uint(p, n);
            *p++ = (char)ch;
        } else {
            n = count < 4 ? count : 1;
            while (n-- > 0) {
                *p++ = (char)ch;
                count--;
            }
            continue;
        }
        count -= n;
    }
    return p;
}

size_t sixel_line_capacity(unsigned width)
{
    return (size_t)width + 16u;
}

size_t sixel_encoder_next(struct sixel_encoder *e, char *out, size_t capacity)
{
    char *p = out;
    unsigned reg, x, k, bits, ch, run_ch = 0, more;
    unsigned long run = 0;

    if (e->band_rows == 0 || capacity < sixel_line_capacity(e->width))
        return 0;
    reg = e->cursor;
    while (reg < e->colors && (e->first[reg] > e->last[reg] || (reg == 0 && e->transparent)))
        reg++;
    if (reg >= e->colors) {
        /* A band with nothing drawn still moves down. */
        e->band_rows = 0;
        if (e->lines > 0)
            return 0;
        *p++ = '-';
        e->written++;
        return 1;
    }
    *p++ = '#';
    p = put_uint(p, reg);
    if (e->first[reg] > 0)
        p = put_run(e, out, p, '?', e->first[reg]);
    for (x = e->first[reg]; x <= e->last[reg]; x++) {
        bits = 0;
        for (k = 0; k < e->band_rows; k++)
            if (e->band[(size_t)k * e->width + x] == reg)
                bits |= 1u << k;
        ch = 0x3fu + bits;
        if (run > 0 && ch != run_ch) {
            p = put_run(e, out, p, run_ch, run);
            run = 0;
        }
        run_ch = ch;
        run++;
    }
    p = put_run(e, out, p, run_ch, run);
    e->cursor = reg + 1;
    e->lines++;
    /* Carriage return between colours, line feed after the last. */
    for (more = 0, reg = e->cursor; reg < e->colors && !more; reg++)
        more = e->first[reg] <= e->last[reg] && !(reg == 0 && e->transparent);
    *p++ = more ? '$' : '-';
    if (!more)
        e->cursor = e->colors;
    e->written += (unsigned long)(p - out);
    return (size_t)(p - out);
}

size_t sixel_encoder_end(char *out, size_t capacity)
{
    if (capacity < 2)
        return 0;
    out[0] = '\033';
    out[1] = '\\';
    return 2;
}

void sixel_encoder_free(struct sixel_encoder *e)
{
    free(e->table);
    free(e->histogram);
    free(e->bin_color);
    free(e->band);
    free(e->first);
    free(e->last);
    e->table = NULL;
    e->histogram = NULL;
    e->bin_color = NULL;
    e->band = NULL;
    e->first = NULL;
    e->last = NULL;
}
