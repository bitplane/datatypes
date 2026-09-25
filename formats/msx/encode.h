#ifndef BITPLANE_MSX_ENCODE_H
#define BITPLANE_MSX_ENCODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
/* The largest file msx_encode writes: a screen 7 dump up to its palette. */
#define MSX_MAX_OUTPUT (7u + 0xfaa0u)
/* Writes a BSAVE screen dump. 256x212 is screen 5 when it has at most 16
   colours, each on the V9938's 3-bit levels, or else screen 8 when every
   colour is one of its 256. 512x212 is screen 7, with at most 16 such
   colours. Pixels are composited over white first; anything else is
   CODEC_INVALID. */
enum codec_result msx_encode(const uint8_t *rgba, unsigned width,
                             unsigned height, uint8_t output[MSX_MAX_OUTPUT],
                             size_t *size);
#endif
