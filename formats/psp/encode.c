#include <stdlib.h>
#include <string.h>

#include "common/zlib.h"
#include "psp.h"

#define LAYER_NAME "Raster 1"
#define NAME_LENGTH 8u
/* The layer information chunk: size, name, then 115 bytes of fields. */
#define LAYER_INFO (6u + NAME_LENGTH + 115u)

static uint8_t *block(uint8_t *p, unsigned id, uint32_t length)
{
    memcpy(p, "~BK", 4);
    psp_put16(p + 4, id);
    psp_put32(p + 6, length);
    return p + 10;
}

static void rect(uint8_t *p, unsigned width, unsigned height)
{
    psp_put32(p, 0);
    psp_put32(p + 4, 0);
    psp_put32(p + 8, width);
    psp_put32(p + 12, height);
}

enum codec_result psp_encode(const uint8_t *rgba, unsigned width,
                             unsigned height, uint8_t **out, size_t *length)
{
    size_t n = (size_t)width * height, bound, i, size, packed[4];
    unsigned channels = 3, c;
    uint8_t *plane, *data[4] = { NULL, NULL, NULL, NULL }, *file, *p;
    enum codec_result r = CODEC_OK;
    uint32_t layer_length, bank_length;

    *out = NULL;
    *length = 0;
    if (width == 0 || height == 0 || width > PSP_MAX_SIDE || height > PSP_MAX_SIDE)
        return CODEC_INVALID;
    if (n > PSP_MAX_PIXELS)
        return CODEC_TOO_LARGE;
    for (i = 0; i < n; i++)
        if (rgba[i * 4 + 3] != 255) {
            channels = 4;
            break;
        }
    bound = zlib_deflate_bound(n);
    plane = malloc(n);
    if (plane == NULL || bound == 0) {
        free(plane);
        return CODEC_NO_MEMORY;
    }
    /* Red, green, blue, then the transparency mask. */
    for (c = 0; c < channels && r == CODEC_OK; c++) {
        for (i = 0; i < n; i++)
            plane[i] = rgba[i * 4 + c];
        data[c] = malloc(bound);
        if (data[c] == NULL)
            r = CODEC_NO_MEMORY;
        else
            r = zlib_deflate(plane, n, data[c], bound, 9, &packed[c]);
    }
    free(plane);
    if (r != CODEC_OK) {
        for (c = 0; c < 4; c++)
            free(data[c]);
        return r;
    }

    layer_length = LAYER_INFO + 8u;
    for (c = 0; c < channels; c++)
        layer_length += 10u + 16u + (uint32_t)packed[c];
    bank_length = 10u + layer_length;
    size = 36u + 10u + 46u + 10u + bank_length;
    file = calloc(size, 1);
    if (file == NULL) {
        for (c = 0; c < 4; c++)
            free(data[c]);
        return CODEC_NO_MEMORY;
    }

    memcpy(file, "Paint Shop Pro Image File\n\x1a", 27);
    psp_put16(file + 32, 5);
    psp_put16(file + 34, 0);

    /* General image attributes: 72 dpi, LZ77, 24-bit, one raster layer. */
    p = block(file + 36, 0, 46);
    psp_put32(p, 46);
    psp_put32(p + 4, width);
    psp_put32(p + 8, height);
    memcpy(p + 12, "\0\0\0\0\0\0\x52\x40", 8);
    p[20] = 1;
    psp_put16(p + 21, 2);
    psp_put16(p + 23, 24);
    psp_put16(p + 25, 1);
    psp_put32(p + 27, 1u << 24);
    p[31] = 0;
    psp_put32(p + 32, (uint32_t)(n * 3u));
    psp_put32(p + 36, 0);
    psp_put16(p + 40, 1);
    psp_put32(p + 42, 1);
    p += 46;

    p = block(p, 3, bank_length);
    p = block(p, 4, layer_length);
    psp_put32(p, LAYER_INFO);
    psp_put16(p + 4, NAME_LENGTH);
    memcpy(p + 6, LAYER_NAME, NAME_LENGTH);
    p += 6 + NAME_LENGTH;
    p[0] = 1;                       /* raster */
    rect(p + 1, width, height);
    rect(p + 17, width, height);
    p[33] = 255;                    /* opacity */
    p[34] = 0;                      /* normal */
    p[35] = 1;                      /* visible */
    p[70] = 1;                      /* mask linked, as Paint Shop Pro writes */
    /* Five pairs of blend ranges, each open from 0 to 255. */
    for (i = 0; i < 10; i++)
        memcpy(p + 75 + i * 4, "\0\0\xff\xff", 4);
    p += 115;
    psp_put32(p, 8);
    psp_put16(p + 4, channels == 4 ? 2 : 1);
    psp_put16(p + 6, channels);
    p += 8;
    for (c = 0; c < channels; c++) {
        p = block(p, 5, 16u + (uint32_t)packed[c]);
        psp_put32(p, 16);
        psp_put32(p + 4, (uint32_t)packed[c]);
        psp_put32(p + 8, (uint32_t)n);
        psp_put16(p + 12, c < 3 ? 0 : 1);    /* image or transparency */
        psp_put16(p + 14, c < 3 ? c + 1 : 0);
        memcpy(p + 16, data[c], packed[c]);
        p += 16 + packed[c];
        free(data[c]);
    }
    *out = file;
    *length = size;
    return CODEC_OK;
}
