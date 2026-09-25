#ifndef BITPLANE_XCURSOR_ENCODE_H
#define BITPLANE_XCURSOR_ENCODE_H
#include <stddef.h>
#include <stdint.h>

#define XCURSOR_ENCODED_HEADER 64u

/* File header, one table entry and the image chunk header for a one-image
   cursor with its hotspot at the top left. 0 if the size can't be stored. */
int xcursor_make_header(unsigned width, unsigned height,
                        uint8_t header[XCURSOR_ENCODED_HEADER]);
/* Premultiply one RGBA row into width * 4 bytes of little-endian ARGB. */
void xcursor_encode_row(const uint8_t *rgba, unsigned width, uint8_t *output);
#endif
