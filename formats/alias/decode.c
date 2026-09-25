#include "decode.h"
#include "rla.h"
#include <stdlib.h>
#include <string.h>

#define ALIAS_MAX_PIXELS (16u * 1024u * 1024u)
#define ALIAS_MAX_SIDE 65535u
#define PIX_HEADER 10u

/* One RLA channel group: colour or matte. */
struct group {
    unsigned channels;
    unsigned bytes;         /* 1, or 2 for a high then a low byte span */
    unsigned long max;      /* largest sample value */
};

struct rla {
    size_t width, height;
    size_t table;           /* offset of the scanline offset table */
    size_t next;            /* offset of the next header, 0 if none */
    struct group colour, matte;
};

static unsigned be16(const uint8_t *p)
{
    return ((unsigned)p[0] << 8) | p[1];
}

static long sbe16(const uint8_t *p)
{
    unsigned v = be16(p);
    return v >= 0x8000u ? (long)v - 0x10000L : (long)v;
}

static unsigned long be32(const uint8_t *p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) |
           ((unsigned long)p[2] << 8) | p[3];
}

static enum codec_result check_size(size_t width, size_t height)
{
    if (width == 0 || height == 0)
        return CODEC_INVALID;
    if (width > ALIAS_MAX_SIDE || height > ALIAS_MAX_SIDE ||
        width * height > ALIAS_MAX_PIXELS)
        return CODEC_TOO_LARGE;
    return CODEC_OK;
}

static enum codec_result alloc_image(struct alias_image *image,
                                     size_t width, size_t height)
{
    image->width = (unsigned)width;
    image->height = (unsigned)height;
    image->rgba = malloc(width * height * 4u);
    return image->rgba == NULL ? CODEC_NO_MEMORY : CODEC_OK;
}

/* Storage type and bit count to a group. Byte storage holding more than 8
   bits is 16-bit, as some writers label it; 32-bit and float are out. */
static int parse_group(unsigned type, long bits, unsigned channels,
                       struct group *g)
{
    if (bits == 0)
        bits = 8;
    if (bits < 0 || bits > 16 || (type != RLA_BYTE && type != RLA_WORD))
        return 0;
    g->channels = channels;
    g->bytes = (type == RLA_WORD || bits > 8) ? 2u : 1u;
    g->max = (1ul << bits) - 1u;
    return 1;
}

static int rla_revision(unsigned revision)
{
    return revision == 0xfffeu || revision == 0xfffdu || revision == 0;
}

static enum codec_result parse_rla(const uint8_t *data, size_t length,
                                   size_t base, struct rla *r)
{
    const uint8_t *p = data + base;
    long left, right, bottom, top, colours, mattes;
    enum codec_result result;

    if (length - base < RLA_HEADER_SIZE)
        return CODEC_TRUNCATED;
    if (!rla_revision(be16(p + 26)))
        return CODEC_INVALID;
    left = sbe16(p + 8);
    right = sbe16(p + 10);
    bottom = sbe16(p + 12);
    top = sbe16(p + 14);
    if (right < left || top < bottom)
        return CODEC_INVALID;
    r->width = (size_t)(right - left + 1);
    r->height = (size_t)(top - bottom + 1);
    result = check_size(r->width, r->height);
    if (result != CODEC_OK)
        return result;
    colours = sbe16(p + 20);
    mattes = sbe16(p + 22);
    if ((colours != 1 && colours != 3) || mattes < 0 || mattes > 3 ||
        !parse_group(be16(p + 18), sbe16(p + 658), (unsigned)colours,
                     &r->colour))
        return CODEC_INVALID;
    /* Only the first matte channel is alpha; the rest are skipped. */
    r->matte.channels = 0;
    if (mattes > 0 &&
        !parse_group(be16(p + 660), sbe16(p + 662), 1u, &r->matte))
        return CODEC_INVALID;
    r->table = base + RLA_HEADER_SIZE;
    if ((length - r->table) / 4u < r->height)
        return CODEC_TRUNCATED;
    r->next = (size_t)be32(p + 736);
    return CODEC_OK;
}

/* Fill count bytes of out, stride apart, from the RLE data at *pos, which
   must end before end. A run past the last value is clamped. */
