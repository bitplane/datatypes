#ifndef BITPLANE_FTEX_DECODE_H
#define BITPLANE_FTEX_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"

#define FTEX_DXT1 0u
#define FTEX_RGB 1u

struct ftex_image { unsigned width, height; uint8_t *rgba; };

/* Images are numbered in file order: each DXT1 or RGB format in the
   directory in turn, and within it each mip level, largest first. Formats of
   other types are skipped. Only levels wholly inside the file count, and a
   format's count stops at the first level that is cut short. */
enum codec_result ftex_count(const uint8_t *data, size_t length, unsigned long *count);
/* Decode one image to RGBA. DXT1 blocks can be transparent; RGB is opaque. */
enum codec_result ftex_decode(const uint8_t *data, size_t length, unsigned long index,
                              struct ftex_image *image);
void ftex_free(struct ftex_image *image);
#endif
