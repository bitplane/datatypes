#include "decode.h"
#include <stdlib.h>
#include <string.h>

#define SUNICON_MAX_PIXELS (16u * 1024u * 1024u)
/* Larger header values are reported as this, which no field accepts. */
#define SUNICON_HUGE 0x1000000ul
#define UNSET (~0ul)

enum field { F_VERSION, F_WIDTH, F_HEIGHT, F_DEPTH, F_BITS, F_COUNT };

static const char *const field_names[F_COUNT] = {
    "Format_version", "Width", "Height", "Depth", "Valid_bits_per_item"
};

static int is_ident(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static int is_space(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static int hex_value(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* The first "*" "/" at or after p, or NULL. */
static const uint8_t *comment_end(const uint8_t *p, const uint8_t *end)
{
    for (; end - p >= 2; p++)
        if (p[0] == '*' && p[1] == '/')
            return p;
    return NULL;
}

/* Record each Name=value in the comment text; the first of each name wins,
   as in XView. A name without a number is ignored, as XView and netpbm do. */
static void read_fields(const uint8_t *p, const uint8_t *end, unsigned long values[F_COUNT])
{
    while (p < end) {
        const uint8_t *name = p;
        unsigned long value = 0;
        size_t length;
        int f;

        /* Whole identifiers are consumed, so p is at the start of one. */
        if (!is_ident(*p)) {
            p++;
            continue;
        }
        while (p < end && is_ident(*p))
            p++;
        length = (size_t)(p - name);
        while (p < end && (*p == ' ' || *p == '\t'))
            p++;
        if (p == end || *p != '=')
            continue;
        p++;
        while (p < end && is_space(*p))
            p++;
        if (p == end || *p < '0' || *p > '9')
            continue;
        while (p < end && *p >= '0' && *p <= '9') {
            value = value * 10u + (unsigned long)(*p - '0');
            if (value > SUNICON_HUGE)
                value = SUNICON_HUGE;
            p++;
        }
        for (f = 0; f < F_COUNT; f++)
            if (strlen(field_names[f]) == length &&
                memcmp(field_names[f], name, length) == 0 && values[f] == UNSET)
                values[f] = value;
    }
}

/* Find the comment holding Format_version, which may follow other comments
   or text (SCCS and RCS ids), and return the data that follows it. */
static enum codec_result read_header(const uint8_t **pp, const uint8_t *end,
                                     unsigned long values[F_COUNT])
{
    const uint8_t *p = *pp;

    for (;;) {
        const uint8_t *close;
        int f;

        while (end - p >= 2 && !(p[0] == '/' && p[1] == '*'))
            p++;
        if (end - p < 2)
            return CODEC_INVALID;
        p += 2;
        close = comment_end(p, end);
        for (f = 0; f < F_COUNT; f++)
            values[f] = UNSET;
        read_fields(p, close != NULL ? close : end, values);
        if (values[F_VERSION] != UNSET) {
            if (close == NULL)
                return CODEC_TRUNCATED;
            *pp = close + 2;
            return CODEC_OK;
        }
        if (close == NULL)
            return CODEC_INVALID;
        p = close + 2;
    }
}

/* Skip commas, white space and comments between items. */
static enum codec_result skip_separators(const uint8_t **pp, const uint8_t *end)
{
    const uint8_t *p = *pp;

    while (p < end) {
        if (*p == ',' || is_space(*p)) {
            p++;
        } else if (*p == '/' && end - p >= 2 && p[1] == '*') {
            p = comment_end(p + 2, end);
            if (p == NULL)
                return CODEC_TRUNCATED;
            p += 2;
        } else {
            break;
        }
    }
    *pp = p;
    return CODEC_OK;
}

/* One "0x" hex item no wider than bits. */
static enum codec_result read_item(const uint8_t **pp, const uint8_t *end,
                                   unsigned bits, unsigned long *value)
{
    const uint8_t *p = *pp;
    unsigned long max = bits == 32 ? 0xfffffffful : (1ul << bits) - 1u;
    int digits = 0, d, wide = 0;
    enum codec_result result = skip_separators(&p, end);

    if (result != CODEC_OK)
        return result;
    if (p == end)
        return CODEC_TRUNCATED;
    if (*p != '0')
        return CODEC_INVALID;
    if (++p == end)
        return CODEC_TRUNCATED;
    if (*p != 'x' && *p != 'X')
        return CODEC_INVALID;
    p++;
    *value = 0;
    while (p < end && (d = hex_value(*p)) >= 0) {
        if (*value > (max >> 4))
            wide = 1;
        *value = ((*value << 4) | (unsigned long)d) & 0xfffffffful;
        digits++;
        p++;
    }
    if (digits == 0)
        return p == end ? CODEC_TRUNCATED : CODEC_INVALID;
    if (wide || *value > max || (p < end && is_ident(*p)))
        return CODEC_INVALID;
    *pp = p;
    return CODEC_OK;
}

void sunicon_free(struct sunicon_image *image)
{
    free(image->pixels);
    image->pixels = NULL;
    image->width = image->height = image->depth = 0;
}

enum codec_result sunicon_decode(const uint8_t *data, size_t length,
                                 struct sunicon_image *image)
{
    unsigned long values[F_COUNT], width, height, depth, bits;
    const uint8_t *p = data, *end;
    size_t per_row, items, i;
    unsigned samples, mask;
    enum codec_result result;

    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = image->depth = 0;
    image->pixels = NULL;
    if (data == NULL)
        return CODEC_TRUNCATED;
    end = data + length;
    result = read_header(&p, end, values);
    if (result != CODEC_OK)
        return result;

    /* XView's defaults for absent fields. */
    width = values[F_WIDTH] != UNSET ? values[F_WIDTH] : 64u;
    height = values[F_HEIGHT] != UNSET ? values[F_HEIGHT] : 64u;
    depth = values[F_DEPTH] != UNSET ? values[F_DEPTH] : 1u;
    bits = values[F_BITS] != UNSET ? values[F_BITS] : 16u;
    if (values[F_VERSION] != 1u || (depth != 1u && depth != 8u) ||
        (bits != 8u && bits != 16u && bits != 32u) || width == 0 || height == 0)
        return CODEC_INVALID;
    if (width > 65535u || height > 65535u ||
        (size_t)width * height > SUNICON_MAX_PIXELS)
        return CODEC_TOO_LARGE;
    image->pixels = malloc((size_t)width * height);
    if (image->pixels == NULL)
        return CODEC_NO_MEMORY;
    image->width = (unsigned)width;
    image->height = (unsigned)height;
    image->depth = (unsigned)depth;

    /* Each row is padded to a whole item, as mpr_static lays it out. */
    samples = (unsigned)(bits / depth);
    mask = (1u << depth) - 1u;
    per_row = ((size_t)width * depth + bits - 1u) / bits;
    items = per_row * height;
    for (i = 0; i < items; i++) {
        uint8_t *row = image->pixels + (i / per_row) * width;
        size_t x = (i % per_row) * samples;
        unsigned long value;
        unsigned s;

        result = read_item(&p, end, (unsigned)bits, &value);
        if (result != CODEC_OK) {
            sunicon_free(image);
            return result;
        }
        for (s = 0; s < samples && x + s < width; s++)
            row[x + s] = (uint8_t)((value >> (bits - depth * (s + 1u))) & mask);
    }
    return CODEC_OK;
}
