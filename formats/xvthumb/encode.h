#ifndef BITPLANE_XVTHUMB_ENCODE_H
#define BITPLANE_XVTHUMB_ENCODE_H
#include <stddef.h>
#include <stdint.h>
#define XVTHUMB_HEADER_MAX 64
/* Writes the header to output and returns its length, or 0 for a bad size. */
size_t xvthumb_make_header(unsigned width, unsigned height,
                           char output[XVTHUMB_HEADER_MAX]);
/* Writes width bytes of 3:3:2 colour, composited over white, to output. */
void xvthumb_encode_row(const uint8_t *rgba, unsigned width, uint8_t *output);
#endif
