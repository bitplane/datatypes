#ifndef BITPLANE_SPECTRUM_ENCODE_H
#define BITPLANE_SPECTRUM_ENCODE_H
#include <stdint.h>
#include "common/result.h"
#include "decode.h"

/* Store a 320x200 RGBA picture, composited over white, as an SPU file.
   CODEC_INVALID when the file can't hold it exactly: line 0 isn't black,
   a colour isn't an ST or STE level, or a line's colours don't fit its
   48 palette slots. */
enum codec_result spectrum_encode(const uint8_t *rgba, unsigned width,
                                  unsigned height,
                                  uint8_t output[SPU_FILE_SIZE]);
#endif
