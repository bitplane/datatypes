#ifndef BITPLANE_ICNS_ENCODE_H
#define BITPLANE_ICNS_ENCODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"

/* Whether ICNS has a slot for a width x height image. */
int icns_can_encode(unsigned width, unsigned height);
/* Write RGBA as a one-image ICNS: 24-bit RLE and an 8-bit mask at 16, 32, 48
   and 128 pixels square, PNG at 64, 256, 512 and 1024. Other sizes are
   CODEC_INVALID. The caller frees *out. */
enum codec_result icns_encode(const uint8_t *rgba, unsigned width, unsigned height,
                              uint8_t **out, size_t *length);
#endif
