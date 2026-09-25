#include <stdlib.h>
#include <string.h>

#include "decode.h"
#include "lab.h"

#define PSD_MAX_PIXELS (16u * 1024u * 1024u)
#define PSD_BUFFER 4096u

enum { BITMAP = 0, GRAY = 1, INDEXED = 2, RGB = 3, CMYK = 4,
       MULTICHANNEL = 7, DUOTONE = 8, LAB = 9 };

struct psd {
    const struct psd_source *src;
    int big;                    /* PSB: some lengths are 64-bit */
    unsigned channels, width, height, depth, mode;
    unsigned colours;           /* channels that make up the colour */
    int alpha;                  /* channel `colours` is the transparency */
    long transparent;           /* indexed colour shown clear, or -1 */
    uint8_t palette[768];
    unsigned names;             /* channel names in resource 1006 */
    int merged_alpha;           /* the file says the composite has alpha */
    int matted;                 /* undo Photoshop's white matte */
    int straight;               /* a pixel shows the colour wasn't matted */
    int fake;                   /* saved without a real composite */
};

/* One channel's data, read sequentially through a small buffer. */
struct stream {
    const struct psd_source *src;
    uint64_t pos, end;          /* next source offset to buffer, and limit */
    size_t at, have;
    uint8_t buf[PSD_BUFFER];
};

static unsigned be16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8 | p[3];
}

static uint64_t be64(const uint8_t *p)
{
    return (uint64_t)be32(p) << 32 | be32(p + 4);
}

static int get(const struct psd *p, uint64_t offset, void *buf, size_t n)
{
    const struct psd_source *s = p->src;
    return offset <= s->size && n <= s->size - offset &&
           s->read(s->context, offset, buf, n) == n;
}

/* A 4-byte length, or 8 bytes in a PSB where wide is set. */
static int get_length(const struct psd *p, uint64_t offset, int wide,
                      uint64_t *length)
{
    uint8_t b[8];
    if (!get(p, offset, b, wide ? 8u : 4u))
        return 0;
    *length = wide ? be64(b) : be32(b);
    return 1;
}

static enum codec_result header(struct psd *p)
{
    uint8_t h[26];
    uint64_t pixels;

    if (!get(p, 0, h, 4))
        return CODEC_TRUNCATED;
    if (memcmp(h, "8BPS", 4) != 0)
        return CODEC_INVALID;
    if (!get(p, 0, h, sizeof h))
        return CODEC_TRUNCATED;
    if (be16(h + 4) != 1 && be16(h + 4) != 2)
        return CODEC_INVALID;
    p->big = be16(h + 4) == 2;
    p->channels = be16(h + 12);
    p->height = be32(h + 14) > 65535u ? 65536u : (unsigned)be32(h + 14);
    p->width = be32(h + 18) > 65535u ? 65536u : (unsigned)be32(h + 18);
    p->depth = be16(h + 22);
    p->mode = be16(h + 24);
    if (p->channels < 1 || p->channels > 56 || p->width == 0 || p->height == 0)
        return CODEC_INVALID;
    switch (p->mode) {
    case BITMAP:
        if (p->depth != 1)
            return CODEC_INVALID;
        p->colours = 1;
        break;
    case INDEXED:
        if (p->depth != 8)
            return CODEC_INVALID;
        p->colours = 1;
        break;
    case MULTICHANNEL:
        /* Shown as ImageMagick does: the first three channels as RGB. */
        if (p->depth != 8 && p->depth != 16)
            return CODEC_INVALID;
        p->colours = p->channels >= 3 ? 3u : 1u;
        break;
    case GRAY: case DUOTONE:
    case RGB: case LAB: case CMYK:
        if (p->depth != 8 && p->depth != 16)
            return CODEC_INVALID;
        p->colours = p->mode == RGB || p->mode == LAB ? 3u :
                     p->mode == CMYK ? 4u : 1u;
        break;
    default:
        return CODEC_INVALID;
    }
    if (p->channels < p->colours)
        return CODEC_INVALID;
    pixels = (uint64_t)p->width * p->height;
    if (p->width > 65535u || p->height > 65535u || pixels > PSD_MAX_PIXELS)
        return CODEC_TOO_LARGE;
    return CODEC_OK;
}

