#ifndef BITPLANE_WBMP_DECODE_H
#define BITPLANE_WBMP_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
struct wbmp_image { unsigned width, height; uint8_t *rgba; };
/* Type 0 only: 1 bit per pixel, 1 is white. */
enum codec_result wbmp_decode(const uint8_t *data, size_t length, struct wbmp_image *image);
void wbmp_free(struct wbmp_image *image);
#endif
