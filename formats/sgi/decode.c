#include "decode.h"

#include <stdlib.h>
#include <string.h>

/* Keep the decoded image below 64 MiB on small AROS machines. */
#define SGI_MAX_PIXELS (16u * 1024u * 1024u)
#define SGI_MAGIC 474u
#define SGI_HEADER 512u

enum { CMAP_NORMAL, CMAP_DITHERED, CMAP_SCREEN, CMAP_COLORMAP };

struct layout {
    unsigned width;
    unsigned height;
    unsigned channels; /* stored in the file */
    unsigned used;     /* decoded: extra channels beyond RGBA are ignored */
    unsigned bpc;
    unsigned colormap;
};

static unsigned be16(const uint8_t *p)
{
    return ((unsigned)p[0] << 8) | p[1];
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

/* Values are big-endian, so p[0] is the top byte of a 16-bit value too. */
static void store(const struct layout *l, uint8_t *pixel, unsigned channel,
                  unsigned value)
{
    if (l->colormap == CMAP_DITHERED) {
        /* Red in bits 0-2, green in 3-5, blue in 6-7. */
        pixel[0] = (uint8_t)(((value & 7u) * 255u + 3u) / 7u);
        pixel[1] = (uint8_t)((((value >> 3) & 7u) * 255u + 3u) / 7u);
        pixel[2] = (uint8_t)((value >> 6) * 85u);
    } else if (l->used <= 2 && channel == 0) {
        pixel[0] = pixel[1] = pixel[2] = (uint8_t)value;
    } else if (l->used == 2) {
        pixel[3] = (uint8_t)value;
    } else {
        pixel[channel] = (uint8_t)value;
    }
}

static enum codec_result rle_row(const uint8_t *data, size_t length,
                                 uint32_t offset, uint32_t size,
                                 const struct layout *l, uint8_t *row,
                                 unsigned channel)
{
    const unsigned bpc = l->bpc;
    unsigned x = 0;
    size_t pos, end;

    if (offset > length || size > length - offset)
        return CODEC_TRUNCATED;
    pos = offset;
    end = pos + size;
    /* A row that fills exactly may omit its zero terminator. */
    while (end - pos >= bpc) {
        unsigned code = data[pos + bpc - 1u], count = code & 0x7fu;
        pos += bpc;
        if (count == 0)
            break;
        if (count > l->width - x)
            return CODEC_INVALID;
        if (code & 0x80u) {
            if ((size_t)count * bpc > end - pos)
                return CODEC_INVALID;
            while (count--) {
                store(l, row + (size_t)x++ * 4u, channel, data[pos]);
                pos += bpc;
            }
        } else {
            unsigned value;
            if (bpc > end - pos)
                return CODEC_INVALID;
            value = data[pos];
            pos += bpc;
            while (count--)
                store(l, row + (size_t)x++ * 4u, channel, value);
        }
    }
    return x == l->width ? CODEC_OK : CODEC_INVALID;
}

static enum codec_result decode_rle(const uint8_t *data, size_t length,
                                    const struct layout *l, uint8_t *rgba)
{
    const uint64_t rows = (uint64_t)l->height * l->channels;
    const uint8_t *starts = data + SGI_HEADER;
    unsigned c, y;

    if (rows * 8u > length - SGI_HEADER)
        return CODEC_TRUNCATED;
    for (c = 0; c < l->used; c++) {
        for (y = 0; y < l->height; y++) {
            size_t i = (size_t)c * l->height + y;
            /* Scan lines are stored bottom up. */
            uint8_t *row = rgba + (size_t)(l->height - 1u - y) * l->width * 4u;
            enum codec_result result =
                rle_row(data, length, be32(starts + i * 4u),
                        be32(starts + ((size_t)rows + i) * 4u), l, row, c);
            if (result != CODEC_OK)
                return result;
        }
    }
    return CODEC_OK;
}

static enum codec_result decode_verbatim(const uint8_t *data, size_t length,
                                         const struct layout *l, uint8_t *rgba)
{
    const size_t plane = (size_t)l->width * l->height * l->bpc;
    const uint8_t *src = data + SGI_HEADER;
    unsigned c, y, x;

    /* Planes past the ones decoded may be missing. */
    if (plane * l->used > length - SGI_HEADER)
        return CODEC_TRUNCATED;
    for (c = 0; c < l->used; c++) {
        for (y = 0; y < l->height; y++) {
            uint8_t *row = rgba + (size_t)(l->height - 1u - y) * l->width * 4u;
            for (x = 0; x < l->width; x++) {
                store(l, row + (size_t)x * 4u, c, *src);
                src += l->bpc;
            }
        }
    }
    return CODEC_OK;
}

enum codec_result sgi_decode(const uint8_t *data, size_t length,
                             struct sgi_image *image)
{
    struct layout l;
    unsigned storage, dimension;
    enum codec_result result;
    uint8_t *rgba;
    size_t pixels;

    if (image == NULL)
        return CODEC_INVALID;
    memset(image, 0, sizeof *image);
    if (data == NULL || length < SGI_HEADER)
        return CODEC_TRUNCATED;
    storage = data[2];
    l.bpc = data[3];
    dimension = be16(data + 4);
    l.width = be16(data + 6);
    l.height = dimension >= 2 ? be16(data + 8) : 1u;
    l.channels = dimension == 3 ? be16(data + 10) : 1u;
    l.colormap = (unsigned)be32(data + 104);
    if (be16(data) != SGI_MAGIC || storage > 1 || l.bpc < 1 || l.bpc > 2 ||
        dimension < 1 || dimension > 3 || l.colormap > CMAP_COLORMAP ||
        l.width == 0 || l.height == 0 || l.channels == 0)
        return CODEC_INVALID;
    /* Dithered and screen images keep one 8-bit channel of packed values. */
    if (l.colormap == CMAP_DITHERED && l.bpc != 1)
        return CODEC_INVALID;
    l.used = l.colormap == CMAP_DITHERED || l.colormap == CMAP_SCREEN ? 1u
           : l.channels > 4 ? 4u : l.channels;
    pixels = (size_t)l.width * l.height;
    if (pixels > SGI_MAX_PIXELS)
        return CODEC_TOO_LARGE;

    rgba = malloc(pixels * 4u);
    if (rgba == NULL)
        return CODEC_NO_MEMORY;
    memset(rgba, 255, pixels * 4u);
    result = storage ? decode_rle(data, length, &l, rgba)
                     : decode_verbatim(data, length, &l, rgba);
    if (result != CODEC_OK) {
        free(rgba);
        return result;
    }
    image->width = l.width;
    image->height = l.height;
    image->rgba = rgba;
    return CODEC_OK;
}

void sgi_free(struct sgi_image *image)
{
    if (image == NULL)
        return;
    free(image->rgba);
    image->rgba = NULL;
}
