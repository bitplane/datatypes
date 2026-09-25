#ifndef BITPLANE_ZXSCR_ENCODE_H
#define BITPLANE_ZXSCR_ENCODE_H
#include <stdint.h>
#include "common/result.h"
#include "decode.h"
/* Writes a whole screen from a 256x192 picture. Pixels are composited over
   white, every colour must be one zxscr_decode produces, and each 8x8 cell
   may hold at most two colours of the same brightness (black goes with
   either). Anything else is CODEC_INVALID, since a screen can't store it. */
enum codec_result zxscr_encode(const uint8_t *rgba, unsigned width,
                               unsigned height, uint8_t output[ZXSCR_FILE_SIZE]);
#endif
