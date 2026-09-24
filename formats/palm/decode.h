#ifndef BITPLANE_PALM_DECODE_H
#define BITPLANE_PALM_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"

/* The most bitmaps a family may chain before the rest are ignored. */
#define PALM_MAX_BITMAPS 64u

struct palm_image { unsigned width, height; uint8_t *rgba; };

/* Count the bitmaps in a family, skipping high-density separators. */
enum codec_result palm_count(const uint8_t *data, size_t length, unsigned *count);
/* Index of the largest, then deepest, bitmap. */
enum codec_result palm_best(const uint8_t *data, size_t length, unsigned *index);
/* Decode bitmap `index`, counting from 0 in file order. */
enum codec_result palm_decode(const uint8_t *data, size_t length, unsigned index,
                              struct palm_image *image);
void palm_free(struct palm_image *image);
#endif
