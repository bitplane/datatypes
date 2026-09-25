#ifndef BITPLANE_JBIG_DECODE_H
#define BITPLANE_JBIG_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"

#define JBIG_HEADER_SIZE 20

/* One byte a pixel. With one plane it is the bit: 1 is black on white.
   With more it is a grey level, 0 black to 255 white. */
struct jbig_image { unsigned width, height, planes; uint8_t *pixels; };

/* A bi-level image entity (T.82 / ISO 11544), at its full resolution. */
enum codec_result jbig_decode(const uint8_t *data, size_t length, struct jbig_image *image);
void jbig_free(struct jbig_image *image);
#endif
