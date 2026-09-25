#ifndef BITPLANE_C64_ENCODE_H
#define BITPLANE_C64_ENCODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
#include "decode.h"
#define C64_KOALA_SIZE 10003u
#define C64_ART_STUDIO_SIZE 9009u
/* Large enough for either. */
#define C64_ENCODE_MAX C64_KOALA_SIZE
/* Saves a 320x200 picture, composited over white, whose pixels are all
   Pepto colours. A picture made of pixel pairs is saved as Koala Painter
   when each 4x8 cell has at most three colours besides one background
   shared by the whole picture. Otherwise it is saved as Art Studio hires
   when each 8x8 cell has at most two colours. Anything else is
   CODEC_INVALID, since neither can store it. *size receives the length. */
enum codec_result c64_encode(const uint8_t *rgba, unsigned width, unsigned height,
                             uint8_t output[C64_ENCODE_MAX], size_t *size);
#endif
