#ifndef BITPLANE_ALIAS_DECODE_H
#define BITPLANE_ALIAS_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
struct alias_image { unsigned width, height; uint8_t *rgba; };
/* Number of images: an RLA file chains several through its headers, a PIX
   file holds one. The chain stops at the first header that doesn't parse;
   the first must parse. */
enum codec_result alias_count(const uint8_t *data, size_t length,
                              unsigned *count);
/* Decodes image index, counted from 0 in file order, of a Wavefront RLA or
   Alias PIX file, telling them apart by content. */
enum codec_result alias_decode(const uint8_t *data, size_t length,
                               unsigned index, struct alias_image *image);
void alias_free(struct alias_image *image);
#endif
