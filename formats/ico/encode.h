#ifndef BITPLANE_ICO_ENCODE_H
#define BITPLANE_ICO_ENCODE_H
#include <stddef.h>
#include <stdint.h>

#define ICO_MAX_ENCODE_SIDE 256u

/* Largest file ico_encode can write for this size; 0 if it can't be saved. */
size_t ico_encode_capacity(unsigned width, unsigned height);
/* Encode top-down RGBA as a one-entry icon, or a cursor if cursor is nonzero.
   The entry is a 24-bit BMP when every pixel is opaque, 32-bit otherwise.
   Returns the size written, or 0 if it doesn't fit or can't be saved. */
size_t ico_encode(const uint8_t *rgba, unsigned width, unsigned height,
                  int cursor, unsigned hot_x, unsigned hot_y,
                  uint8_t *output, size_t capacity);
#endif
