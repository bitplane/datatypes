#ifndef BITPLANE_MACPAINT_ENCODE_H
#define BITPLANE_MACPAINT_ENCODE_H
#include <stddef.h>
#include <stdint.h>
#include "decode.h"
/* PackBits output for one 72-byte row never exceeds this. */
#define MACPAINT_ROW_MAX 73
/* A version 0 header: no patterns, so readers use their defaults. */
void macpaint_make_header(uint8_t header[MACPAINT_HEADER_SIZE]);
/* Threshold one RGBA row, composited over white, to 1-bit pixels and pack it.
   Pixels past 576 are dropped and missing ones are white; a NULL row with
   width zero is all white. Returns the bytes written, or zero. */
size_t macpaint_encode_row(const uint8_t *rgba, unsigned width, uint8_t *output, size_t capacity);
#endif
