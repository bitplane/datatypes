#include "decode.h"
#include <stdlib.h>

#define MTV_MAX_PIXELS (16u * 1024u * 1024u)
/* ImageMagick reads the header line into a 4096-byte buffer. */
#define MTV_MAX_LINE 4096u

/* One MTV image: a text line "width height", then width * height RGB triples. */
struct frame {
    unsigned width, height;
    size_t pixels, end;
    int has_header;
};

static int is_blank(uint8_t c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f';
}

static int too_large(unsigned long width, unsigned long height)
{
    return width > 65535u || height > 65535u || width * height > MTV_MAX_PIXELS;
}

/* Reads "[blanks][+]digits" as sscanf's %d would, saturating above 65535. */
static int number(const uint8_t *line, size_t size, size_t *pos, unsigned long *value)
{
    size_t i = *pos;

    while (i < size && is_blank(line[i]))
        i++;
    if (i < size && line[i] == '+')
        i++;
    if (i >= size || line[i] < '0' || line[i] > '9')
        return 0;
    *value = 0;
    while (i < size && line[i] >= '0' && line[i] <= '9') {
        *value = *value * 10u + (unsigned long)(line[i] - '0');
        if (*value > 65536u)
            *value = 65536u;
        i++;
    }
    *pos = i;
    return 1;
}

/* Anything after the two numbers, up to the newline, is ignored, as by
   ImageMagick and netpbm. has_header is set once the line holds two positive
   numbers, so the result then says only whether the image is usable. */
static enum codec_result mtv_frame(const uint8_t *data, size_t length,
                                   size_t offset, struct frame *f)
{
    const uint8_t *line = data + offset;
    size_t size = 0, pos = 0, avail = length - offset;
    unsigned long width, height;

    f->has_header = 0;
    while (size < avail && size < MTV_MAX_LINE && line[size] != '\n')
        size++;
    if (size == avail)
        return CODEC_TRUNCATED;
    if (size == MTV_MAX_LINE)
        return CODEC_INVALID;
    if (!number(line, size, &pos, &width) || !number(line, size, &pos, &height) ||
        width == 0 || height == 0)
        return CODEC_INVALID;
    f->has_header = 1;
    if (too_large(width, height))
        return CODEC_TOO_LARGE;
    f->width = (unsigned)width;
    f->height = (unsigned)height;
    f->pixels = offset + size + 1;
    if ((length - f->pixels) / 3u < (size_t)width * height)
        return CODEC_TRUNCATED;
    f->end = f->pixels + (size_t)width * height * 3u;
    return CODEC_OK;
}

/* QRT: 16-bit little-endian width and height, then per row a 16-bit row
   number (ignored, as by netpbm) and the red, green and blue planes. */
static enum codec_result qrt_header(const uint8_t *data, size_t length,
                                    unsigned *width, unsigned *height)
{
    if (length < 4)
        return CODEC_TRUNCATED;
    *width = data[0] | (unsigned)data[1] << 8;
    *height = data[2] | (unsigned)data[3] << 8;
    if (*width == 0 || *height == 0)
        return CODEC_INVALID;
    if (too_large(*width, *height))
        return CODEC_TOO_LARGE;
    if ((length - 4) / (2u + 3u * (size_t)*width) < *height)
        return CODEC_TRUNCATED;
    return CODEC_OK;
}

/* A complete first MTV image wins; otherwise a complete QRT file. */
static int is_qrt(const uint8_t *data, size_t length)
{
    struct frame f;
    unsigned width, height;

    return mtv_frame(data, length, 0, &f) != CODEC_OK &&
           qrt_header(data, length, &width, &height) == CODEC_OK;
}

void mtv_free(struct mtv_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
    image->width = image->height = 0;
}

unsigned mtv_count(const uint8_t *data, size_t length)
{
    struct frame f;
    size_t offset = 0;
    unsigned count = 0;

    if (data == NULL)
        return 0;
    if (is_qrt(data, length))
        return 1;
    while (count < 65535u) {
        enum codec_result result = mtv_frame(data, length, offset, &f);
        if (!f.has_header)
            break;
        count++;
        if (result != CODEC_OK)
            break;
        offset = f.end;
    }
    return count;
}

static enum codec_result allocate(struct mtv_image *image,
                                  unsigned width, unsigned height)
{
    image->rgba = malloc((size_t)width * height * 4u);
    if (image->rgba == NULL)
        return CODEC_NO_MEMORY;
    image->width = width;
    image->height = height;
    return CODEC_OK;
}

static enum codec_result decode_qrt(const uint8_t *data, size_t length,
                                    struct mtv_image *image)
{
    unsigned width, height, x, y;
    enum codec_result result = qrt_header(data, length, &width, &height);

    if (result == CODEC_OK)
        result = allocate(image, width, height);
    if (result != CODEC_OK)
        return result;
    for (y = 0; y < height; y++) {
        const uint8_t *row = data + 4 + (size_t)y * (2u + 3u * width) + 2;
        uint8_t *dst = image->rgba + (size_t)y * width * 4u;
        for (x = 0; x < width; x++) {
            dst[x * 4u] = row[x];
            dst[x * 4u + 1u] = row[width + x];
            dst[x * 4u + 2u] = row[2u * width + x];
            dst[x * 4u + 3u] = 255;
        }
    }
    return CODEC_OK;
}

enum codec_result mtv_decode(const uint8_t *data, size_t length, unsigned index,
                             struct mtv_image *image)
{
    struct frame f;
    enum codec_result result;
    size_t offset = 0, i;
    unsigned n, width, height;

    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->rgba = NULL;
    if (data == NULL)
        return CODEC_TRUNCATED;
    if (is_qrt(data, length))
        return index == 0 ? decode_qrt(data, length, image) : CODEC_INVALID;
    for (n = 0;; n++) {
        result = mtv_frame(data, length, offset, &f);
        if (!f.has_header) {
            if (n > 0)
                return CODEC_INVALID;
            /* Neither format: report the error of the one it looks like. */
            if (length > 0 && !is_blank(data[0]) && data[0] != '+' &&
                data[0] != '-' && (data[0] < '0' || data[0] > '9'))
                return qrt_header(data, length, &width, &height);
            return result;
        }
        if (n == index)
            break;
        if (result != CODEC_OK)
            return CODEC_INVALID;
        offset = f.end;
    }
    if (result == CODEC_OK)
        result = allocate(image, f.width, f.height);
    if (result != CODEC_OK)
        return result;
    for (i = 0; i < (size_t)f.width * f.height; i++) {
        image->rgba[i * 4u] = data[f.pixels + i * 3u];
        image->rgba[i * 4u + 1u] = data[f.pixels + i * 3u + 1u];
        image->rgba[i * 4u + 2u] = data[f.pixels + i * 3u + 2u];
        image->rgba[i * 4u + 3u] = 255;
    }
    return CODEC_OK;
}
