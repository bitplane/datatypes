#ifndef BITPLANE_SCT_DECODE_H
#define BITPLANE_SCT_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
struct sct_image { unsigned width, height; uint8_t *rgba; };
/* Decodes a Scitex CT file (any mix of the C, M, Y and K separations) to
   opaque RGBA. Other HandShake types (LW, BM, PG, TX) are CODEC_INVALID. */
enum codec_result sct_decode(const uint8_t *data, size_t length,
                             struct sct_image *image);
void sct_free(struct sct_image *image);
#endif
