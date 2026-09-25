#ifndef BITPLANE_NEO_DECODE_H
#define BITPLANE_NEO_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
/* Header plus 32000 bytes of interleaved bitplanes. */
#define NEO_FILE_SIZE 32128u
struct neo_image { unsigned width, height; uint8_t *rgba; };
/* Low (320x200x16), medium (640x200x4) or high (640x400x2) resolution, opaque RGBA.
   A palette is STE (4 bits per gun) if any colour the resolution uses has a
   fourth bit set, and ST (3 bits) otherwise. High resolution is black on white. */
enum codec_result neo_decode(const uint8_t *data, size_t length,
                             struct neo_image *image);
void neo_free(struct neo_image *image);
#endif
