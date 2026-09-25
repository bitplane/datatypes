#include "encode.h"
#include "decode.h"
#include "palette.h"
#include <stdlib.h>
#include <string.h>

#define CACHE 4096u
#define MISSING 0xffffu

/* Colour to index, direct mapped; key 0 is empty, since keys carry bit 24. */
struct cache { uint32_t key[CACHE]; uint16_t index[CACHE]; };

struct wal_encoder {
    unsigned width, height, row;
    size_t size;
    uint8_t *file;
    struct cache exact, near;
};

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static uint32_t key(unsigned r, unsigned g, unsigned b)
{
    return 0x1000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | b;
}

static unsigned slot(uint32_t k)
{
    return (unsigned)((k * 2654435761u) >> 20) & (CACHE - 1u);
}

/* First index with exactly this colour, or MISSING. */
static unsigned exact(unsigned r, unsigned g, unsigned b)
{
    unsigned i;
    for (i = 0; i < 256; i++) {
        const uint8_t *p = wal_palette + i * 3u;
        if (p[0] == r && p[1] == g && p[2] == b)
            return i;
    }
    return MISSING;
}

/* Closest colour, leaving out 255, which Quake 2 draws transparent. */
static unsigned nearest(unsigned r, unsigned g, unsigned b)
{
    unsigned i, best = 0;
    long best_d = -1;
    for (i = 0; i < 255; i++) {
        const uint8_t *p = wal_palette + i * 3u;
        long dr = (long)p[0] - (long)r, dg = (long)p[1] - (long)g,
             db = (long)p[2] - (long)b, d = dr * dr + dg * dg + db * db;
        if (best_d < 0 || d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

static unsigned lookup(struct cache *c, unsigned (*find)(unsigned, unsigned, unsigned),
                       unsigned r, unsigned g, unsigned b)
{
    uint32_t k = key(r, g, b);
    unsigned s = slot(k);
    if (c->key[s] != k) {
        c->key[s] = k;
        c->index[s] = (uint16_t)find(r, g, b);
    }
    return c->index[s];
}

static size_t level_size(unsigned width, unsigned height, unsigned level)
{
    return (size_t)(width >> level) * (height >> level);
}

struct wal_encoder *wal_encoder_new(unsigned width, unsigned height)
{
    struct wal_encoder *e;
    size_t offset = WAL_HEADER;
    unsigned level;

    if (width == 0 || height == 0 || width > 65535u || height > 65535u ||
        (size_t)width * height > 16u * 1024u * 1024u)
        return NULL;
    e = calloc(1, sizeof *e);
    if (e == NULL)
        return NULL;
    e->width = width;
    e->height = height;
    for (level = 0; level < WAL_LEVELS; level++)
        e->size += level_size(width, height, level);
    e->size += WAL_HEADER;
    e->file = calloc(1, e->size);
    if (e->file == NULL) {
        free(e);
        return NULL;
    }
    put32(e->file + 32, width);
    put32(e->file + 36, height);
    for (level = 0; level < WAL_LEVELS; level++) {
        put32(e->file + 40 + level * 4u, (uint32_t)offset);
        offset += level_size(width, height, level);
    }
    return e;
}

static unsigned over_white(const uint8_t *pixel, unsigned channel)
{
    unsigned a = pixel[3];
    return (pixel[channel] * a + 255u * (255u - a) + 127u) / 255u;
}

int wal_encoder_row(struct wal_encoder *e, const uint8_t *rgba)
{
    uint8_t *out;
    unsigned x;

    if (e == NULL || rgba == NULL || e->row >= e->height)
        return 0;
    out = e->file + WAL_HEADER + (size_t)e->row * e->width;
    for (x = 0; x < e->width; x++, rgba += 4) {
        unsigned i = lookup(&e->exact, exact, over_white(rgba, 0),
                            over_white(rgba, 1), over_white(rgba, 2));
        if (i == MISSING)
            return 0;
        out[x] = (uint8_t)i;
    }
    e->row++;
    return 1;
}

/* Each mip pixel is the nearest colour to the mean of four in the level above. */
static void shrink(struct wal_encoder *e, const uint8_t *src, unsigned src_w,
                   uint8_t *dst, unsigned w, unsigned h)
{
    unsigned x, y, c, sum[3];
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            const uint8_t *s = src + (size_t)y * 2u * src_w + x * 2u;
            const uint8_t *p[4];
            p[0] = wal_palette + s[0] * 3u;
            p[1] = wal_palette + s[1] * 3u;
            p[2] = wal_palette + s[src_w] * 3u;
            p[3] = wal_palette + s[src_w + 1u] * 3u;
            for (c = 0; c < 3; c++)
                sum[c] = (p[0][c] + p[1][c] + p[2][c] + p[3][c] + 2u) / 4u;
            dst[(size_t)y * w + x] = (uint8_t)lookup(&e->near, nearest,
                                                     sum[0], sum[1], sum[2]);
        }
}

const uint8_t *wal_encoder_finish(struct wal_encoder *e, size_t *size)
{
    uint8_t *src, *dst;
    unsigned level;

    if (e == NULL || size == NULL || e->row != e->height)
        return NULL;
    src = e->file + WAL_HEADER;
    for (level = 1; level < WAL_LEVELS; level++) {
        dst = src + level_size(e->width, e->height, level - 1);
        shrink(e, src, e->width >> (level - 1), dst,
               e->width >> level, e->height >> level);
        src = dst;
    }
    *size = e->size;
    return e->file;
}

void wal_encoder_free(struct wal_encoder *e)
{
    if (e == NULL)
        return;
    free(e->file);
    free(e);
}
