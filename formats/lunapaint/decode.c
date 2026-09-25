#include "decode.h"
#include <stdlib.h>
#include <string.h>

/* Lunapaint saves its structures as they sit in memory, in the byte order of
   the machine that saved them, and records no byte order. The header is
   version[16], projectName[128], author[128], then five shorts: width,
   height, layers, frames and an object count. A 32-bit description length
   and the description follow. Then come the pixel buffers, one per layer
   per frame (frame 0's layers first), each a 64-bit word per pixel with red
   in the top 16 bits and alpha in the bottom 16. Last is the object table:
   records of four 32-bit words (type, layer, frame, data length) and their
   data, giving each buffer's opacity, visibility and name. */

#define MAGIC "Lunapaint_v1"
#define HEADER_SIZE 282u
#define DATA_START (HEADER_SIZE + 4u)
#define MAX_PIXELS (16u * 1024u * 1024u)
/* Real object tables are a few dozen bytes per buffer. */
#define MAX_TABLE (4u * 1024u * 1024u)
#define CHUNK 65536u

enum { OBJ_OPACITY = 1, OBJ_VISIBILITY = 2, OBJ_NAME = 3 };

struct layout {
    int big_endian;
    int fits;               /* the description ends inside the file */
    unsigned width, height, layers, frames;
    uint64_t data;          /* first pixel buffer */
    uint64_t table;         /* object table, after the last buffer */
    uint64_t buffer_size;
};

static unsigned get16(const uint8_t *p, int big_endian)
{
    return big_endian ? (unsigned)p[0] << 8 | p[1]
                      : (unsigned)p[1] << 8 | p[0];
}

