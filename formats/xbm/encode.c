#include "encode.h"
#include <stdio.h>
#include <string.h>

static int is_ident(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

void xbm_make_name(const char *filename, char name[XBM_MAX_NAME + 1])
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
    for (p = start; p < end && n < XBM_MAX_NAME; p++)
        name[n++] = is_ident((unsigned char)*p) ? *p : '_';
    name[n] = '\0';
    if (n == 0)
        strcpy(name, "image");
}

size_t xbm_make_header(const char *name, unsigned width, unsigned height,
                       long hot_x, long hot_y, char *output, size_t capacity)
{
    int n;

    if (width == 0 || height == 0 || width > 65535u || height > 65535u ||
        strlen(name) > XBM_MAX_NAME)
        return 0;
    if (hot_x >= 0 && hot_y >= 0)
        n = snprintf(output, capacity,
                     "#define %s_width %u\n#define %s_height %u\n"
                     "#define %s_x_hot %ld\n#define %s_y_hot %ld\n"
                     "static unsigned char %s_bits[] = {\n",
                     name, width, name, height, name, hot_x, name, hot_y, name);
    else
        n = snprintf(output, capacity,
                     "#define %s_width %u\n#define %s_height %u\n"
                     "static unsigned char %s_bits[] = {\n",
                     name, width, name, height, name);
    return n > 0 && (size_t)n < capacity ? (size_t)n : 0;
}

void xbm_encoder_init(struct xbm_encoder *encoder, unsigned width, unsigned height)
{
    encoder->remaining = (size_t)((width + 7u) / 8u) * height;
    encoder->column = 0;
}

size_t xbm_row_capacity(unsigned width)
{
    /* "   0xff" and a separator per byte, then the closing "};\n". */
    return (size_t)((width + 7u) / 8u) * 9u + 3u;
}

static int dark(const uint8_t *p)
{
    unsigned a = p[3], white = 255u * (255u - a);
    unsigned r = (p[0] * a + white) / 255u;
    unsigned g = (p[1] * a + white) / 255u;
    unsigned b = (p[2] * a + white) / 255u;
    /* Rec. 601 luma of the colour composited over white. */
    return 299u * r + 587u * g + 114u * b < 128000u;
}

size_t xbm_encode_row(struct xbm_encoder *encoder, const uint8_t *rgba,
                      unsigned width, char *output, size_t capacity)
{
    static const char hex[] = "0123456789abcdef";
    unsigned bytes = (width + 7u) / 8u, i, b;
    size_t pos = 0;

    if (capacity < xbm_row_capacity(width) || bytes > encoder->remaining)
        return SIZE_MAX;
    for (i = 0; i < bytes; i++) {
        unsigned value = 0;
        for (b = 0; b < 8u && i * 8u + b < width; b++)
            if (dark(rgba + (size_t)(i * 8u + b) * 4u))
                value |= 1u << b;
        if (encoder->column == 0) {
            memcpy(output + pos, "   ", 3);
            pos += 3;
        }
        output[pos++] = '0';
        output[pos++] = 'x';
        output[pos++] = hex[value >> 4];
        output[pos++] = hex[value & 15u];
        if (--encoder->remaining == 0) {
            memcpy(output + pos, "};\n", 3);
            pos += 3;
        } else if (++encoder->column == 12) {
            output[pos++] = ',';
            output[pos++] = '\n';
            encoder->column = 0;
        } else {
            output[pos++] = ',';
            output[pos++] = ' ';
        }
    }
    return pos;
}
