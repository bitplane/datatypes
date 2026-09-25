#ifndef BITPLANE_PAA_ENCODE_H
#define BITPLANE_PAA_ENCODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"

/* Encode straight RGBA as a PAA with one ARGB8888 level, LZSS packed, and the
   average colour, maximum colour, alpha flag and offset taggs that BI's tools
   write. The result is malloc'd; free it with free(). CODEC_TOO_LARGE when
   the packed level doesn't fit its 24-bit size field. */
enum codec_result paa_encode(const uint8_t *rgba, unsigned width, unsigned height,
                             uint8_t **out, size_t *out_len);
#endif
