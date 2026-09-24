#ifndef BITPLANE_XWD_ENCODE_H
#define BITPLANE_XWD_ENCODE_H
#include <stddef.h>
#include <stdint.h>
#define XWD_WRITE_HEADER 104u
/* Header for a 24-bit TrueColor ZPixmap in 32-bit big-endian pixels, no colormap. */
int xwd_make_header(unsigned width, unsigned height,
                    uint8_t header[XWD_WRITE_HEADER]);
/* Encode one RGBA row as 00RRGGBB over white; returns width * 4 or 0. */
size_t xwd_encode_row(const uint8_t *rgba, unsigned width,
                      uint8_t *output, size_t capacity);
#endif
