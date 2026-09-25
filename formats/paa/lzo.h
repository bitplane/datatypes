#ifndef BITPLANE_PAA_LZO_H
#define BITPLANE_PAA_LZO_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"

/* Expand an LZO1X stream, as Arma 2 and later store large DXT mip levels,
   to exactly out_len bytes. The stream must end with its end marker inside
   in_len; bytes after the marker are ignored. */
enum codec_result paa_lzo_expand(const uint8_t *in, size_t in_len,
                                 uint8_t *out, size_t out_len);
#endif
