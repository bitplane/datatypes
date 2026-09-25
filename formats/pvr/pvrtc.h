#ifndef BITPLANE_PVR_PVRTC_H
#define BITPLANE_PVR_PVRTC_H
#include <stddef.h>
#include <stdint.h>

/* PVRTC1 at 4 or 2 bits per pixel. Sides must be powers of two. Blocks are
   4x4 or 8x4 pixels, at least two blocks each way, stored in Morton order. */

/* Bytes of compressed data for an image of this size. */
size_t pvrtc_size(unsigned width, unsigned height, int two_bpp);
/* Decode pvrtc_size() bytes of data to width * height RGBA pixels. */
void pvrtc_decode(const uint8_t *data, unsigned width, unsigned height, int two_bpp,
                  uint8_t *rgba);

#endif
