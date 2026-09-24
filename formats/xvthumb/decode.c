#include "decode.h"
#include <stdlib.h>
#include <string.h>

#define XVTHUMB_MAX_PIXELS (16u * 1024u * 1024u)

static const char magic[6] = { 'P', '7', ' ', '3', '3', '2' };

static int space(uint8_t c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f';
}

/* Offset just past the next '\n' at or after pos, or 0 if there is none. */
static size_t next_line(const uint8_t *data, size_t length, size_t pos)
{
    const uint8_t *nl = memchr(data + pos, '\n', length - pos);
    return nl == NULL ? 0 : (size_t)(nl - data) + 1u;
}

/* Reads decimal digits in [*pos, end), saturating at 65536, after an optional
   '+' that netpbm and Pillow both allow. 0 if there are no digits. */
static int number(const uint8_t *data, size_t *pos, size_t end, unsigned long *value)
{
    size_t start;

    *value = 0;
    if (*pos < end && data[*pos] == '+')
        (*pos)++;
    start = *pos;
    while (*pos < end && data[*pos] >= '0' && data[*pos] <= '9') {
        *value = *value * 10u + (data[*pos] - '0');
        if (*value > 65536u)
            *value = 65536u;
        (*pos)++;
    }
    return *pos > start;
}

static void skip_space(const uint8_t *data, size_t *pos, size_t end)
{
    while (*pos < end && space(data[*pos]))
        (*pos)++;
}

void xvthumb_free(struct xvthumb_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
    image->width = image->height = 0;
}

enum codec_result xvthumb_decode(const uint8_t *data, size_t length,
                                 struct xvthumb_image *image)
{
    unsigned long width, height, maxval;
    size_t pos, end, pixels, i;

    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->rgba = NULL;
    if (data == NULL)
        return CODEC_TRUNCATED;
    if (memcmp(data, magic, length < 6 ? length : 6) != 0)
        return CODEC_INVALID;
    if (length < 6)
        return CODEC_TRUNCATED;
    /* The rest of the magic line is ignored, as netpbm and Pillow do. */
    pos = next_line(data, length, 6);
    /* Skip every comment line, including any after #END_OF_COMMENTS. */
    while (pos != 0 && pos < length && data[pos] == '#')
        pos = next_line(data, length, pos);
    if (pos == 0 || pos >= length)
        return CODEC_TRUNCATED;
    /* "width height maxval" on one line; the pixels follow its newline. */
    end = next_line(data, length, pos);
    if (end == 0)
        return CODEC_TRUNCATED;
    end--;
    skip_space(data, &pos, end);
    if (!number(data, &pos, end, &width) || pos == end || !space(data[pos]))
        return CODEC_INVALID;
    skip_space(data, &pos, end);
    if (!number(data, &pos, end, &height) || (pos < end && !space(data[pos])))
        return CODEC_INVALID;
    skip_space(data, &pos, end);
    /* Pillow reads files without a maxval. When there is one, it must be 255
       as netpbm requires; anything after it on the line is ignored. */
    if (pos < end && (!number(data, &pos, end, &maxval) || maxval != 255u))
        return CODEC_INVALID;
    if (width == 0 || height == 0)
        return CODEC_INVALID;
    if (width > 65535u || height > 65535u ||
        (size_t)width * height > XVTHUMB_MAX_PIXELS)
        return CODEC_TOO_LARGE;
    pixels = (size_t)width * height;
    pos = end + 1u;
    /* Bytes after the last pixel are ignored. */
    if (length - pos < pixels)
        return CODEC_TRUNCATED;
    image->rgba = malloc(pixels * 4u);
    if (image->rgba == NULL)
        return CODEC_NO_MEMORY;
    image->width = (unsigned)width;
    image->height = (unsigned)height;
    for (i = 0; i < pixels; i++) {
        unsigned v = data[pos + i];
        uint8_t *dst = image->rgba + i * 4u;
        dst[0] = (uint8_t)((v >> 5) * 255u / 7u);
        dst[1] = (uint8_t)(((v >> 2) & 7u) * 255u / 7u);
        dst[2] = (uint8_t)((v & 3u) * 85u);
        dst[3] = 255;
    }
    return CODEC_OK;
}
