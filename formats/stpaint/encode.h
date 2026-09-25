#ifndef BITPLANE_STPAINT_ENCODE_H
#define BITPLANE_STPAINT_ENCODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"

/* Room the encoder needs: the header, then a control stream of at most one
   byte per word, then up to 16000 data words. */
#define TINY_MAX_SIZE (37u + 16000u + 32000u)

/* Writes a Tiny picture and sets *size. 320x200 takes 16 colours, 640x200 takes
   4 and 640x400 only black and white. Pixels are composited over white, and
   every colour must be an exact ST or STE level; anything else is
   CODEC_INVALID, since Tiny can't store it losslessly. */
enum codec_result tiny_encode(const uint8_t *rgba, unsigned width, unsigned height,
                              uint8_t output[TINY_MAX_SIZE], size_t *size);
#endif
