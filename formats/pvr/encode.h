#ifndef BITPLANE_PVR_ENCODE_H
#define BITPLANE_PVR_ENCODE_H
#include <stddef.h>
#include <stdint.h>
#define PVR_HEADER_SIZE 52u
/* Nonzero when any pixel in the row is not fully opaque. */
int pvr_row_has_alpha(const uint8_t *rgba, unsigned width);
/* Version 3 header of a single uncompressed sRGB image: 8-bit RGB, or RGBA
   when alpha is set. Returns 0 when the size can't be stored. */
int pvr_make_header(unsigned width, unsigned height, int alpha,
                    uint8_t header[PVR_HEADER_SIZE]);
/* Writes width * 3 bytes, or width * 4 with alpha, to output. */
void pvr_encode_row(const uint8_t *rgba, unsigned width, int alpha, uint8_t *output);
#endif
