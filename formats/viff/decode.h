#ifndef BITPLANE_VIFF_DECODE_H
#define BITPLANE_VIFF_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
struct viff_image { unsigned width, height; uint8_t *rgba; };
/* Count the images in a file of concatenated VIFF images. Trailing bytes that
   don't start a complete image are ignored; the first image must be complete. */
enum codec_result viff_count(const uint8_t *data, size_t length, unsigned *count);
/* Decode image index (in file order) to RGBA. */
enum codec_result viff_decode(const uint8_t *data, size_t length, unsigned index,
                              struct viff_image *image);
void viff_free(struct viff_image *image);
#endif
