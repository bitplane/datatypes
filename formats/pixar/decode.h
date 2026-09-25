#ifndef BITPLANE_PIXAR_DECODE_H
#define BITPLANE_PIXAR_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
struct pixar_image { unsigned width, height; uint8_t *rgba; };
enum codec_result pixar_decode(const uint8_t *data, size_t length,
                               struct pixar_image *image);
void pixar_free(struct pixar_image *image);
#endif
