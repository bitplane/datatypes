#ifndef BITPLANE_NEO_ENCODE_H
#define BITPLANE_NEO_ENCODE_H
#include <stdint.h>
#include "common/result.h"
#include "decode.h"
/* Writes a whole NEOchrome file. The size picks the resolution: 320x200 takes
   16 colours, 640x200 takes 4 and 640x400 only black and white. Pixels are
   composited over white, and every colour must be an exact ST or STE level.
   Anything else is CODEC_INVALID, since NEOchrome can't store it losslessly. */
enum codec_result neo_encode(const uint8_t *rgba, unsigned width,
                             unsigned height, uint8_t output[NEO_FILE_SIZE]);
#endif
