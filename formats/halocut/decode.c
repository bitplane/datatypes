#include "decode.h"
#include <stdlib.h>
#include <string.h>

#define HALOCUT_MAX_PIXELS (16u * 1024u * 1024u)
#define HALOCUT_HEADER 6u
/* Palette entries never straddle one of these blocks. */
#define PAL_BLOCK 512u
#define PAL_ENTRY 6u

static unsigned le16(const uint8_t *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

/* Unpack one row at *pos into row, which is row_bytes long. A run past the
   end is clamped; bytes the row doesn't reach keep the previous row's
   values. *count is the row's unclamped length. row may be NULL to measure. */
static enum codec_result unpack_row(const uint8_t *data, size_t length,
                                    size_t *pos, uint8_t *row,
                                    size_t row_bytes, size_t *count)
{
    size_t p = *pos, n = 0, take;
    unsigned c, run;

    /* Each row starts with its encoded size. The end-of-row code is what
       counts, as in ImageMagick. */
    if (length - p < 2)
        return CODEC_TRUNCATED;
    p += 2;
    for (;;) {
        if (p >= length)
            return CODEC_TRUNCATED;
        c = data[p++];
        run = c & 0x7fu;
        if (run == 0)
            break;
        take = n < row_bytes ? row_bytes - n : 0;
        if (take > run)
            take = run;
        if (c & 0x80u) {
            if (p >= length)
                return CODEC_TRUNCATED;
            if (row != NULL)
                memset(row + n, data[p], take);
            p++;
        } else {
            if (length - p < run)
                return CODEC_TRUNCATED;
            if (row != NULL)
                memcpy(row + n, data + p, take);
            p += run;
        }
        n += run;
    }
    *pos = p;
    *count = n;
    return CODEC_OK;
}

enum codec_result halocut_decode(const uint8_t *data, size_t length,
                                 const struct halocut_palette *palette,
                                 struct halocut_image *image)
{
    unsigned width, height, bits, x, y, max = 0, v;
    size_t pos = HALOCUT_HEADER, first, row_bytes, pixels, i, n;
    uint8_t *row, *index;
    enum codec_result result;

    image->rgba = NULL;
    if (length < HALOCUT_HEADER)
        return CODEC_TRUNCATED;
    width = le16(data);
    height = le16(data + 2);
    if (width == 0 || height == 0 || le16(data + 4) != 0)
        return CODEC_INVALID;
    pixels = (size_t)width * height;
    if (pixels > HALOCUT_MAX_PIXELS)
        return CODEC_TOO_LARGE;

    /* The header has no depth. Like ImageMagick, take it from the first
       row's length: a byte per pixel, or packed 4-bit or 1-bit pixels. */
    result = unpack_row(data, length, &pos, NULL, 0, &first);
    if (result != CODEC_OK)
        return result;
    if (first * 2u == width) {
        bits = 4;
        row_bytes = first;
    } else if (first * 8u == width) {
        bits = 1;
        row_bytes = first;
    } else {
        bits = 8;
        row_bytes = width;
    }

    row = calloc(row_bytes, 1);
    image->rgba = malloc(pixels * 4u);
    if (row == NULL || image->rgba == NULL) {
        free(row);
        halocut_free(image);
        return CODEC_NO_MEMORY;
    }
    /* Indices go in the last quarter of the RGBA buffer. Expanding them
       front to back never overwrites one that hasn't been read. */
    index = image->rgba + pixels * 3u;
    pos = HALOCUT_HEADER;
    for (y = 0; y < height; y++) {
        uint8_t *out = index + (size_t)y * width;
        result = unpack_row(data, length, &pos, row, row_bytes, &n);
        if (result != CODEC_OK) {
            free(row);
            halocut_free(image);
            return result;
        }
        for (x = 0; x < width; x++) {
            if (bits == 8)
                v = row[x];
            else if (bits == 4)
                v = (row[x / 2u] >> (x & 1u ? 0 : 4)) & 0x0fu;
            else
                v = (row[x / 8u] >> (7u - (x & 7u))) & 1u;
            out[x] = (uint8_t)v;
            if (v > max)
                max = v;
        }
    }
    free(row);

    for (i = 0; i < pixels; i++) {
        uint8_t *p = image->rgba + i * 4u;
        v = index[i];
        if (palette != NULL) {
            const uint8_t *c = palette->rgb + (v < palette->count ? v : 0) * 3u;
            p[0] = c[0];
            p[1] = c[1];
            p[2] = c[2];
        } else {
            /* A grey ramp, except that ImageMagick shows pictures using
               only 0 and 1 in black and white. */
            if (max <= 1)
                v = v ? 255u : 0u;
            p[0] = p[1] = p[2] = (uint8_t)v;
        }
        p[3] = 255;
    }
    image->width = width;
    image->height = height;
    return CODEC_OK;
}

void halocut_free(struct halocut_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
}

/* Scale a component with the given maximum to 8 bits. This rounds through
   16 bits as ImageMagick does, so the two agree exactly. A maximum of 0
   means 65535. */
static uint8_t scale(unsigned value, unsigned max)
{
    uint64_t q = value;

    if (max != 0 && max != 65535u) {
        q = (2u * ((uint64_t)value * 65535u + (max >> 1)) + max) /
            (2u * (uint64_t)max);
        if (q > 65535u)
            q = 65535u;
    }
    return (uint8_t)(((q + 128u) - ((q + 128u) >> 8)) >> 8);
}

enum codec_result halocut_palette(const uint8_t *data, size_t length,
                                  struct halocut_palette *palette)
{
    unsigned max_index, max[3], i, k;
    size_t pos = HALOCUT_PAL_HEADER;

    if (length < HALOCUT_PAL_HEADER || data[0] != 'A' || data[1] != 'H')
        return CODEC_INVALID;
    max_index = le16(data + 12);
    if (max_index < 1)
        return CODEC_INVALID;
    for (k = 0; k < 3; k++)
        max[k] = le16(data + 14 + k * 2u);
    palette->count = max_index < 256 ? max_index + 1u : 256u;
    for (i = 0; i < palette->count; i++) {
        if (pos % PAL_BLOCK > PAL_BLOCK - PAL_ENTRY)
            pos += PAL_BLOCK - pos % PAL_BLOCK;
        /* Entries past the end of a short file are black. */
        for (k = 0; k < 3; k++, pos += 2)
            palette->rgb[i * 3u + k] =
                scale(pos + 2 <= length ? le16(data + pos) : 0, max[k]);
    }
    return CODEC_OK;
}
