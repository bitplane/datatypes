#ifndef BITPLANE_FARBFELD_ENCODE_H
#define BITPLANE_FARBFELD_ENCODE_H
#include <stddef.h>
#include <stdint.h>
int farbfeld_make_header(unsigned width, unsigned height, uint8_t header[16]);
/* Writes width * 8 bytes of 16-bit big-endian RGBA to output. */
void farbfeld_encode_row(const uint8_t *rgba, unsigned width, uint8_t *output);
#endif
