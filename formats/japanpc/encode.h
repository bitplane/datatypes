#ifndef BITPLANE_JAPANPC_ENCODE_H
#define BITPLANE_JAPANPC_ENCODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"

/* Bytes that japanpc_encode may need for a picture this size; 0 if a MAG
   can't hold it. */
size_t japanpc_encode_bound(unsigned width, unsigned height);

/* Save RGBA, composited over white, as a MAG with 16 colours or 256.
   CODEC_INVALID if the picture has more than 256 colours. */
enum codec_result japanpc_encode(const uint8_t *rgba, unsigned width,
                                 unsigned height, uint8_t *output,
                                 size_t capacity, size_t *size);

#endif
