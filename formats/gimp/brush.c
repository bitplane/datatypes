#include "gimp.h"
#include <stdlib.h>
#include <string.h>

#define GBR_V1_HEADER 20u
#define GBR_HEADER 28u
#define PAT_HEADER 24u
/* GIMP reads a pipe's text lines up to this length. */
#define GIH_MAX_LINE 1024u

struct brush {
    unsigned width, height, bytes;
    size_t pixels;  /* offset of the mask or RGBA data */
    size_t pixmap;  /* offset of a .gpb colour pattern, or 0 */
    size_t end;     /* offset just past the brush */
};

static enum codec_result check_size(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
        return CODEC_INVALID;
    if (width > GIMP_MAX_SIDE || height > GIMP_MAX_SIDE ||
        (uint64_t)width * height > GIMP_MAX_PIXELS)
        return CODEC_TOO_LARGE;
    return CODEC_OK;
}

static enum codec_result alloc_image(struct gimp_image *image,
                                     unsigned width, unsigned height)
{
    image->rgba = malloc((size_t)width * height * 4u);
    if (image->rgba == NULL)
        return CODEC_NO_MEMORY;
    image->width = width;
    image->height = height;
    return CODEC_OK;
}

/* The obsolete .gpb layout follows a grey brush with an RGB pattern of the
   same size. Anything else after the brush isn't part of it. */
static size_t find_pixmap(const uint8_t *data, size_t length, size_t at,
                          const struct brush *brush)
{
    const uint8_t *p = data + at;
    uint32_t header;

    if (length - at < PAT_HEADER || memcmp(p + 20, "GPAT", 4) != 0 ||
        gimp_be32(p + 4) != 1 || gimp_be32(p + 16) != 3 ||
        gimp_be32(p + 8) != brush->width || gimp_be32(p + 12) != brush->height)
        return 0;
    header = gimp_be32(p);
    if (header <= PAT_HEADER || header > length - at)
        return 0;
    return at + header;
}

/* Check one brush starting at data[0]. */
static enum codec_result parse_brush(const uint8_t *data, size_t length,
                                     struct brush *brush)
{
    uint32_t header, version, width, height, bytes;
    uint64_t size;
    enum codec_result result;

    if (length < GBR_V1_HEADER)
        return CODEC_TRUNCATED;
    header = gimp_be32(data);
    version = gimp_be32(data + 4);
    width = gimp_be32(data + 8);
    height = gimp_be32(data + 12);
    bytes = gimp_be32(data + 16);
    if (version == 2 || version == 3) {
        if (length < GBR_HEADER)
            return CODEC_TRUNCATED;
        if (memcmp(data + 20, "GIMP", 4) != 0 || header < GBR_HEADER)
            return CODEC_INVALID;
    } else if (version != 1 || header < GBR_V1_HEADER) {
        return CODEC_INVALID;
    }
    /* Version 3 is CinePaint's 16-bit float mask. */
    if (bytes != 1 && bytes != 4)
        return CODEC_INVALID;
    result = check_size(width, height);
    if (result != CODEC_OK)
        return result;
    size = (uint64_t)header + (uint64_t)width * height * bytes;
    if (size > length)
        return CODEC_TRUNCATED;
    brush->width = width;
    brush->height = height;
    brush->bytes = bytes;
    brush->pixels = header;
    brush->end = (size_t)size;
    brush->pixmap = 0;
    if (bytes == 1) {
        size_t pixmap = find_pixmap(data, length, brush->end, brush);
        if (pixmap != 0) {
            size = pixmap + (uint64_t)width * height * 3u;
            if (size > length)
                return CODEC_TRUNCATED;
            brush->pixmap = pixmap;
            brush->end = (size_t)size;
        }
    }
    return CODEC_OK;
}

/* Grey brushes show as GIMP opens them: black paint on white. */
static enum codec_result brush_pixels(const uint8_t *data,
                                      const struct brush *brush,
                                      struct gimp_image *image)
{
    size_t i, n = (size_t)brush->width * brush->height;
    const uint8_t *in = data + brush->pixels;
    const uint8_t *rgb = data + brush->pixmap;
    uint8_t *out;
    enum codec_result result = alloc_image(image, brush->width, brush->height);

    if (result != CODEC_OK)
        return result;
    out = image->rgba;
    if (brush->bytes == 4) {
        memcpy(out, in, n * 4u);
        return CODEC_OK;
    }
    for (i = 0; i < n; i++, out += 4) {
        if (brush->pixmap != 0) {
            out[0] = rgb[i * 3];
            out[1] = rgb[i * 3 + 1];
            out[2] = rgb[i * 3 + 2];
            out[3] = in[i];
        } else {
            out[0] = out[1] = out[2] = (uint8_t)(255u - in[i]);
            out[3] = 255;
        }
    }
    return CODEC_OK;
}

