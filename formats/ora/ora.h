#ifndef BITPLANE_ORA_ORA_H
#define BITPLANE_ORA_ORA_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"

struct ora_image {
    unsigned width, height;
    uint8_t *rgba;
};

/* Decode the composite (mergedimage.png) of an OpenRaster or Krita file to
   straight 8-bit RGBA. Free with ora_free. */
enum codec_result ora_decode(const uint8_t *data, size_t length, struct ora_image *image);
void ora_free(struct ora_image *image);

/* Write a one-layer OpenRaster file. The caller frees *out. */
enum codec_result ora_encode(const uint8_t *rgba, unsigned width, unsigned height,
                             uint8_t **out, size_t *out_length);

#endif
