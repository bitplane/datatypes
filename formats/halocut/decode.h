#ifndef BITPLANE_HALOCUT_DECODE_H
#define BITPLANE_HALOCUT_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"

#define HALOCUT_PAL_HEADER 40

struct halocut_palette { unsigned count; uint8_t rgb[256 * 3]; };
struct halocut_image { unsigned width, height; uint8_t *rgba; };

/* Decode a Dr. Halo CUT picture. Rows are 8-bit indices, or 4-bit or 1-bit
   when the first row's length says so. palette may be NULL: the picture is
   then a grey ramp, or black and white if it only uses indices 0 and 1. */
enum codec_result halocut_decode(const uint8_t *data, size_t length,
                                 const struct halocut_palette *palette,
                                 struct halocut_image *image);
void halocut_free(struct halocut_image *image);

/* Read a Dr. Halo palette (.PAL) file. CODEC_INVALID when it isn't one. */
enum codec_result halocut_palette(const uint8_t *data, size_t length,
                                  struct halocut_palette *palette);
#endif
