#ifndef BITPLANE_CIS_ENCODE_H
#define BITPLANE_CIS_ENCODE_H
#include <stddef.h>
#include <stdint.h>
#include "decode.h"
/* The frame an image is saved in: 128x96 if it fits, as pbmtocis chooses,
   otherwise 256x192, cropping anything larger. Returns zero for an empty
   image. */
int cis_frame(unsigned width, unsigned height, unsigned *frame_width, unsigned *frame_height);
/* Threshold `width` RGBA pixels, composited over white, into 1-bit pixels
   where 1 is black. */
void cis_threshold_row(const uint8_t *rgba, unsigned width, uint8_t *pixels);
/* The most bytes cis_encode writes for a frame. */
size_t cis_encode_bound(unsigned width, unsigned height);
/* Encode a frame of 1-bit pixels (1 is black) as ESC G M or ESC G H, runs of
   at most 94 joined by empty runs, and ESC G N. Returns the bytes written,
   or zero if the frame size is wrong or the output doesn't fit. */
size_t cis_encode(const uint8_t *pixels, unsigned width, unsigned height,
                  uint8_t *output, size_t capacity);
#endif
