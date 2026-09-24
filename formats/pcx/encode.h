#ifndef BITPLANE_PCX_ENCODE_H
#define BITPLANE_PCX_ENCODE_H
#include <stddef.h>
#include <stdint.h>
int pcx_make_header(unsigned width, unsigned height, uint8_t header[128]);
/* Encode one RGBA row as three RLE-compressed, even-padded RGB planes. */
size_t pcx_encode_row(const uint8_t *rgba, unsigned width, uint8_t *output, size_t capacity);
#endif
