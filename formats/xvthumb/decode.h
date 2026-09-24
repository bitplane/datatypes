#ifndef BITPLANE_XVTHUMB_DECODE_H
#define BITPLANE_XVTHUMB_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
struct xvthumb_image { unsigned width, height; uint8_t *rgba; };
/* Pixels are 3:3:2 RGB, expanded as v * 255 / max rounded down; the result is opaque. */
enum codec_result xvthumb_decode(const uint8_t *data, size_t length,
                                 struct xvthumb_image *image);
void xvthumb_free(struct xvthumb_image *image);
#endif