/* The palette of an indexed image: all reds, then greens, then blues. */
static enum codec_result colour_mode(struct psd *p, uint64_t *pos)
{
    uint64_t length;
    unsigned n, i, c;

    if (!get_length(p, *pos, 0, &length) || length > p->src->size - *pos - 4)
        return CODEC_TRUNCATED;
    if (p->mode == INDEXED) {
        if (length < 3)
            return CODEC_INVALID;
        n = length / 3 > 256 ? 256u : (unsigned)(length / 3);
        for (c = 0; c < 3; c++) {
            uint8_t level[256];
            if (!get(p, *pos + 4 + c * (length / 3), level, n))
                return CODEC_TRUNCATED;
            for (i = 0; i < n; i++)
                p->palette[i * 3 + c] = level[i];
        }
    }
    *pos += 4 + length;
    return CODEC_OK;
}

static void resource(struct psd *p, unsigned id, uint64_t at, uint32_t size)
{
    uint8_t b[2];
    uint64_t end = at + size;

    if (id == 1057 && size >= 5 && get(p, at + 4, b, 1))
        p->fake = b[0] == 0;
    else if (id == 1047 && size >= 2 && get(p, at, b, 2))
        p->transparent = (long)be16(b);
    else if (id == 1006)
        /* Pascal strings; Photoshop names every channel it adds itself. */
        while (p->names < 64 && at < end && get(p, at, b, 1)) {
            at += 1u + b[0];
            if (at > end)
                break;
            p->names++;
        }
}

/* Image resources. Malformed ones end the walk rather than the load. */
static enum codec_result resources(struct psd *p, uint64_t *pos)
{
    uint64_t length, r, end;
    uint8_t h[7], b[4];

    if (!get_length(p, *pos, 0, &length) || length > p->src->size - *pos - 4)
        return CODEC_TRUNCATED;
    r = *pos + 4;
    end = r + length;
    while (r + 12 <= end && get(p, r, h, sizeof h)) {
        uint64_t data = r + 6 + ((2u + h[6]) & ~1u);
        uint32_t size;
        if (data + 4 > end || !get(p, data, b, 4))
            break;
        size = be32(b);
        data += 4;
        if (size > end - data)
            break;
        resource(p, be16(h + 4), data, size);
        r = data + size + (size & 1u);
    }
    *pos = end;
    return CODEC_OK;
}

static int is_signature(const struct psd *p, uint64_t at, uint64_t end)
{
    uint8_t s[4];
    return at + 4 <= end && get(p, at, s, 4) &&
           (memcmp(s, "8BIM", 4) == 0 || memcmp(s, "8B64", 4) == 0);
}

/* PSB widens these keys' lengths to 64 bits. */
static int wide_key(const struct psd *p, const uint8_t *key)
{
    static const char keys[] = "LMskLr16Lr32LayrMt16Mt32MtrnAlphFMsklnk2FEidFXidPxSD";
    size_t i;
    if (!p->big)
        return 0;
    for (i = 0; i < sizeof keys - 1; i += 4)
        if (memcmp(keys + i, key, 4) == 0)
            return 1;
    return 0;
}

static void layer_count(struct psd *p, uint64_t at)
{
    uint8_t b[2];
    if (get(p, at, b, 2) && (b[0] & 0x80u))
        p->merged_alpha = 1;
}

/* A negative layer count, in the layer info or in a Layr, Lr16 or Lr32
   block, says the first extra channel is the composite's transparency, as
   does an Mtrn, Mt16 or Mt32 block. Nothing else here matters for the
   composite, so malformed structures just end the search. */
static enum codec_result layers(struct psd *p, uint64_t *pos)
{
    unsigned wide = p->big ? 8u : 4u;
    uint64_t length, q, end, n;
    uint8_t h[8];

