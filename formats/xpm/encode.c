#include "encode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Printable characters that need no escaping in a C string. '?' is left out
   so no pair can form a trigraph, and '/' so no pair looks like a comment. */
static const char code_chars[] =
    " .#abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
    "!$%&'()*+,-:;<=>@[]^_`{|}~";
#define CODE_BASE (sizeof code_chars - 1u)

static int is_ident(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

void xpm_make_name(const char *filename, char name[XPM_MAX_NAME + 1])
{
    const char *start = filename ? filename : "", *end, *p;
    size_t n = 0;

    for (p = start; *p != '\0'; p++)
        if (*p == '/' || *p == ':')
            start = p + 1;
    end = strrchr(start, '.');
    if (end == NULL || end == start)
        end = start + strlen(start);
    if (start < end && *start >= '0' && *start <= '9')
        name[n++] = '_';
    for (p = start; p < end && n < XPM_MAX_NAME; p++)
        name[n++] = is_ident((unsigned char)*p) ? *p : '_';
    name[n] = '\0';
    if (n == 0)
        strcpy(name, "image");
}

void xpm_encoder_init(struct xpm_encoder *encoder)
{
    memset(encoder, 0, sizeof *encoder);
}

void xpm_encoder_free(struct xpm_encoder *encoder)
{
    free(encoder->colors);
    free(encoder->slots);
    xpm_encoder_init(encoder);
}

/* What a pixel is written as: XPM_NONE, or 0xRRGGBB composited over white. */
static uint32_t pixel_color(const uint8_t *p)
{
    uint32_t color = 0;
    unsigned a = p[3], c;
    if (a < 128)
        return XPM_NONE;
    for (c = 0; c < 3; c++)
        color = color << 8 | (p[c] * a + 255u * (255u - a) + 127u) / 255u;
    return color;
}

static size_t hash_color(uint32_t color)
{
    return (size_t)(color * 2654435761u);
}

static int32_t *find_slot(const struct xpm_encoder *e, uint32_t color)
{
    size_t i = hash_color(color) & e->mask;
    for (;;) {
        int32_t *slot = &e->slots[i];
        if (*slot < 0 || e->colors[*slot] == color)
            return slot;
        i = (i + 1) & e->mask;
    }
}

/* Double the table, keeping at most half the slots in use. */
static enum codec_result grow(struct xpm_encoder *e)
{
    size_t capacity = e->capacity ? e->capacity * 2u : 256u, i;
    uint32_t *colors = realloc(e->colors, capacity * sizeof *colors);
    int32_t *slots;

    if (colors == NULL)
        return CODEC_NO_MEMORY;
    e->colors = colors;
    slots = malloc(capacity * 2u * sizeof *slots);
    if (slots == NULL)
        return CODEC_NO_MEMORY;
    free(e->slots);
    e->slots = slots;
    e->capacity = capacity;
    e->mask = capacity * 2u - 1u;
    memset(slots, 0xff, capacity * 2u * sizeof *slots);
    for (i = 0; i < e->count; i++)
        *find_slot(e, e->colors[i]) = (int32_t)i;
    return CODEC_OK;
}

enum codec_result xpm_encoder_add_row(struct xpm_encoder *encoder,
                                      const uint8_t *rgba, unsigned width)
{
    unsigned x;
    for (x = 0; x < width; x++, rgba += 4) {
        uint32_t color = pixel_color(rgba);
        int32_t *slot;
        if (encoder->count == encoder->capacity) {
            enum codec_result result = grow(encoder);
            if (result != CODEC_OK)
                return result;
        }
        slot = find_slot(encoder, color);
        if (*slot < 0) {
            encoder->colors[encoder->count] = color;
            *slot = (int32_t)encoder->count++;
        }
    }
    return CODEC_OK;
}

void xpm_encoder_finish(struct xpm_encoder *encoder, unsigned height)
{
    size_t codes = CODE_BASE;
    encoder->cpp = 1;
    while (codes < encoder->count) {
        codes *= CODE_BASE;
        encoder->cpp++;
    }
    encoder->rows_left = height;
}

size_t xpm_make_header(const struct xpm_encoder *encoder, const char *name,
                       unsigned width, unsigned height, long hot_x, long hot_y,
                       char *output, size_t capacity)
{
    int n;

    if (width == 0 || height == 0 || width > 65535u || height > 65535u ||
        encoder->count == 0 || strlen(name) > XPM_MAX_NAME)
        return 0;
    if (hot_x >= 0 && hot_y >= 0)
        n = snprintf(output, capacity,
                     "/* XPM */\nstatic char *%s[] = {\n\"%u %u %lu %u %ld %ld\",\n",
                     name, width, height, (unsigned long)encoder->count,
                     encoder->cpp, hot_x, hot_y);
    else
        n = snprintf(output, capacity,
                     "/* XPM */\nstatic char *%s[] = {\n\"%u %u %lu %u\",\n",
                     name, width, height, (unsigned long)encoder->count,
                     encoder->cpp);
    return n > 0 && (size_t)n < capacity ? (size_t)n : 0;
}

static void put_code(const struct xpm_encoder *encoder, size_t index, char *output)
{
    unsigned i;
    for (i = 0; i < encoder->cpp; i++) {
        output[i] = code_chars[index % CODE_BASE];
        index /= CODE_BASE;
    }
}

size_t xpm_color_line(const struct xpm_encoder *encoder, size_t index,
                      char *output, size_t capacity)
{
    uint32_t color;
    int n;

    if (index >= encoder->count || capacity < encoder->cpp + 2u)
        return 0;
    color = encoder->colors[index];
    output[0] = '"';
    put_code(encoder, index, output + 1);
    if (color == XPM_NONE)
        n = snprintf(output + 1 + encoder->cpp, capacity - 1u - encoder->cpp,
                     " c None\",\n");
    else
        n = snprintf(output + 1 + encoder->cpp, capacity - 1u - encoder->cpp,
                     " c #%06lX\",\n", (unsigned long)color);
    return n > 0 && (size_t)n < capacity - 1u - encoder->cpp
           ? (size_t)n + 1u + encoder->cpp : 0;
}

size_t xpm_row_capacity(const struct xpm_encoder *encoder, unsigned width)
{
    return (size_t)width * encoder->cpp + 8u;
}

size_t xpm_encode_row(struct xpm_encoder *encoder, const uint8_t *rgba,
                      unsigned width, char *output, size_t capacity)
{
    size_t pos = 0;
    unsigned x;

    if (capacity < xpm_row_capacity(encoder, width) || encoder->rows_left == 0 ||
        encoder->slots == NULL)
        return SIZE_MAX;
    output[pos++] = '"';
    for (x = 0; x < width; x++, rgba += 4) {
        int32_t index = *find_slot(encoder, pixel_color(rgba));
        if (index < 0)
            return SIZE_MAX;
        put_code(encoder, (size_t)index, output + pos);
        pos += encoder->cpp;
    }
    if (--encoder->rows_left > 0) {
        memcpy(output + pos, "\",\n", 3);
        pos += 3;
    } else {
        memcpy(output + pos, "\"\n};\n", 5);
        pos += 5;
    }
    return pos;
}
