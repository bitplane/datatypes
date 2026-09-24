#include "decode.h"
#include <stdlib.h>
#include <string.h>

#define MSP_HEADER 32u
#define MSP_MAX_PIXELS (16u * 1024u * 1024u)

static unsigned read16(const uint8_t *p)
{
    return p[0] | ((unsigned)p[1] << 8);
}

/* Expand one RLE row into row_bytes bytes. A row that ends early is white;
   a run past the end of the row is cut off. */
static enum codec_result unpack_row(const uint8_t *in, size_t size,
                                    uint8_t *out, size_t row_bytes)
{
    size_t pos = 0, done = 0, count;

    memset(out, 0xff, row_bytes);
    while (pos < size) {
        unsigned type = in[pos++];
        if (type == 0) {
            if (size - pos < 2)
                return CODEC_INVALID;
            count = in[pos];
            if (count > row_bytes - done)
                count = row_bytes - done;
            memset(out + done, in[pos + 1], count);
            pos += 2;
        } else {
            if (size - pos < type)
                return CODEC_INVALID;
            count = type;
            if (count > row_bytes - done)
                count = row_bytes - done;
            memcpy(out + done, in + pos, count);
            pos += type;
        }
        done += count;
    }
    return CODEC_OK;
}

void msp_free(struct msp_image *image)
{
    free(image->pixels);
    image->pixels = NULL;
    image->width = image->height = 0;
}

enum codec_result msp_decode(const uint8_t *data, size_t length, struct msp_image *image)
{
    unsigned width, height;
    size_t row_bytes, y, x, pos, total;
    uint8_t *row = NULL, *out;
    int compressed;
    enum codec_result result = CODEC_OK;

    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->pixels = NULL;
    if (data == NULL || length < 4)
        return CODEC_TRUNCATED;
    if (memcmp(data, "DanM", 4) == 0)
        compressed = 0;
    else if (memcmp(data, "LinS", 4) == 0)
        compressed = 1;
    else
        return CODEC_INVALID;
    if (length < MSP_HEADER)
        return CODEC_TRUNCATED;
    /* The header checksum isn't checked: nothing else depends on it. */
    width = read16(data + 4);
    height = read16(data + 6);
    if (width == 0 || height == 0)
        return CODEC_INVALID;
    if ((size_t)width * height > MSP_MAX_PIXELS)
        return CODEC_TOO_LARGE;
    row_bytes = (width + 7u) / 8u;
    pos = MSP_HEADER;
    if (!compressed) {
        if (length - pos < row_bytes * height)
            return CODEC_TRUNCATED;
    } else {
        /* A map of each row's packed size, then the rows in order. */
        if (length - pos < 2u * height)
            return CODEC_TRUNCATED;
        for (y = 0, total = 0; y < height; y++)
            total += read16(data + pos + 2u * y);
        pos += 2u * height;
        if (length - pos < total)
            return CODEC_TRUNCATED;
        row = malloc(row_bytes);
        if (row == NULL)
            return CODEC_NO_MEMORY;
    }
    image->pixels = malloc((size_t)width * height);
    if (image->pixels == NULL) {
        free(row);
        return CODEC_NO_MEMORY;
    }
    out = image->pixels;
    for (y = 0; y < height; y++) {
        const uint8_t *bits;
        if (compressed) {
            size_t size = read16(data + MSP_HEADER + 2u * y);
            if ((result = unpack_row(data + pos, size, row, row_bytes)) != CODEC_OK)
                break;
            bits = row;
            pos += size;
        } else {
            bits = data + pos;
            pos += row_bytes;
        }
        for (x = 0; x < width; x++)
            *out++ = (bits[x / 8u] >> (7u - x % 8u)) & 1u;
    }
    free(row);
    if (result != CODEC_OK) {
        msp_free(image);
        return result;
    }
    image->width = width;
    image->height = height;
    return CODEC_OK;
}