    if (!get_length(p, *pos, p->big, &length) ||
        length > p->src->size - *pos - wide)
        return CODEC_TRUNCATED;
    q = *pos + wide;
    end = q + length;
    *pos = end;
    if (q + wide > end || !get_length(p, q, p->big, &n) || n > end - q - wide)
        return CODEC_OK;
    q += wide;
    if (n >= 2)
        layer_count(p, q);
    q += n;
    if (q + 4 > end || !get_length(p, q, 0, &n) || n > end - q - 4)
        return CODEC_OK;
    q += 4 + n;
    while (is_signature(p, q, end) && q + 12 <= end && get(p, q, h, 8)) {
        unsigned size = wide_key(p, h + 4) ? 8u : 4u;
        uint64_t data = q + 8 + size;
        if (data > end || !get_length(p, q + 8, size == 8, &n) || n > end - data)
            break;
        if (memcmp(h + 4, "Mtrn", 4) == 0 || memcmp(h + 4, "Mt16", 4) == 0 ||
            memcmp(h + 4, "Mt32", 4) == 0)
            p->merged_alpha = 1;
        if ((memcmp(h + 4, "Layr", 4) == 0 || memcmp(h + 4, "Lr16", 4) == 0 ||
             memcmp(h + 4, "Lr32", 4) == 0) && n >= 2)
            layer_count(p, data);
        /* Photoshop pads blocks to 4 bytes without counting the padding. */
        q = data + n;
        if (!is_signature(p, q, end))
            q = data + ((n + 3u) & ~(uint64_t)3u);
    }
    return CODEC_OK;
}

static int refill(struct stream *s)
{
    size_t n = s->end - s->pos < PSD_BUFFER ? (size_t)(s->end - s->pos)
                                           : PSD_BUFFER;
    if (n == 0 || s->src->read(s->src->context, s->pos, s->buf, n) != n)
        return 0;
    s->pos += n;
    s->at = 0;
    s->have = n;
    return 1;
}

static int take(struct stream *s, uint8_t *out, size_t n)
{
    while (n > 0) {
        size_t part;
        if (s->at == s->have && !refill(s))
            return 0;
        part = s->have - s->at < n ? s->have - s->at : n;
        if (out != NULL) {
            memcpy(out, s->buf + s->at, part);
            out += part;
        }
        s->at += part;
        n -= part;
    }
    return 1;
}

static void open_stream(struct stream *s, const struct psd_source *src,
                        uint64_t start, uint64_t length)
{
    s->src = src;
    s->pos = start;
    s->end = start + length;
    s->at = s->have = 0;
}

/* One PackBits row from count bytes. A run past the row's end is clipped;
   a row whose bytes run out first is corrupt. */
static enum codec_result unpack(struct stream *s, uint32_t count,
                                uint8_t *out, size_t length)
{
    size_t done = 0;
    uint8_t h, v;

    while (done < length) {
        size_t n, keep;
        if (count == 0)
            return CODEC_INVALID;
        if (!take(s, &h, 1))
            return CODEC_TRUNCATED;
        count--;
        if (h == 128)
            continue;
        n = h < 128 ? h + 1u : 257u - h;
        keep = n < length - done ? n : length - done;
        if (h < 128) {
            if (n > count)
                return CODEC_INVALID;
            if (!take(s, out + done, keep) || !take(s, NULL, n - keep))
                return CODEC_TRUNCATED;
            count -= (uint32_t)n;
        } else {
            if (count == 0)
                return CODEC_INVALID;
            if (!take(s, &v, 1))
                return CODEC_TRUNCATED;
            count--;
            memset(out + done, v, keep);
        }
        done += keep;
    }
    return take(s, NULL, count) ? CODEC_OK : CODEC_TRUNCATED;
}

struct planes {
    struct stream *streams;
    uint32_t *counts;           /* per used channel and row, for RLE */
    uint8_t *raw;
    uint16_t *rows;             /* one 16-bit row per used channel */
};

static enum codec_result open_raw(struct psd *p, struct planes *pl,
                                  uint64_t base, unsigned used, size_t bytes)
{
    uint64_t plane = (uint64_t)bytes * p->height;
    unsigned c;
    if (plane * p->channels > p->src->size - base)
        return CODEC_TRUNCATED;
    for (c = 0; c < used; c++)
        open_stream(&pl->streams[c], p->src, base + c * plane, plane);
    return CODEC_OK;
}

