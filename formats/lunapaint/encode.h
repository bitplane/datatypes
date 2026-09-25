#ifndef BITPLANE_LUNAPAINT_ENCODE_H
#define BITPLANE_LUNAPAINT_ENCODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"

/* A saved project has one layer and one frame: the header, a row of
   pixels at a time, then the trailer holding the layer's opacity,
   visibility and name. */
#define LUNAPAINT_WRITE_HEADER 287u
#define LUNAPAINT_WRITE_TRAILER 55u

/* Lunapaint reads projects in its own byte order, so save in ours. */
int lunapaint_native_big_endian(void);
/* CODEC_INVALID for sizes Lunapaint can't hold (above 32767),
   CODEC_TOO_LARGE above 16M pixels. */
enum codec_result lunapaint_make_header(unsigned width, unsigned height,
                                        int big_endian,
                                        uint8_t out[LUNAPAINT_WRITE_HEADER]);
size_t lunapaint_row_size(unsigned width);
/* Widen each 8-bit channel exactly (times 257). */
void lunapaint_encode_row(const uint8_t *rgba, unsigned width, int big_endian,
                          uint8_t *out);
void lunapaint_make_trailer(int big_endian,
                            uint8_t out[LUNAPAINT_WRITE_TRAILER]);
#endif
