#ifndef BITPLANE_DCX_PCXENCODE_H
#define BITPLANE_DCX_PCXENCODE_H
/* A copy of formats/pcx/encode.[ch]: keep the two in step until the PCX codec moves to common/. */
#include <stddef.h>
#include <stdint.h>
int pcx_make_header(unsigned width, unsigned height, uint8_t header[128]);
/* Encode one RGBA row as three RLE-compressed, even-padded RGB planes. */
size_t pcx_encode_row(const uint8_t *rgba, unsigned width, uint8_t *output, size_t capacity);
#endif
