#include "pcxdecode.h"
#include <stdlib.h>

#define PCX_MAX_PIXELS (16u * 1024u * 1024u)

static unsigned le16(const uint8_t *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

/* netpbm's palette for files without one: the EGA colours, but with white
   second and no brown. ImageMagick and Pillow show such files black. */
static const uint8_t default_palette[16][3] = {
    {0,0,0}, {255,255,255}, {0,170,0}, {0,170,170},
    {170,0,0}, {170,0,170}, {170,170,0}, {170,170,170},
    {85,85,85}, {85,85,255}, {85,255,85}, {85,255,255},
    {255,85,85}, {255,85,255}, {255,255,85}, {255,255,255}
};

static const uint8_t mono_palette[2][3] = { {0,0,0}, {255,255,255} };

static int next_byte(const uint8_t *data, size_t limit, size_t *pos,
                     unsigned encoding, unsigned *run, uint8_t *repeat,
                     uint8_t *value)
{
    if (*run != 0) {
        *value = *repeat;
        (*run)--;
        return 1;
    }
    if (*pos == limit)
        return 0;
    *value = data[(*pos)++];
    if (encoding == 1 && (*value & 0xc0u) == 0xc0u) {
        *run = *value & 0x3fu;
        if (*run == 0 || *pos == limit)
            return 0;
        *repeat = data[(*pos)++];
        *value = *repeat;
        (*run)--;
    }
    return 1;
}

void pcx_free(struct pcx_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
    image->width = image->height = 0;
}

enum codec_result pcx_decode(const uint8_t *data, size_t length,
                           struct pcx_image *image)
{
    unsigned width, height, bits, planes, bytes_per_line, encoding;
    unsigned min_bytes, x, y, p, run = 0, standard = 1;
    unsigned palette_info;
    size_t pixels, row_bytes, pos = 128, limit = length;
    uint8_t *row, repeat = 0, value;
    const uint8_t *palette = NULL;
    enum codec_result result = CODEC_OK;

    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->rgba = NULL;
    if (data == NULL || length < 128)
        return CODEC_TRUNCATED;
    if (data[0] != 0x0a || (data[1] != 0 && data[1] != 2 &&
        data[1] != 3 && data[1] != 4 && data[1] != 5) ||
        data[2] > 1 || data[64] != 0)
        return CODEC_INVALID;
    bits = data[3]; planes = data[65]; encoding = data[2];
    palette_info = le16(data + 68);
    if (le16(data + 8) < le16(data + 4) ||
        le16(data + 10) < le16(data + 6))
        return CODEC_INVALID;
    width = le16(data + 8) - le16(data + 4) + 1u;
    height = le16(data + 10) - le16(data + 6) + 1u;
    if (width > 65535u || height > 65535u ||
        (size_t)width * height > PCX_MAX_PIXELS)
        return CODEC_TOO_LARGE;
    if (!((planes == 1 && (bits == 1 || bits == 2 || bits == 4 || bits == 8)) ||
          (bits == 1 && planes >= 2 && planes <= 4) ||
          (bits == 8 && (planes == 3 || planes == 4))))
        return CODEC_INVALID;
    bytes_per_line = le16(data + 66);
    min_bytes = (width * bits + 7u) / 8u;
    /* The spec wants an even count, but ImageMagick and netpbm write odd ones. */
    if (bytes_per_line == 0 || bytes_per_line < min_bytes)
        return CODEC_INVALID;
    row_bytes = (size_t)bytes_per_line * planes;
    pixels = (size_t)width * height;
    if (bits == 8 && planes == 1) {
        if (length >= 128 + 769 && data[length - 769] == 0x0c) {
            palette = data + length - 768;
            limit = length - 769;
        } else if (palette_info != 2) {
            return CODEC_INVALID;
        }
    } else if (bits < 8) {
        for (p = 0; p < 48; p++)
            standard &= data[16 + p] == 0;
        /* 1-bit images whose two colours are the same, as Pillow and blank
           palettes give, are black and white, as every reader shows them. */
        if (bits == 1 && planes == 1 && data[16] == data[19] &&
            data[17] == data[20] && data[18] == data[21])
            palette = &mono_palette[0][0];
        else
            palette = standard ? &default_palette[0][0] : data + 16;
    }
    row = malloc(row_bytes);
    if (row == NULL)
        return CODEC_NO_MEMORY;
    image->rgba = malloc(pixels * 4u);
    if (image->rgba == NULL) {
        free(row);
        return CODEC_NO_MEMORY;
    }
    image->width = width; image->height = height;
    for (y = 0; y < height; y++) {
        for (p = 0; p < row_bytes; p++) {
            if (!next_byte(data, limit, &pos, encoding, &run, &repeat, &value)) {
                result = CODEC_TRUNCATED;
                goto done;
            }
            row[p] = value;
        }
        for (x = 0; x < width; x++) {
            uint8_t *rgba = image->rgba + ((size_t)y * width + x) * 4u;
            unsigned index = 0;
            rgba[3] = 255;
            if (bits == 8 && planes >= 3) {
                for (p = 0; p < planes; p++)
                    rgba[p] = row[(size_t)p * bytes_per_line + x];
            } else {
                if (bits == 8) {
                    index = row[x];
                } else if (planes == 1) {
                    unsigned shift = 8u - bits - (x * bits & 7u);
                    index = (row[x * bits / 8u] >> shift) & ((1u << bits) - 1u);
                } else {
                    for (p = 0; p < planes; p++)
                        index |= ((row[(size_t)p * bytes_per_line + x / 8u] >>
                                  (7u - x % 8u)) & 1u) << p;
                }
                if (palette != NULL) {
                    rgba[0] = palette[index * 3u];
                    rgba[1] = palette[index * 3u + 1u];
                    rgba[2] = palette[index * 3u + 2u];
                } else {
                    rgba[0] = rgba[1] = rgba[2] = (uint8_t)index;
                }
            }
        }
    }
done:
    free(row);
    if (result != CODEC_OK)
        pcx_free(image);
    return result;
}
