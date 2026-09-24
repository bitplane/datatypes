#include "decode.h"
#include <stdlib.h>
#include <string.h>

#define QOI_MAX_PIXELS (16u * 1024u * 1024u)

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static unsigned hash(const uint8_t p[4])
{
    return (p[0] * 3u + p[1] * 5u + p[2] * 7u + p[3] * 11u) & 63u;
}

void qoi_free(struct qoi_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
    image->width = image->height = 0;
}

enum codec_result qoi_decode(const uint8_t *data, size_t length, struct qoi_image *image)
{
    static const uint8_t end[8] = {0,0,0,0,0,0,0,1};
    uint8_t index[64][4] = {{0}}, pixel[4] = {0,0,0,255};
    unsigned width, height, run = 0;
    size_t pixels, pos = 14, i;
    enum codec_result result = CODEC_OK;

    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->rgba = NULL;
    if (data == NULL || length < 22)
        return CODEC_TRUNCATED;
    if (memcmp(data, "qoif", 4) != 0 ||
        (data[12] != 3 && data[12] != 4) || data[13] > 1 ||
        memcmp(data + length - 8, end, 8) != 0)
        return CODEC_INVALID;
    width = be32(data + 4); height = be32(data + 8);
    if (width == 0 || height == 0)
        return CODEC_INVALID;
    if (width > 65535u || height > 65535u ||
        (size_t)width * height > QOI_MAX_PIXELS)
        return CODEC_TOO_LARGE;
    pixels = (size_t)width * height;
    image->rgba = malloc(pixels * 4u);
    if (image->rgba == NULL)
        return CODEC_NO_MEMORY;
    image->width = width; image->height = height;
    for (i = 0; i < pixels; i++) {
        unsigned op;
        if (run != 0) {
            run--;
        } else {
            if (pos >= length - 8) {
                result = CODEC_TRUNCATED;
                goto fail;
            }
            op = data[pos++];
            if (op == 0xfe) {
                if (length - 8 - pos < 3) { result = CODEC_TRUNCATED; goto fail; }
                memcpy(pixel, data + pos, 3); pos += 3;
            } else if (op == 0xff) {
                if (length - 8 - pos < 4) { result = CODEC_TRUNCATED; goto fail; }
                memcpy(pixel, data + pos, 4); pos += 4;
            } else if ((op & 0xc0u) == 0) {
                memcpy(pixel, index[op & 63u], 4);
            } else if ((op & 0xc0u) == 0x40u) {
                pixel[0] = (uint8_t)(pixel[0] + ((op >> 4) & 3u) - 2u);
                pixel[1] = (uint8_t)(pixel[1] + ((op >> 2) & 3u) - 2u);
                pixel[2] = (uint8_t)(pixel[2] + (op & 3u) - 2u);
            } else if ((op & 0xc0u) == 0x80u) {
                unsigned b, dg;
                if (pos >= length - 8) { result = CODEC_TRUNCATED; goto fail; }
                b = data[pos++]; dg = (op & 63u) - 32u;
                pixel[0] = (uint8_t)(pixel[0] + dg + (b >> 4) - 8u);
                pixel[1] = (uint8_t)(pixel[1] + dg);
                pixel[2] = (uint8_t)(pixel[2] + dg + (b & 15u) - 8u);
            } else {
                run = op & 63u;
                if (run >= pixels - i) { result = CODEC_INVALID; goto fail; }
            }
        }
        memcpy(index[hash(pixel)], pixel, 4);
        memcpy(image->rgba + i * 4u, pixel, 4);
    }
    if (pos != length - 8 || run != 0) {
        result = CODEC_INVALID;
        goto fail;
    }
    return CODEC_OK;
fail:
    qoi_free(image);
    return result;
}
