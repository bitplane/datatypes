#ifndef BITPLANE_GD_DECODE_H
#define BITPLANE_GD_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"

struct gd_image { unsigned width, height; uint8_t *rgba; };

/* Decode a libgd .gd (1.x or 2.x) or .gd2 file, told apart by content. */
enum codec_result gd_decode(const uint8_t *data, size_t length, struct gd_image *image);
void gd_free(struct gd_image *image);
#endif
