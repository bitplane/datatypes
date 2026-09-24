#ifndef BITPLANE_MACPAINT_DECODE_H
#define BITPLANE_MACPAINT_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
#define MACPAINT_WIDTH 576
#define MACPAINT_HEIGHT 720
#define MACPAINT_HEADER_SIZE 512
#define MACPAINT_MACBINARY_SIZE 128
/* One byte per pixel: 0 is white, 1 is black. */
struct macpaint_image { unsigned width, height; uint8_t *pixels; };
/* A 512-byte header, optionally after a 128-byte MacBinary header, then
   PackBits data for 720 rows of 72 bytes. */
enum codec_result macpaint_decode(const uint8_t *data, size_t length, struct macpaint_image *image);
void macpaint_free(struct macpaint_image *image);
#endif