/* The RLE row lengths for every channel, then each channel's data. The
   spare stream after the used channels reads the table. */
static enum codec_result open_rle(struct psd *p, struct planes *pl,
                                  uint64_t base, unsigned used)
{
    unsigned size = p->big ? 4u : 2u, c, y;
    uint64_t table = (uint64_t)p->channels * p->height * size, at;
    struct stream *s = &pl->streams[used];

    if (table > p->src->size - base)
        return CODEC_TRUNCATED;
    pl->counts = malloc((size_t)used * p->height * sizeof *pl->counts);
    if (pl->counts == NULL)
        return CODEC_NO_MEMORY;
    open_stream(s, p->src, base, table);
    at = base + table;
    for (c = 0; c < p->channels; c++) {
        uint64_t total = 0;
        for (y = 0; y < p->height; y++) {
            uint8_t b[4];
            uint32_t n;
            if (!take(s, b, size))
                return CODEC_TRUNCATED;
            n = size == 4 ? be32(b) : be16(b);
            if (c < used)
                pl->counts[(size_t)c * p->height + y] = n;
            total += n;
        }
        if (total > p->src->size - at)
            return CODEC_TRUNCATED;
        if (c < used)
            open_stream(&pl->streams[c], p->src, at, total);
        at += total;
    }
    return CODEC_OK;
}

static uint8_t to8(uint32_t v) { return (uint8_t)((v + 128u) / 257u); }

/* Photoshop blends a transparent composite with white, and other writers
   don't. Matting leaves v between white * (1 - a) and that plus a, give or
   take half a step of the stored depth, so a pixel outside that range means
   the file holds straight colour. */
static uint32_t unmatte(struct psd *p, uint32_t v, uint32_t a, uint32_t white)
{
    int64_t n = (int64_t)v * 65535 - (int64_t)(65535u - a) * white;
    int64_t slack = p->depth == 16 ? 32768 : 257 * 32768;
    double g, c;

    if (a == 0 || a == 65535u)
        return v;
    if (n < -slack || n > (int64_t)a * 65535 + slack)
        p->straight = 1;
    /* ImageMagick's arithmetic, so their results agree. */
    g = a * (1.0 / 65535.0);
    c = ((double)v - (1.0 - g) * white) / g;
    return c <= 0.0 ? 0u : c >= 65535.0 ? 65535u : (uint32_t)(c + 0.5);
}

static void pixel(struct psd *p, uint32_t *v, uint8_t *out)
{
    uint32_t a = p->alpha ? v[p->colours] : 65535u;
    /* Lab's a and b are centred on 128, or 32768 at 16 bits. */
    uint32_t centre = p->depth == 16 ? 32768u : 128u * 257u;
    double scale = p->depth == 16 ? 256.0 : 257.0;
    unsigned c;

    for (c = 0; p->matted && c < p->colours; c++)
        v[c] = unmatte(p, v[c], a, p->mode == LAB && c > 0 ? centre : 65535u);
    switch (p->colours == 3 && p->mode == MULTICHANNEL ? RGB : p->mode) {
    case RGB:
        out[0] = to8(v[0]);
        out[1] = to8(v[1]);
        out[2] = to8(v[2]);
        break;
    case CMYK:
        /* Stored inverted: 65535 is no ink. */
        for (c = 0; c < 3; c++)
            out[c] = to8((v[c] * v[3] + 32767u) / 65535u);
        break;
    case LAB:
        lab_to_srgb(v[0] * 100.0 / 65535.0, ((double)v[1] - centre) / scale,
                    ((double)v[2] - centre) / scale, out);
        break;
    case INDEXED:
        memcpy(out, p->palette + (v[0] >> 8) * 3u, 3);
        if ((long)(v[0] >> 8) == p->transparent)
            a = 0;
        break;
    default:
        out[0] = out[1] = out[2] = to8(v[0]);
        break;
    }
    out[3] = to8(a);
}