static enum codec_result unpack_span(const uint8_t *data, size_t *pos,
                                     size_t end, uint8_t *out, size_t count,
                                     size_t stride)
{
    size_t i = *pos, n = 0, run, k;
    int c;

    while (n < count) {
        if (i >= end)
            return CODEC_INVALID;
        c = (int)(int8_t)data[i++];
        if (c >= 0) {
            if (i >= end)
                return CODEC_INVALID;
            run = (size_t)c + 1u;
            if (run > count - n)
                run = count - n;
            for (k = 0; k < run; k++, n++)
                out[n * stride] = data[i];
            i++;
        } else {
            run = (size_t)-c;
            if (run > end - i)
                return CODEC_INVALID;
            for (k = 0; k < run; k++, i++)
                if (n < count)
                    out[n++ * stride] = data[i];
        }
    }
    *pos = i;
    return CODEC_OK;
}

static uint8_t scale(unsigned long v, unsigned long max)
{
    if (max == 255u)
        return (uint8_t)v;
    if (v >= max)
        return 255u;
    return (uint8_t)((v * 255u + max / 2u) / max);
}

/* Decode the next channel record at *pos into bytes 0..3 of each RGBA pixel
   of row, starting at offset. work holds two bytes per pixel. */
static enum codec_result read_channel(const uint8_t *data, size_t length,
                                      size_t *pos, const struct group *g,
                                      size_t width, uint8_t *work,
                                      uint8_t *row, unsigned offset)
{
    size_t record, end, x;
    enum codec_result result;
    unsigned b;

    if (length - *pos < 2u)
        return CODEC_TRUNCATED;
    record = be16(data + *pos);
    *pos += 2u;
    if (length - *pos < record)
        return CODEC_TRUNCATED;
    end = *pos + record;
    for (b = 0; b < g->bytes; b++) {
        result = unpack_span(data, pos, end, work + b, width, g->bytes);
        if (result != CODEC_OK)
            return result;
    }
    *pos = end;
    for (x = 0; x < width; x++) {
        unsigned long v = work[x * g->bytes];
        if (g->bytes == 2u)
            v = (v << 8) | work[x * 2u + 1u];
        row[x * 4u + offset] = scale(v, g->max);
    }
    return CODEC_OK;
}

/* Colour stored multiplied by the matte, as renderers write RLA. */
static void unpremultiply(uint8_t *px)
{
    unsigned a = px[3], c, v;

    if (a == 0 || a == 255u)
        return;
    for (c = 0; c < 3u; c++) {
        v = ((unsigned)px[c] * 255u + a / 2u) / a;
        px[c] = (uint8_t)(v > 255u ? 255u : v);
    }
}

static enum codec_result decode_rla(const uint8_t *data, size_t length,
                                    const struct rla *r,
                                    struct alias_image *image)
{
    enum codec_result result;
    uint8_t *work, *row;
    size_t y, x, pos;
    unsigned c;

    result = alloc_image(image, r->width, r->height);
    if (result != CODEC_OK)
        return result;
    work = malloc(r->width * 2u);
    if (work == NULL) {
        alias_free(image);
        return CODEC_NO_MEMORY;
    }
    /* Scanline 0 is the bottom row. */
    for (y = 0; y < r->height && result == CODEC_OK; y++) {
        row = image->rgba + (r->height - 1u - y) * r->width * 4u;
        pos = (size_t)be32(data + r->table + y * 4u);
        if (pos > length) {
            result = CODEC_TRUNCATED;
            break;
        }
        for (c = 0; c < r->colour.channels && result == CODEC_OK; c++)
            result = read_channel(data, length, &pos, &r->colour, r->width,
                                  work, row, c);
        if (result != CODEC_OK)
            break;
        if (r->colour.channels == 1u)
            for (x = 0; x < r->width; x++)
                row[x * 4u + 1u] = row[x * 4u + 2u] = row[x * 4u];
        if (r->matte.channels == 0) {
            for (x = 0; x < r->width; x++)
                row[x * 4u + 3u] = 255u;
        } else {
            result = read_channel(data, length, &pos, &r->matte, r->width,
                                  work, row, 3u);
            for (x = 0; x < r->width && result == CODEC_OK; x++)
                unpremultiply(row + x * 4u);
        }
    }
    free(work);
    if (result != CODEC_OK)
        alias_free(image);
    return result;
}

/* Move r to the next header in the chain, if there is one that parses.
   Each header must follow the last, so the chain ends. */
