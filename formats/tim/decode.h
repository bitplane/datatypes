#ifndef BITPLANE_TIM_DECODE_H
#define BITPLANE_TIM_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
struct tim_image { unsigned width, height; uint8_t *rgba; };
/* Number of complete TIM images stored back to back from the start of data. */
unsigned tim_count(const uint8_t *data, size_t length);
/* Decode image index, counting from 0 in file order. Pixels load opaque;
   indexed images use the first palette of their CLUT, or gray without one. */
enum codec_result tim_decode(const uint8_t *data, size_t length, unsigned index,
                             struct tim_image *image);
void tim_free(struct tim_image *image);
#endif
