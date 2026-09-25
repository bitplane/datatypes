#ifndef BITPLANE_VIFF_ENCODE_H
#define BITPLANE_VIFF_ENCODE_H
#include <stddef.h>
#include <stdint.h>
/* A saved file is the header, then each band's bytes for the whole image:
   red, green, blue and, if any pixel isn't opaque, alpha. */
#define VIFF_WRITE_HEADER 1024u
/* 1 if any pixel in the RGBA row isn't opaque. */
int viff_row_has_alpha(const uint8_t *rgba, unsigned width);
/* Header for a big-endian 8-bit RGB (3 bands) or RGBA (4 bands) image. */
int viff_make_header(unsigned width, unsigned height, unsigned bands,
                     uint8_t header[VIFF_WRITE_HEADER]);
/* Copy one band (0 red to 3 alpha) of an RGBA row; returns width or 0. */
size_t viff_encode_band(const uint8_t *rgba, unsigned width, unsigned band,
                        uint8_t *output, size_t capacity);
#endif