static int next_rla(const uint8_t *data, size_t length, size_t *base,
                    struct rla *r)
{
    if (r->next < *base + RLA_HEADER_SIZE || r->next >= length)
        return 0;
    *base = r->next;
    return parse_rla(data, length, *base, r) == CODEC_OK;
}

static enum codec_result count_rla(const uint8_t *data, size_t length,
                                   unsigned *count)
{
    struct rla r;
    size_t base = 0;
    enum codec_result result = parse_rla(data, length, 0, &r);

    if (result != CODEC_OK)
        return result;
    *count = 1;
    while (next_rla(data, length, &base, &r))
        ++*count;
    return CODEC_OK;
}

static enum codec_result find_rla(const uint8_t *data, size_t length,
                                  unsigned index, struct rla *r)
{
    size_t base = 0;
    enum codec_result result = parse_rla(data, length, 0, r);

    if (result != CODEC_OK)
        return result;
    for (; index > 0; index--)
        if (!next_rla(data, length, &base, r))
            return CODEC_INVALID;
    return CODEC_OK;
}

static int pix_header(const uint8_t *data, size_t length)
{
    unsigned bits;

    if (length < PIX_HEADER)
        return 0;
    bits = be16(data + 8);
    return be16(data) != 0 && be16(data + 2) != 0 && (bits == 8 || bits == 24);
}

enum kind { KIND_NONE, KIND_RLA, KIND_PIX };

/* RLA marks its revision; PIX has no magic, so an RLA from a writer that
   leaves the revision zero is tried only when the PIX header doesn't fit. */
static enum kind detect(const uint8_t *data, size_t length)
{
    unsigned revision;

    if (length >= 28u) {
        revision = be16(data + 26);
        if (revision == 0xfffeu || revision == 0xfffdu)
            return KIND_RLA;
    }
    if (pix_header(data, length))
        return KIND_PIX;
    if (length >= 28u && be16(data + 26) == 0)
        return KIND_RLA;
    return KIND_NONE;
}

/* Runs of (count, blue, green, red) or (count, value). A run may carry on
   into the next row, as ImageMagick reads it; one past the last pixel is
   clamped, and zero counts are skipped. */
static enum codec_result decode_pix(const uint8_t *data, size_t length,
                                    struct alias_image *image)
{
    size_t width = be16(data), height = be16(data + 2);
    size_t record = be16(data + 8) == 24u ? 4u : 2u;
    size_t pos = PIX_HEADER, n = 0, total, run, k;
    enum codec_result result = check_size(width, height);
    uint8_t *px;

    if (result != CODEC_OK)
        return result;
    result = alloc_image(image, width, height);
    if (result != CODEC_OK)
        return result;
    total = width * height;
    px = image->rgba;
    while (n < total) {
        if (length - pos < record) {
            alias_free(image);
            return CODEC_TRUNCATED;
        }
        run = data[pos];
        if (run > total - n)
            run = total - n;
        for (k = 0; k < run; k++, n++, px += 4) {
            if (record == 4u) {
                px[0] = data[pos + 3u];
                px[1] = data[pos + 2u];
                px[2] = data[pos + 1u];
            } else {
                px[0] = px[1] = px[2] = data[pos + 1u];
            }
            px[3] = 255u;
        }
        pos += record;
    }
    return CODEC_OK;
}

enum codec_result alias_count(const uint8_t *data, size_t length,
                              unsigned *count)
{
    switch (detect(data, length)) {
    case KIND_PIX:
        *count = 1;
        return CODEC_OK;
    case KIND_RLA:
        return count_rla(data, length, count);
    default:
        return length < PIX_HEADER ? CODEC_TRUNCATED : CODEC_INVALID;
    }
}

enum codec_result alias_decode(const uint8_t *data, size_t length,
                               unsigned index, struct alias_image *image)
{
    struct rla r;
    enum codec_result result;

    image->rgba = NULL;
    switch (detect(data, length)) {
    case KIND_PIX:
        return index == 0 ? decode_pix(data, length, image) : CODEC_INVALID;
    case KIND_RLA:
        result = find_rla(data, length, index, &r);
        if (result != CODEC_OK)
            return result;
        return decode_rla(data, length, &r, image);
    default:
        return length < PIX_HEADER ? CODEC_TRUNCATED : CODEC_INVALID;
    }
}

void alias_free(struct alias_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
}
