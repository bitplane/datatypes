#ifndef BITPLANE_WBMP_ENCODE_H
#define BITPLANE_WBMP_ENCODE_H
#include <stddef.h>
#include <stdint.h>
#define WBMP_HEADER_MAX 8
/* Returns the header length, or zero for an unsupported size. */
size_t wbmp_make_header(unsigned width, unsigned height, uint8_t header[WBMP_HEADER_MAX]);
/* Threshold one RGBA row, composited over white, to 1-bit pixels.
   Returns the (width + 7) / 8 bytes written, or zero. */
size_t wbmp_encode_row(const uint8_t *rgba, unsigned width, uint8_t *output, size_t capacity);
#endif
