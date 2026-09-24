#ifndef BITPLANE_FARBFELD_DECODE_H
#define BITPLANE_FARBFELD_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
struct farbfeld_image { unsigned width, height; uint8_t *rgba; };
/* 16-bit channels are rounded to 8 bits; an all-zero alpha channel is shown opaque. */
enum codec_result farbfeld_decode(const uint8_t *data, size_t length,
                                  struct farbfeld_image *image);
void farbfeld_free(struct farbfeld_image *image);
#endif
