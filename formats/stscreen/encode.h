#ifndef BITPLANE_STSCREEN_ENCODE_H
#define BITPLANE_STSCREEN_ENCODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
/* The largest file stscreen_encode writes: a header and a double-height page. */
#define STSCREEN_MAX_OUTPUT (128u + 64000u)
/* Writes an uncompressed Paintworks file. The size picks the layout: 320x200
   takes 16 colours (SC0), 640x200 takes 4 (SC1), 640x400 black and white
   (SC2) or else 4 colours (a PG1 page), 320x400 16 colours (PG0) and 640x800
   black and white (PG2). Pixels are composited over white, and every colour
   must be an exact ST or STE level; anything else is CODEC_INVALID. */
enum codec_result stscreen_encode(const uint8_t *rgba, unsigned width,
                                  unsigned height,
                                  uint8_t output[STSCREEN_MAX_OUTPUT],
                                  size_t *size);
#endif
