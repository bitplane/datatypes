#ifndef BITPLANE_UTAHRLE_DECODE_H
#define BITPLANE_UTAHRLE_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
struct utahrle_image { unsigned width, height; uint8_t *rgba; };
/* Number of images concatenated in the file, stopping at the first one
   that doesn't parse. The first image must parse. */
enum codec_result utahrle_count(const uint8_t *data, size_t length,
                                unsigned *count);
/* Decode image index, counted from 0 in file order. */
enum codec_result utahrle_decode(const uint8_t *data, size_t length,
                                 unsigned index, struct utahrle_image *image);
void utahrle_free(struct utahrle_image *image);
#endif
