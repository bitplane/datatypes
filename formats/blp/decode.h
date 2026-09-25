#ifndef BITPLANE_BLP_DECODE_H
#define BITPLANE_BLP_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"

struct blp_image { unsigned width, height; uint8_t *rgba; };

/* Mip levels are numbered from the largest. Only levels wholly inside the
   file count, up to the first one that is missing. */
enum codec_result blp_count(const uint8_t *data, size_t length, unsigned long *count);
/* Decode one mip level of a BLP1 or BLP2 texture to straight RGBA. */
enum codec_result blp_decode(const uint8_t *data, size_t length, unsigned long index,
                             struct blp_image *image);
void blp_free(struct blp_image *image);
#endif
