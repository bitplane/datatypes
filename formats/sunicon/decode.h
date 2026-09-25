#ifndef BITPLANE_SUNICON_DECODE_H
#define BITPLANE_SUNICON_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
/* One byte per pixel. Depth 1: 1 for a set (black) bit, 0 for white.
   Depth 8: the stored value, which netpbm shows as a grey level. */
struct sunicon_image {
    unsigned width, height, depth;
    uint8_t *pixels;
};
/* Reads Depth=1 and Depth=8 icons with 8, 16 or 32 valid bits per item. */
enum codec_result sunicon_decode(const uint8_t *data, size_t length,
                                 struct sunicon_image *image);
void sunicon_free(struct sunicon_image *image);
#endif