enum codec_result gbr_decode(const uint8_t *data, size_t length,
                             struct gimp_image *image)
{
    struct brush brush;
    enum codec_result result = parse_brush(data, length, &brush);
    return result != CODEC_OK ? result : brush_pixels(data, &brush, image);
}

/* Length of the text line at data[0] without its newline, or GIH_MAX_LINE
   if it's missing or too long. */
static size_t line_length(const uint8_t *data, size_t length)
{
    size_t i;
    for (i = 0; i < length && i < GIH_MAX_LINE; i++)
        if (data[i] == '\n')
            return i;
    return GIH_MAX_LINE;
}

enum codec_result gih_parse_header(const uint8_t *data, size_t length,
                                           size_t *header, unsigned long *cells)
{
    size_t name = line_length(data, length), at, line, i;
    unsigned long n = 0;
    int digits = 0;

    if (name == GIH_MAX_LINE)
        return length <= GIH_MAX_LINE ? CODEC_TRUNCATED : CODEC_INVALID;
    at = name + 1;
    line = line_length(data + at, length - at);
    if (line == GIH_MAX_LINE)
        return length - at <= GIH_MAX_LINE ? CODEC_TRUNCATED : CODEC_INVALID;
    for (i = 0; i < line && (data[at + i] == ' ' || data[at + i] == '\t'); i++)
        ;
    if (i < line && data[at + i] == '+')
        i++;
    for (; i < line && data[at + i] >= '0' && data[at + i] <= '9'; i++) {
        digits = 1;
        if (n < 100000000ul)
            n = n * 10u + (unsigned long)(data[at + i] - '0');
    }
    /* The rest of the line is painting behaviour, not picture content. */
    if (!digits || n == 0)
        return CODEC_INVALID;
    *header = at + line + 1;
    *cells = n;
    return CODEC_OK;
}

enum codec_result gih_decode(const uint8_t *data, size_t length, long index,
                             struct gimp_image *image, unsigned *count)
{
    struct brush brush, chosen;
    unsigned long cells, i;
    size_t at;
    enum codec_result result = gih_parse_header(data, length, &at, &cells);

    if (result != CODEC_OK)
        return result;
    /* Every cell takes at least a header and one pixel. */
    if (cells > (length - at) / (GBR_V1_HEADER + 1u))
        return CODEC_TRUNCATED;
    memset(&chosen, 0, sizeof chosen);
    for (i = 0; i < cells; i++) {
        result = parse_brush(data + at, length - at, &brush);
        if (result != CODEC_OK)
            return result;
        if ((long)i == index) {
            chosen = brush;
            chosen.pixels += at;
            if (chosen.pixmap != 0)
                chosen.pixmap += at;
        }
        at += brush.end;
    }
    *count = (unsigned)cells;
    if (index < 0 || (unsigned long)index >= cells)
        return CODEC_INVALID;
    return brush_pixels(data, &chosen, image);
}

enum codec_result pat_decode(const uint8_t *data, size_t length,
                             struct gimp_image *image)
{
    uint32_t header, width, height, bytes;
    uint64_t size;
    size_t i, n;
    const uint8_t *in;
    uint8_t *out;
    enum codec_result result;

    if (length < PAT_HEADER)
        return CODEC_TRUNCATED;
    header = gimp_be32(data);
    width = gimp_be32(data + 8);
    height = gimp_be32(data + 12);
    bytes = gimp_be32(data + 16);
    if (memcmp(data + 20, "GPAT", 4) != 0 || gimp_be32(data + 4) != 1 ||
        header <= PAT_HEADER || bytes < 1 || bytes > 4)
        return CODEC_INVALID;
    result = check_size(width, height);
    if (result != CODEC_OK)
        return result;
    size = (uint64_t)header + (uint64_t)width * height * bytes;
    if (size > length)
        return CODEC_TRUNCATED;
    result = alloc_image(image, width, height);
    if (result != CODEC_OK)
        return result;
    in = data + header;
    out = image->rgba;
    n = (size_t)width * height;
    for (i = 0; i < n; i++, in += bytes, out += 4) {
        if (bytes <= 2) {
            out[0] = out[1] = out[2] = in[0];
            out[3] = bytes == 2 ? in[1] : 255;
        } else {
            out[0] = in[0];
            out[1] = in[1];
            out[2] = in[2];
            out[3] = bytes == 4 ? in[3] : 255;
        }
    }
    return CODEC_OK;
}
