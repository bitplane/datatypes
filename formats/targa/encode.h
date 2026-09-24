#ifndef BITPLANE_TARGA_ENCODE_H
#define BITPLANE_TARGA_ENCODE_H

#include <stddef.h>
#include <stdint.h>

/* Encode one RGBA scanline as TGA RLE packets, in BGR or BGRA order. */
size_t tga_encode_row(const uint8_t *rgba, unsigned width, unsigned bytes_per_pixel,
                      uint8_t *output, size_t capacity);

#endif
