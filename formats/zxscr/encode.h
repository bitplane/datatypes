#ifndef BITPLANE_ZXSCR_ENCODE_H
#define BITPLANE_ZXSCR_ENCODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
#include "decode.h"
/* Writes a screen from a 256x192 picture: a standard ZXSCR_FILE_SIZE screen
   when every 8x8 cell fits, otherwise a ZXSCR_TIMEX_SIZE hi-colour screen
   when every 8x1 span does. Pixels are composited over white, every colour
   must be one zxscr_decode produces, and a cell or span may hold at most two
   colours of the same brightness (black goes with either). Anything else is
   CODEC_INVALID, since neither layout can store it. */
enum codec_result zxscr_encode(const uint8_t *rgba, unsigned width, unsigned height,
                               uint8_t output[ZXSCR_TIMEX_SIZE], size_t *length);
#endif
