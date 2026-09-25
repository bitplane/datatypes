#ifndef BITPLANE_PAA_DECODE_H
#define BITPLANE_PAA_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"

struct paa_image { unsigned width, height; uint8_t *rgba; };

/* Mip levels are numbered in file order, largest first. The count stops at
   the end marker, the end of the file, or a level cut short. */
enum codec_result paa_count(const uint8_t *data, size_t length, unsigned long *count);
/* Decode one mip level to straight RGBA. */
enum codec_result paa_decode(const uint8_t *data, size_t length, unsigned long index,
                             struct paa_image *image);
void paa_free(struct paa_image *image);
#endif
