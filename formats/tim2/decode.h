#ifndef BITPLANE_TIM2_DECODE_H
#define BITPLANE_TIM2_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
struct tim2_image { unsigned width, height; uint8_t *rgba; };
/* Number of images in the file: every mipmap level of every complete
   picture, in file order. */
unsigned tim2_count(const uint8_t *data, size_t length);
/* Decode image index, counting as tim2_count does. Indexed images use the
   first palette of their CLUT, or gray without one. */
enum codec_result tim2_decode(const uint8_t *data, size_t length,
                              unsigned index, struct tim2_image *image);
void tim2_free(struct tim2_image *image);
#endif
