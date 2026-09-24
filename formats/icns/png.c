#include <stdlib.h>
#include <string.h>

#include "common/zlib.h"
#include "png.h"

static const uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};

/* Adam7 pass origins and steps; a single pass covers non-interlaced images. */
static const unsigned pass_x[7] = {0, 4, 0, 2, 0, 1, 0};
static const unsigned pass_y[7] = {0, 0, 4, 0, 2, 0, 1};
static const unsigned pass_dx[7] = {8, 8, 4, 4, 2, 2, 1};
static const unsigned pass_dy[7] = {8, 8, 8, 4, 4, 2, 2};

struct header {
    unsigned width, height, depth, type, interlace, channels;
};

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static unsigned be16(const uint8_t *p)
{
    return (unsigned)p[0] << 8 | p[1];
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

int png_signature(const uint8_t *data, size_t length)
{
    return length >= 8 && memcmp(data, signature, 8) == 0;
}

static enum codec_result read_header(const uint8_t *data, size_t length, struct header *h)
{
    static const unsigned channels[7] = {1, 0, 3, 1, 2, 0, 4};
    unsigned depth;

    if (!png_signature(data, length))
        return CODEC_INVALID;
    if (length < 33)
        return CODEC_TRUNCATED;
    if (be32(data + 8) != 13 || memcmp(data + 12, "IHDR", 4) != 0)
        return CODEC_INVALID;
    h->width = (unsigned)be32(data + 16);
    h->height = (unsigned)be32(data + 20);
    h->depth = depth = data[24];
    h->type = data[25];
    h->interlace = data[28];
    if (h->width == 0 || h->height == 0 || data[26] != 0 || data[27] != 0 ||
        h->interlace > 1 || h->type > 6 || channels[h->type] == 0)
        return CODEC_INVALID;
    h->channels = channels[h->type];
    if (h->type == 0) {
        if (depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16)
            return CODEC_INVALID;
    } else if (h->type == 3) {
        if (depth != 1 && depth != 2 && depth != 4 && depth != 8)
            return CODEC_INVALID;
    } else if (depth != 8 && depth != 16) {
        return CODEC_INVALID;
    }
    if (be32(data + 16) > PNG_MAX_SIDE || be32(data + 20) > PNG_MAX_SIDE ||
        h->width > PNG_MAX_PIXELS / h->height)
        return CODEC_TOO_LARGE;
    return CODEC_OK;
}

enum codec_result png_info(const uint8_t *data, size_t length,
                           unsigned *width, unsigned *height)
{
    struct header h;
    enum codec_result result = read_header(data, length, &h);
    if (result == CODEC_OK) {
        *width = h.width;
        *height = h.height;
    }
    return result;
}

static size_t row_bytes(const struct header *h, unsigned width)
{
    return ((size_t)width * h->depth * h->channels + 7u) / 8u;
}

static unsigned pass_size(unsigned size, unsigned origin, unsigned step)
{
    return size > origin ? (size - origin + step - 1u) / step : 0;
}

static unsigned paeth(unsigned a, unsigned b, unsigned c)
{
    int p = (int)a + (int)b - (int)c;
    int pa = abs(p - (int)a), pb = abs(p - (int)b), pc = abs(p - (int)c);
    if (pa <= pb && pa <= pc)
        return a;
    return pb <= pc ? b : c;
}

/* Undo one row's filter in place. prior is NULL on a pass's first row. */
static int unfilter(unsigned filter, uint8_t *row, const uint8_t *prior,
                    size_t length, size_t bpp)
{
    size_t i;
    for (i = 0; i < length; i++) {
        unsigned a = i >= bpp ? row[i - bpp] : 0;
        unsigned b = prior ? prior[i] : 0;
        unsigned c = prior && i >= bpp ? prior[i - bpp] : 0;
        switch (filter) {
        case 0: break;
        case 1: row[i] = (uint8_t)(row[i] + a); break;
        case 2: row[i] = (uint8_t)(row[i] + b); break;
        case 3: row[i] = (uint8_t)(row[i] + ((a + b) >> 1)); break;
        case 4: row[i] = (uint8_t)(row[i] + paeth(a, b, c)); break;
        default: return 0;
        }
    }
    return 1;
}

static unsigned sample(const uint8_t *row, size_t index, unsigned depth)
{
    size_t bit;
    if (depth == 8)
        return row[index];
    if (depth == 16)
        return (unsigned)row[index * 2u] << 8 | row[index * 2u + 1u];
    bit = index * depth;
    return (row[bit >> 3] >> (8u - depth - (unsigned)(bit & 7u))) & ((1u << depth) - 1u);
}

static uint8_t to8(unsigned value, unsigned depth)
{
    if (depth == 16)
        return (uint8_t)((value * 255u + 32767u) / 65535u);
    if (depth < 8)
        return (uint8_t)(value * 255u / ((1u << depth) - 1u));
    return (uint8_t)value;
}

struct colours {
    const uint8_t *palette, *trns;
    size_t palette_count, trns_length;
};

static void put_pixel(const struct header *h, const struct colours *c,
                      const uint8_t *row, unsigned x, uint8_t *out)
{
    unsigned d = h->depth, v, r, g, b;
    size_t s = (size_t)x * h->channels;

    switch (h->type) {
    case 0:
        v = sample(row, s, d);
        out[0] = out[1] = out[2] = to8(v, d);
        out[3] = c->trns_length == 2 && v == be16(c->trns) ? 0 : 255;
        break;
    case 2:
        r = sample(row, s, d);
        g = sample(row, s + 1u, d);
        b = sample(row, s + 2u, d);
        out[0] = to8(r, d);
        out[1] = to8(g, d);
        out[2] = to8(b, d);
        out[3] = c->trns_length == 6 &&
                 r == be16(c->trns) && g == be16(c->trns + 2) &&
                 b == be16(c->trns + 4) ? 0 : 255;
        break;
    case 3:
        v = sample(row, s, d);
        if (v < c->palette_count) {
            memcpy(out, c->palette + v * 3u, 3);
            out[3] = v < c->trns_length ? c->trns[v] : 255;
        } else {
            out[0] = out[1] = out[2] = 0;
            out[3] = 255;
        }
        break;
    case 4:
        out[0] = out[1] = out[2] = to8(sample(row, s, d), d);
        out[3] = to8(sample(row, s + 1u, d), d);
        break;
    default:
        out[0] = to8(sample(row, s, d), d);
        out[1] = to8(sample(row, s + 1u, d), d);
        out[2] = to8(sample(row, s + 2u, d), d);
        out[3] = to8(sample(row, s + 3u, d), d);
        break;
    }
}

enum codec_result png_decode(const uint8_t *data, size_t length,
                             unsigned *width, unsigned *height, uint8_t **rgba)
{
    struct header h;
    struct colours c = {NULL, NULL, 0, 0};
    size_t pos = 8, idat_total = 0, expected = 0, written, bpp, i;
    uint8_t *idat, *raw, *out, *p;
    int ended = 0;
    unsigned pass, passes, x, y;
    enum codec_result result;

    *rgba = NULL;
    result = read_header(data, length, &h);
    if (result != CODEC_OK)
        return result;

    /* Walk the chunks: note the palette and transparency, and size IDAT. */
    while (pos < length) {
        const uint8_t *body;
        uint32_t size;
        if (length - pos < 12)
            return CODEC_TRUNCATED;
        size = be32(data + pos);
        if (size > 0x7fffffffu)
            return CODEC_INVALID;
        if (size > length - pos - 12)
            return CODEC_TRUNCATED;
        body = data + pos + 8;
        if (memcmp(data + pos + 4, "IEND", 4) == 0) {
            ended = 1;
            break;
        }
        if (memcmp(data + pos + 4, "IDAT", 4) == 0) {
            idat_total += size;
        } else if (memcmp(data + pos + 4, "PLTE", 4) == 0 && c.palette == NULL) {
            if (size >= 3 && size <= 768 && size % 3 == 0) {
                c.palette = body;
                c.palette_count = size / 3u;
            } else if (h.type == 3) {
                return CODEC_INVALID;
            }
        } else if (memcmp(data + pos + 4, "tRNS", 4) == 0 && c.trns == NULL) {
            /* Keep only well-formed transparency; otherwise ignore it. */
            if ((h.type == 3 && size <= 256) ||
                (h.type == 0 && size == 2) || (h.type == 2 && size == 6)) {
                c.trns = body;
                c.trns_length = size;
            }
        }
        pos += 12u + size;
    }
    if (h.type == 3 && c.palette == NULL)
        return CODEC_INVALID;
    if (idat_total == 0)
        return ended ? CODEC_INVALID : CODEC_TRUNCATED;

    passes = h.interlace ? 7u : 1u;
    for (pass = 0; pass < passes; pass++) {
        unsigned pw = h.interlace ? pass_size(h.width, pass_x[pass], pass_dx[pass]) : h.width;
        unsigned ph = h.interlace ? pass_size(h.height, pass_y[pass], pass_dy[pass]) : h.height;
        if (pw != 0 && ph != 0)
            expected += (size_t)ph * (1u + row_bytes(&h, pw));
    }

    idat = malloc(idat_total);
    raw = malloc(expected);
    out = malloc((size_t)h.width * h.height * 4u);
    if (idat == NULL || raw == NULL || out == NULL) {
        free(idat);
        free(raw);
        free(out);
        return CODEC_NO_MEMORY;
    }
    for (pos = 8, p = idat; pos < length; pos += 12u + be32(data + pos)) {
        if (memcmp(data + pos + 4, "IEND", 4) == 0)
            break;
        if (memcmp(data + pos + 4, "IDAT", 4) == 0) {
            memcpy(p, data + pos + 8, be32(data + pos));
            p += be32(data + pos);
        }
    }
    result = zlib_inflate(idat, idat_total, raw, expected, &written);
    free(idat);
    /* Extra data after the image is ignored, as libpng does. */
    if (result == CODEC_TOO_LARGE || (result == CODEC_OK && written < expected))
        result = written == expected ? CODEC_OK : CODEC_INVALID;
    if (result != CODEC_OK) {
        free(raw);
        free(out);
        return result;
    }

    bpp = (h.depth * h.channels + 7u) / 8u;
    for (pass = 0, p = raw; pass < passes; pass++) {
        unsigned x0 = h.interlace ? pass_x[pass] : 0, dx = h.interlace ? pass_dx[pass] : 1;
        unsigned y0 = h.interlace ? pass_y[pass] : 0, dy = h.interlace ? pass_dy[pass] : 1;
        unsigned pw = pass_size(h.width, x0, dx), ph = pass_size(h.height, y0, dy);
        size_t rb;
        const uint8_t *prior = NULL;
        if (pw == 0 || ph == 0)
            continue;
        rb = row_bytes(&h, pw);
        for (y = 0; y < ph; y++, p += 1u + rb) {
            if (!unfilter(p[0], p + 1, prior, rb, bpp)) {
                free(raw);
                free(out);
                return CODEC_INVALID;
            }
            prior = p + 1;
            for (x = 0; x < pw; x++) {
                i = ((size_t)(y0 + y * dy) * h.width + x0 + x * dx) * 4u;
                put_pixel(&h, &c, p + 1, x, out + i);
            }
        }
    }
    free(raw);
    *width = h.width;
    *height = h.height;
    *rgba = out;
    return CODEC_OK;
}

static uint32_t crc_update(uint32_t crc, const uint8_t *data, size_t length)
{
    static const uint32_t nibble[16] = {
        0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
        0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
        0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
        0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c
    };
    size_t i;
    for (i = 0; i < length; i++) {
        crc ^= data[i];
        crc = (crc >> 4) ^ nibble[crc & 15u];
        crc = (crc >> 4) ^ nibble[crc & 15u];
    }
    return crc;
}

/* Write a chunk whose body is already at out + 8; returns its full size. */
static size_t finish_chunk(uint8_t *out, const char *type, size_t size)
{
    put32(out, (uint32_t)size);
    memcpy(out + 4, type, 4);
    put32(out + 8 + size, ~crc_update(0xffffffffu, out + 4, size + 4u));
    return size + 12u;
}

static unsigned filtered(unsigned filter, const uint8_t *row, const uint8_t *prior,
                         size_t i, size_t bpp)
{
    unsigned a = i >= bpp ? row[i - bpp] : 0;
    unsigned b = prior ? prior[i] : 0;
    unsigned c = prior && i >= bpp ? prior[i - bpp] : 0;
    switch (filter) {
    case 1: return (uint8_t)(row[i] - a);
    case 2: return (uint8_t)(row[i] - b);
    case 3: return (uint8_t)(row[i] - ((a + b) >> 1));
    case 4: return (uint8_t)(row[i] - paeth(a, b, c));
    default: return row[i];
    }
}

enum codec_result png_encode(const uint8_t *rgba, unsigned width, unsigned height,
                             uint8_t **out, size_t *out_length)
{
    size_t pixels, rb, raw_size, bound, i, ihdr, z = 0;
    unsigned channels = 3, y, f, best;
    uint8_t *packed, *raw, *png;
    enum codec_result result;

    *out = NULL;
    *out_length = 0;
    if (width == 0 || height == 0 || width > PNG_MAX_SIDE || height > PNG_MAX_SIDE ||
        width > PNG_MAX_PIXELS / height)
        return CODEC_TOO_LARGE;
    pixels = (size_t)width * height;
    for (i = 0; i < pixels; i++)
        if (rgba[i * 4u + 3u] != 255) {
            channels = 4;
            break;
        }
    rb = (size_t)width * channels;
    raw_size = (rb + 1u) * height;
    bound = zlib_deflate_bound(raw_size);
    packed = malloc(rb * height);
    raw = malloc(raw_size);
    png = bound ? malloc(8u + 25u + 12u + bound + 12u) : NULL;
    if (packed == NULL || raw == NULL || png == NULL) {
        free(packed);
        free(raw);
        free(png);
        return CODEC_NO_MEMORY;
    }
    for (i = 0; i < pixels; i++)
        memcpy(packed + i * channels, rgba + i * 4u, channels);

    /* Pick each row's filter by the smallest sum of signed differences. */
    for (y = 0; y < height; y++) {
        const uint8_t *row = packed + y * rb, *prior = y ? row - rb : NULL;
        uint8_t *dst = raw + y * (rb + 1u);
        unsigned long cost, best_cost = (unsigned long)-1;
        best = 0;
        for (f = 0; f < 5; f++) {
            for (cost = 0, i = 0; i < rb; i++) {
                unsigned v = filtered(f, row, prior, i, channels);
                cost += v < 128 ? v : 256u - v;
            }
            if (cost < best_cost) {
                best_cost = cost;
                best = f;
            }
        }
        dst[0] = (uint8_t)best;
        for (i = 0; i < rb; i++)
            dst[1 + i] = (uint8_t)filtered(best, row, prior, i, channels);
    }
    free(packed);

    memcpy(png, signature, 8);
    put32(png + 16, width);
    put32(png + 20, height);
    png[24] = 8;
    png[25] = channels == 4 ? 6 : 2;
    png[26] = png[27] = png[28] = 0;
    ihdr = finish_chunk(png + 8, "IHDR", 13);
    result = zlib_deflate(raw, raw_size, png + 8 + ihdr + 8, bound, 9, &z);
    free(raw);
    if (result != CODEC_OK || z > 0x7fffffffu) {
        free(png);
        return result == CODEC_OK ? CODEC_TOO_LARGE : result;
    }
    z = 8u + ihdr + finish_chunk(png + 8 + ihdr, "IDAT", z);
    z += finish_chunk(png + z, "IEND", 0);
    *out = png;
    *out_length = z;
    return CODEC_OK;
}