static enum codec_result read_row(const struct psd *p, struct planes *pl,
                                  int rle, unsigned c, unsigned y, size_t bytes)
{
    struct stream *s = &pl->streams[c];
    uint16_t *row = pl->rows + (size_t)c * p->width;
    unsigned x;

    if (rle) {
        enum codec_result r = unpack(s, pl->counts[(size_t)c * p->height + y],
                                     pl->raw, bytes);
        if (r != CODEC_OK)
            return r;
    } else if (!take(s, pl->raw, bytes)) {
        return CODEC_TRUNCATED;
    }
    for (x = 0; x < p->width; x++)
        if (p->depth == 1)
            row[x] = pl->raw[x >> 3] >> (7u - (x & 7u)) & 1u ? 0u : 65535u;
        else if (p->depth == 8)
            row[x] = (uint16_t)(pl->raw[x] * 257u);
        else
            row[x] = (uint16_t)be16(pl->raw + x * 2u);
    return CODEC_OK;
}

static enum codec_result composite(struct psd *p, uint64_t pos,
                                   struct psd_image *image)
{
    unsigned used = p->colours + (p->alpha ? 1u : 0u), c, x, y;
    size_t bytes = p->depth == 1 ? (p->width + 7u) / 8u
                                 : (size_t)p->width * (p->depth / 8u);
    struct planes pl = { NULL, NULL, NULL, NULL };
    enum codec_result r;
    uint8_t b[2];
    int rle;

    if (!get(p, pos, b, 2))
        return CODEC_TRUNCATED;
    if (be16(b) > 1)
        return CODEC_INVALID;       /* ZIP: no reader handles it */
    rle = be16(b) == 1;
    pl.streams = malloc((used + 1u) * sizeof *pl.streams);
    pl.raw = malloc(bytes);
    pl.rows = malloc((size_t)used * p->width * sizeof *pl.rows);
    image->rgba = malloc((size_t)p->width * p->height * 4u);
    r = CODEC_NO_MEMORY;
    if (pl.streams != NULL && pl.raw != NULL && pl.rows != NULL &&
        image->rgba != NULL)
        r = rle ? open_rle(p, &pl, pos + 2, used)
                : open_raw(p, &pl, pos + 2, used, bytes);
    for (y = 0; r == CODEC_OK && !p->straight && y < p->height; y++) {
        uint8_t *out = image->rgba + (size_t)y * p->width * 4u;
        for (c = 0; r == CODEC_OK && c < used; c++)
            r = read_row(p, &pl, rle, c, y, bytes);
        for (x = 0; r == CODEC_OK && x < p->width; x++) {
            uint32_t v[5];
            for (c = 0; c < used; c++)
                v[c] = pl.rows[(size_t)c * p->width + x];
            pixel(p, v, out + x * 4u);
        }
    }
    free(pl.streams);
    free(pl.counts);
    free(pl.raw);
    free(pl.rows);
    if (r != CODEC_OK)
        psd_free(image);
    return r;
}

enum codec_result psd_decode(const struct psd_source *source,
                             struct psd_image *image)
{
    struct psd *p = calloc(1, sizeof *p);
    uint64_t pos = 26;
    enum codec_result r;

    image->rgba = NULL;
    if (p == NULL)
        return CODEC_NO_MEMORY;
    p->src = source;
    p->transparent = -1;
    r = header(p);
    if (r == CODEC_OK)
        r = colour_mode(p, &pos);
    if (r == CODEC_OK)
        r = resources(p, &pos);
    if (r == CODEC_OK && p->fake)
        r = CODEC_INVALID;
    if (r == CODEC_OK)
        r = layers(p, &pos);
    if (r == CODEC_OK) {
        unsigned extra = p->channels - p->colours;
        p->alpha = extra > 0 && p->mode != BITMAP && p->mode != INDEXED &&
                   p->mode != MULTICHANNEL &&
                   (p->merged_alpha || extra > p->names);
        p->matted = p->alpha;
        image->width = p->width;
        image->height = p->height;
        r = composite(p, pos, image);
        if (r == CODEC_OK && p->straight) {
            psd_free(image);
            p->matted = p->straight = 0;
            r = composite(p, pos, image);
        }
    }
    free(p);
    return r;
}

void psd_free(struct psd_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
}
