#ifndef BITPLANE_SUNRASTER_ENCODE_H
#define BITPLANE_SUNRASTER_ENCODE_H
#include <stddef.h>
#include <stdint.h>
/* Header for an uncompressed (type 1) 24-bit image with no colormap. */
int sunraster_make_header(unsigned width, unsigned height, uint8_t header[32]);
/* Encode one RGBA row as BGR over white, padded to an even length. */
size_t sunraster_encode_row(const uint8_t *rgba, unsigned width,
                            uint8_t *output, size_t capacity);
#endif
