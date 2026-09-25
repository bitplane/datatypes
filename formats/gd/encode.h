#ifndef BITPLANE_GD_ENCODE_H
#define BITPLANE_GD_ENCODE_H
#include <stdint.h>

#define GD_HEADER_SIZE 11

/* A GD 2.x truecolour header with no transparent colour; 0 for a bad size. */
int gd_make_header(unsigned width, unsigned height, uint8_t header[GD_HEADER_SIZE]);
/* One row of RGBA as gd pixels: width * 4 bytes of ARGB, 7-bit alpha. */
void gd_encode_row(const uint8_t *rgba, unsigned width, uint8_t *output);
#endif
