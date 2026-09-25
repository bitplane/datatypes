#ifndef BITPLANE_ANI_ENCODE_H
#define BITPLANE_ANI_ENCODE_H
#include <stddef.h>
#include <stdint.h>

/* Largest file ani_encode can write for this size; 0 if it can't be saved. */
size_t ani_encode_capacity(unsigned width, unsigned height);
/* Encode top-down RGBA as a one-frame ANI whose frame is a one-entry cursor,
   as ico_encode writes it. Returns the size written, or 0 on failure. */
size_t ani_encode(const uint8_t *rgba, unsigned width, unsigned height,
                  unsigned hot_x, unsigned hot_y, uint8_t *output, size_t capacity);
#endif
