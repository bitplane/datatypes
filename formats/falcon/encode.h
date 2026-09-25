#ifndef BITPLANE_FALCON_ENCODE_H
#define BITPLANE_FALCON_ENCODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
/* Bytes falcon_encode writes for a picture this size, or 0 if the picture
   is empty or too large. */
size_t falcon_encode_size(unsigned width, unsigned height);
/* Save RGBA as an uncompressed 24-bit Prism Paint file, the one lossless
   variant these formats have. Alpha is composited over white. */
enum codec_result falcon_encode(const uint8_t *rgba, unsigned width,
                                unsigned height, uint8_t *out, size_t capacity);
#endif