static uint32_t get32(const uint8_t *p, int big_endian)
{
    return big_endian
        ? (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]
        : (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}

/* The header's sizes are C shorts, so Lunapaint itself can't use a value
   with the top bit set. */
static int positive16(unsigned v)
{
    return v > 0 && v < 0x8000u;
}

static enum codec_result read_layout(const uint8_t *header, uint64_t size,
                                     int big_endian, struct layout *out)
{
    uint64_t pixels, buffers;
    out->big_endian = big_endian;
    out->data = DATA_START + (uint64_t)get32(header + HEADER_SIZE, big_endian);
    out->fits = out->data <= size;
    out->width = get16(header + 272, big_endian);
    out->height = get16(header + 274, big_endian);
    out->layers = get16(header + 276, big_endian);
    out->frames = get16(header + 278, big_endian);
    if (!positive16(out->width) || !positive16(out->height) ||
        !positive16(out->layers) || !positive16(out->frames))
        return CODEC_INVALID;
    pixels = (uint64_t)out->width * out->height;
    if (pixels > MAX_PIXELS)
        return CODEC_TOO_LARGE;
    out->buffer_size = pixels * 8u;
    buffers = (uint64_t)out->layers * out->frames;
    out->table = out->data + buffers * out->buffer_size;
    if (out->table > size)
        return CODEC_TRUNCATED;
    return CODEC_OK;
}

/* Walk the object table the way Lunapaint's loader does: opacity and
   visibility records carry one byte of data, names carry their length, and
   other types carry nothing. When apply is set, store the attributes of
   frame's buffers. Returns 1 when every record is well formed and the
   table ends exactly at the end of the file. */
static int walk_table(const uint8_t *table, size_t length, int complete,
                      const struct layout *l, unsigned frame,
                      uint8_t *opacity, uint8_t *visible)
{
    size_t at = 0;
    while (length - at >= 16) {
        const uint8_t *p = table + at;
        uint32_t type = get32(p, l->big_endian);
        uint32_t layer = get32(p + 4, l->big_endian);
        uint32_t in_frame = get32(p + 8, l->big_endian);
        uint32_t data = get32(p + 12, l->big_endian);
        size_t need;
        at += 16;
        if (type == OBJ_OPACITY || type == OBJ_VISIBILITY)
            need = 1;
        else if (type == OBJ_NAME)
            need = data;
        else
            return 0;       /* the rest can't be trusted to line up */
        if (need > length - at)
            return 0;
        if (layer >= l->layers || in_frame >= l->frames) {
            /* Lunapaint would crash here; skip the record. */
            at += need;
            complete = 0;
            continue;
        }
        if (opacity != NULL && in_frame == frame) {
            if (type == OBJ_OPACITY)
                opacity[layer] = table[at] > 100 ? 100 : table[at];
            else if (type == OBJ_VISIBILITY)
                visible[layer] = table[at] == 1;
        }
        at += need;
    }
    return complete && at == length;
}

/* Read the object table, or as much of it as MAX_TABLE allows; *complete
   says whether it all fitted. NULL with length 0 is an empty table. */
static enum codec_result load_table(const struct lunapaint_source *source,
                                    const struct layout *l, uint8_t **table,
                                    size_t *length, int *complete)
{
    uint64_t left = source->size - l->table;
    *complete = left <= MAX_TABLE;
    *length = (size_t)(*complete ? left : MAX_TABLE);
    *table = NULL;
    if (*length == 0)
        return CODEC_OK;
    *table = malloc(*length);
    if (*table == NULL)
        return CODEC_NO_MEMORY;
    if (source->read(source->context, l->table, *table, *length) != 0) {
        free(*table);
        *table = NULL;
        return CODEC_TRUNCATED;
    }
    return CODEC_OK;
}

static int exact(const struct lunapaint_source *source, const struct layout *l)
{
    uint8_t *table;
    size_t length;
    int complete, result;
    if (load_table(source, l, &table, &length, &complete) != CODEC_OK)
        return 0;
    result = walk_table(table, length, complete, l, 0, NULL, NULL);
    free(table);
    return result;
}

/* Pick the byte order that explains the file: both are read, and one whose
   object table ends exactly at the end of the file wins. Little-endian,
   the order of most AROS machines, breaks ties. */
static enum codec_result find_layout(const struct lunapaint_source *source,
                                     struct layout *out)
{
    uint8_t header[DATA_START];
    struct layout le, be;
    enum codec_result rle, rbe;
    if (source->size < DATA_START) {
        if (source->size >= sizeof MAGIC - 1 &&
            source->read(source->context, 0, header, sizeof MAGIC - 1) == 0 &&
            memcmp(header, MAGIC, sizeof MAGIC - 1) != 0)
            return CODEC_INVALID;
        return CODEC_TRUNCATED;
    }
    if (source->read(source->context, 0, header, DATA_START) != 0)
        return CODEC_TRUNCATED;
    if (memcmp(header, MAGIC, sizeof MAGIC - 1) != 0)
        return CODEC_INVALID;
    rle = read_layout(header, source->size, 0, &le);
    rbe = read_layout(header, source->size, 1, &be);
    if (rle == CODEC_OK && rbe == CODEC_OK)
        *out = !exact(source, &le) && exact(source, &be) ? be : le;
    else if (rle == CODEC_OK)
        *out = le;
    else if (rbe == CODEC_OK)
        *out = be;
    /* Neither works: report the order whose description fits the file. */
    else if (le.fits != be.fits)
        return le.fits ? rle : rbe;
    else if (rle == CODEC_TRUNCATED || rbe == CODEC_TRUNCATED)
        return CODEC_TRUNCATED;
    else if (rle == CODEC_TOO_LARGE || rbe == CODEC_TOO_LARGE)
        return CODEC_TOO_LARGE;
    else
        return CODEC_INVALID;
    return CODEC_OK;
}

/* Blend one layer pixel over the picture so far, in 8 bits as Lunapaint
   does. Over an opaque pixel this is Lunapaint's own arithmetic; elsewhere
   it is straight-alpha "over", where Lunapaint's export would mix in grey
   and add the alphas. */
static void blend(uint8_t *d, unsigned r, unsigned g, unsigned b, int a)
{
    unsigned da = d[3];
    if (a <= 0)
        return;
    if (da == 255) {
        double alpha = a / 255.0;
        d[0] = (uint8_t)(d[0] - ((int)d[0] - (int)r) * alpha);
        d[1] = (uint8_t)(d[1] - ((int)d[1] - (int)g) * alpha);
        d[2] = (uint8_t)(d[2] - ((int)d[2] - (int)b) * alpha);
    } else if (da == 0) {
        d[0] = (uint8_t)r;
        d[1] = (uint8_t)g;
        d[2] = (uint8_t)b;
        d[3] = (uint8_t)a;
    } else {
        unsigned src = (unsigned)a * 255u, dst = da * (255u - (unsigned)a);
        unsigned den = src + dst;
        d[0] = (uint8_t)((r * src + d[0] * dst + den / 2) / den);
        d[1] = (uint8_t)((g * src + d[1] * dst + den / 2) / den);
        d[2] = (uint8_t)((b * src + d[2] * dst + den / 2) / den);
        d[3] = (uint8_t)((den + 127u) / 255u);
    }
}

static enum codec_result composite(const struct lunapaint_source *source,
                                   const struct layout *l, unsigned frame,
                                   unsigned layer, unsigned opacity,
                                   uint8_t *rgba, uint8_t *chunk)
{
    size_t row = (size_t)l->width * 8u;
    unsigned rows = row >= CHUNK ? 1u : (unsigned)(CHUNK / row);
    uint64_t offset = l->data +
        ((uint64_t)frame * l->layers + layer) * l->buffer_size;
    /* The top byte of each 16-bit channel, as Lunapaint shows it. */
    unsigned ri = l->big_endian ? 0 : 7, gi = l->big_endian ? 2 : 5;
    unsigned bi = l->big_endian ? 4 : 3, ai = l->big_endian ? 6 : 1;
    unsigned y = 0;
    uint8_t *d = rgba;
    while (y < l->height) {
        unsigned n = l->height - y < rows ? l->height - y : rows;
        size_t i, count = (size_t)n * l->width;
        const uint8_t *p = chunk;
        if (source->read(source->context, offset, chunk, row * n) != 0)
            return CODEC_TRUNCATED;
        for (i = 0; i < count; i++, p += 8, d += 4) {
            int a = p[ai];
            if (opacity < 100)
                a = (int)(a / 100.0 * opacity);
            blend(d, p[ri], p[gi], p[bi], a);
        }
        offset += row * n;
        y += n;
    }
    return CODEC_OK;
}

enum codec_result lunapaint_decode(const struct lunapaint_source *source,
                                   unsigned index,
                                   struct lunapaint_image *image)
{
    struct layout l;
    uint8_t *table = NULL, *opacity = NULL, *visible, *chunk = NULL;
    size_t length, row;
    int complete;
    unsigned layer;
    enum codec_result result;

    memset(image, 0, sizeof *image);
    result = find_layout(source, &l);
    if (result != CODEC_OK)
        return result;
    image->frames = l.frames;
    if (index >= l.frames)
        return CODEC_INVALID;
    result = load_table(source, &l, &table, &length, &complete);
    if (result != CODEC_OK)
        return result;
    /* Buffers without records keep Lunapaint's defaults. */
    opacity = malloc((size_t)l.layers * 2u);
    row = (size_t)l.width * 8u;
    chunk = malloc(row >= CHUNK ? row : CHUNK);
    image->rgba = calloc((size_t)l.width * l.height, 4u);
    if (opacity == NULL || chunk == NULL || image->rgba == NULL) {
        result = CODEC_NO_MEMORY;
        goto done;
    }
    visible = opacity + l.layers;
    memset(opacity, 100, l.layers);
    memset(visible, 1, l.layers);
    walk_table(table, length, complete, &l, index, opacity, visible);
    for (layer = 0; layer < l.layers && result == CODEC_OK; layer++)
        if (visible[layer] && opacity[layer] != 0)
            result = composite(source, &l, index, layer, opacity[layer],
                               image->rgba, chunk);
    image->width = l.width;
    image->height = l.height;
done:
    free(table);
    free(opacity);
    free(chunk);
    if (result != CODEC_OK) {
        free(image->rgba);
        image->rgba = NULL;
        image->width = image->height = 0;
    }
    return result;
}

void lunapaint_free(struct lunapaint_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
}
